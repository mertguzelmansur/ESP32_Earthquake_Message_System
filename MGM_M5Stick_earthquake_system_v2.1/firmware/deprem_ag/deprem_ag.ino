// ============================================================
//  Deprem Acil Haberlesme Agi v2.1  -  M5StickC Plus / Plus2  (MGM)
//  Acik Wi-Fi AP + captive portal + mesaj panosu + ESP-NOW mesh
//  + ayarlar + QR + sarsinti algilama + internet kopru (Telegram/Webhook)
// ============================================================
#include <M5Unified.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Update.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "mbedtls/md.h"
#include "page.h"

#define FW_VER     "2.1"
#define MAX_MSGS   200
#define TZ_OFS     (3 * 3600)        // Turkiye UTC+3
#define CFG_MAGIC  0xD202
#define STORE_V1   0x44414731UL
#define STORE_V2   0x44414732UL
#define STORE_V3   0x44414733UL
#define BIT_MASK   0x1FFF

enum : uint8_t { F_RESOLVED = 1, F_DELETED = 2, F_UPLOADED = 4, F_OFFICIAL = 8, F_SHARED = 0x0F, L_UPSENT = 0x10, L_RESSENT = 0x20 };
enum : uint8_t { ST_SAFE, ST_HELP, ST_INJ, ST_TRAP, ST_ANN, ST_SEARCH, ST_VOL, ST_REPLY, ST_SYS, ST_BLD, ST_STOCK, ST_NODE, ST_COUNT };
// gorev: 0 yeni, 1 ekip atandi, 2 yolda, 3 tamamlandi | triyaj: 0 yok,1 yesil,2 sari,3 kirmizi,4 siyah
// med: bit0-3 kan grubu (0 bilinmiyor,1 A+,2 A-,3 B+,4 B-,5 AB+,6 AB-,7 0+,8 0-), bit4 insulin, bit5 diyaliz, bit6 oksijen, bit7 duzenli ilac
// dmg: bit0-1 hasar (0 yok,1 hafif,2 orta,3 agir), bit2 girmek yasak, bit3 iceriden ses geliyor

struct __attribute__((packed)) Msg {
  uint32_t id, ts, parent;
  uint8_t status, urg, people, flags;
  uint16_t needs, skills;
  uint8_t task, team, triage, med, dmg, rev;   // rev: degisebilir alanlar icin surum (son yazan kazanir)
  uint32_t sig;                                // resmi mesaj imzasi (HMAC-SHA256 ilk 4 bayt)
  char name[30];
  char loc[48];
  char phone[14];
  char text[124];
};
struct __attribute__((packed)) MsgV2 {
  uint32_t id, ts, parent; uint8_t status, urg, people, flags; uint16_t needs, skills;
  char name[30]; char loc[48]; char phone[14]; char text[132];
};
struct __attribute__((packed)) MsgV1 {
  uint32_t id, ts; uint8_t status, urg, people, flags;
  char name[20]; char loc[44]; char phone[14]; char text[152];
};
struct __attribute__((packed)) PktMsg   { uint8_t m0, m1, type, ttl; Msg m; };
struct __attribute__((packed)) PktHello { uint8_t m0, m1, type, ttl; uint32_t now; uint16_t count; uint8_t gw, bat, cli, chg, urg, open; char place[30]; };
static_assert(sizeof(PktMsg) <= 250, "ESP-NOW paketi cok buyuk");
static_assert(sizeof(Msg) == 246, "Msg boyutu");

struct Cfg {
  uint16_t magic;
  char ssid[33]; uint8_t ch, txp, maxcli;
  char place[41]; char notice[201];
  uint8_t meshEn, ttl, syncSec;
  uint8_t bright; uint16_t dimSec; uint8_t vol, beepLvl;
  uint8_t reqName, reqLoc, rateSec; uint16_t hideH;
  uint8_t psave;
  uint8_t quakeEn, quakeThr, quakePost;
  uint8_t upMode; char upSsid[33]; char upPass[65]; char tgToken[64]; char tgChat[24]; char hookUrl[160];
  // v2.1 (sona eklendi: eski ayarlar korunur)
  char netKey[33]; uint8_t lowBat;
} cfg;

struct Peer { uint8_t mac[6]; uint32_t seen; uint8_t bat, cli, chg, gw, urg, open; uint16_t count; char place[30]; } peers[16];
const wifi_power_t TXP[6] = { WIFI_POWER_8_5dBm, WIFI_POWER_11dBm, WIFI_POWER_13dBm, WIFI_POWER_15dBm, WIFI_POWER_17dBm, WIFI_POWER_19_5dBm };
const char *ST_TR[ST_COUNT] = { "Güvende", "Yardım lazım", "Yaralı var", "Enkaz altında", "Duyuru", "Aranıyor", "Gönüllü", "Cevap", "Sarsıntı (sensör)", "Bina/enkaz noktası", "Dağıtım noktası", "Nokta uyarısı" };
const char *ST_ICON[ST_COUNT] = { "✅", "🙋", "🩹", "🧱", "📢", "🔎", "🤝", "💬", "📳", "🏚", "🏪", "🔋" };
const char *TASK_TR[4] = { "Yeni", "Ekip atandı", "Yolda", "Tamamlandı" };
const char *TRI_TR[5] = { "", "YEŞİL", "SARI", "KIRMIZI", "SİYAH" };
const char *BLOOD[9] = { "", "A+", "A-", "B+", "B-", "AB+", "AB-", "0+", "0-" };
const char *MED_TR[4] = { "İnsülin", "Diyaliz", "Oksijen cihazı", "Düzenli ilaç" };
const char *DMG_TR[4] = { "", "Hafif hasar", "Orta hasar", "Ağır hasar" };
const char *NEED_TR[13] = { "Su", "Gıda", "İlaç", "Battaniye", "Çadır", "Arama-kurtarma", "Sağlık ekibi", "Bebek maması", "Şarj/Elektrik", "Isınma", "Hijyen/Tuvalet", "Giysi", "Tahliye/Ulaşım" };
const char *SKILL_TR[13] = { "Doktor", "Hemşire/Sağlıkçı", "İlk yardım", "Arama-kurtarma", "İnşaat müh./Mimar", "İş makinesi operatörü", "Elektrikçi", "Tesisatçı", "Aşçı", "Şoför/Araç", "Tercüman", "Psikolog", "Genel gönüllü" };

IPAddress apIP(192, 168, 4, 1);
WebServer server(80);
DNSServer dns;
Preferences prefs;
M5Canvas cv(&M5.Display);

Msg msgs[MAX_MSGS];
int nMsgs = 0;
uint32_t ver = 1;
bool dirty = false; uint32_t dirtyAt = 0;
uint16_t nodeId; uint32_t secret; char pinStr[5];
uint32_t timeBase = 0;
bool muted = false;
int screen = 0, viewIdx = 0;
uint32_t lastInput = 0, lastDraw = 0; bool dimmed = false;
uint8_t alarmBeeps = 0; uint32_t nextBeep = 0; uint16_t beepFreq = 3200;

// ---------------- zaman ----------------
uint32_t nowEpoch() { return timeBase ? timeBase + millis() / 1000 : 0; }
void setTime(uint32_t t, bool writeRtc) {
  if (t < 1750000000UL || t > 2200000000UL) return;
  timeBase = t - millis() / 1000;
  if (writeRtc && M5.Rtc.isEnabled()) { time_t tt = t; struct tm tmv; gmtime_r(&tt, &tmv); M5.Rtc.setDateTime(&tmv); }
}
uint32_t civilToEpoch(int y, int m, int d, int hh, int mm, int ss) {
  y -= m <= 2; int era = (y >= 0 ? y : y - 399) / 400; unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097L + (long)doe - 719468L;
  return (uint32_t)(days * 86400L + hh * 3600L + mm * 60L + ss);
}
String fmtLocal(uint32_t ts, bool withDate) {
  if (!ts) return "?";
  time_t t = ts + TZ_OFS; struct tm tmv; gmtime_r(&t, &tmv); char b[24];
  strftime(b, sizeof(b), withDate ? "%d.%m.%Y %H:%M" : "%H:%M", &tmv); return String(b);
}

// ---------------- metin yardimcilari ----------------
void copyField(char *dst, size_t cap, String s) {
  for (size_t i = 0; i < s.length(); i++) if ((uint8_t)s[i] < 0x20 || s[i] == 0x7f) s.setCharAt(i, ' ');
  s.trim();
  size_t n = s.length(); if (n > cap - 1) n = cap - 1;
  while (n > 0 && n < s.length() && ((uint8_t)s[n] & 0xC0) == 0x80) n--;   // UTF-8 karakteri ortadan kesme
  memcpy(dst, s.c_str(), n); dst[n] = 0;
}
void copyStr(char *dst, size_t cap, const char *src) { copyField(dst, cap, String(src)); }
String js(const char *s) {
  String o = "\""; o.reserve(strlen(s) + 8);
  for (; *s; s++) { uint8_t c = *s; if (c == '"' || c == '\\') { o += '\\'; o += (char)c; } else if (c < 0x20) o += ' '; else o += (char)c; }
  o += '"'; return o;
}
String csvEsc(const char *s) { String o = "\""; for (; *s; s++) { if (*s == '"') o += '"'; o += *s; } o += '"'; return o; }
String urlEnc(const String &s) {
  String o; o.reserve(s.length() * 2); const char *hx = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) { uint8_t c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c; else { o += '%'; o += hx[c >> 4]; o += hx[c & 15]; } }
  return o;
}
String hex8(uint32_t v) { char b[9]; snprintf(b, sizeof(b), "%08lx", (unsigned long)v); return String(b); }
String bitList(uint16_t mask, const char **names) { String o; for (int b = 0; b < 13; b++) if (mask >> b & 1) { if (o.length()) o += ", "; o += names[b]; } return o; }
String ascii(const char *s) {   // ekran fontu icin Turkce -> ASCII
  String o;
  while (*s) {
    uint8_t c = *s;
    if (c < 0x80) { o += (char)c; s++; continue; }
    uint8_t d = (uint8_t)s[1]; char r = '?';
    if (c == 0xC3) { switch (d) { case 0xA7: r='c'; break; case 0x87: r='C'; break; case 0xB6: r='o'; break; case 0x96: r='O'; break; case 0xBC: r='u'; break; case 0x9C: r='U'; break; case 0xA2: r='a'; break; case 0xAE: r='i'; break; case 0xBB: r='u'; break; } }
    else if (c == 0xC4) { switch (d) { case 0x9F: r='g'; break; case 0x9E: r='G'; break; case 0xB1: r='i'; break; case 0xB0: r='I'; break; } }
    else if (c == 0xC5) { switch (d) { case 0x9F: r='s'; break; case 0x9E: r='S'; break; } }
    o += r;
    s++; while (((uint8_t)*s & 0xC0) == 0x80) s++;
  }
  return o;
}
uint32_t keyFor(uint32_t id) { uint32_t h = 2166136261UL ^ secret; for (int i = 0; i < 4; i++) { h ^= (id >> (i * 8)) & 0xFF; h *= 16777619UL; } h ^= secret >> 7; h *= 16777619UL; return h; }

// ---------------- ayarlar ----------------
void cfgDefaults() {
  memset(&cfg, 0, sizeof(cfg)); cfg.magic = CFG_MAGIC;
  strcpy(cfg.ssid, "0000-DEPREM-ACIL-AG"); cfg.ch = 6; cfg.txp = 5; cfg.maxcli = 10;
  cfg.meshEn = 1; cfg.ttl = 4; cfg.syncSec = 3;
  cfg.bright = 110; cfg.dimSec = 45; cfg.vol = 200; cfg.beepLvl = 3;
  cfg.rateSec = 5; cfg.quakeEn = 1; cfg.quakeThr = 12; cfg.lowBat = 15;
}
void cfgFix() {
  cfg.ssid[32] = 0; cfg.place[40] = 0; cfg.notice[200] = 0; cfg.upSsid[32] = 0; cfg.upPass[64] = 0; cfg.tgToken[63] = 0; cfg.tgChat[23] = 0; cfg.hookUrl[159] = 0; cfg.netKey[32] = 0; cfg.lowBat = min<uint8_t>(cfg.lowBat, 50);
  if (!cfg.ssid[0]) strcpy(cfg.ssid, "0000-DEPREM-ACIL-AG");
  cfg.ch = constrain(cfg.ch, 1, 13); cfg.txp = min<uint8_t>(cfg.txp, 5); cfg.maxcli = constrain(cfg.maxcli, 1, 10);
  cfg.ttl = constrain(cfg.ttl, 1, 10); cfg.syncSec = min<uint8_t>(cfg.syncSec, 60);
  cfg.bright = max<uint8_t>(cfg.bright, 10); cfg.dimSec = min<uint16_t>(cfg.dimSec, 3600); cfg.beepLvl = min<uint8_t>(cfg.beepLvl, 3);
  cfg.hideH = min<uint16_t>(cfg.hideH, 720); cfg.quakeThr = constrain(cfg.quakeThr, 3, 100); cfg.upMode = min<uint8_t>(cfg.upMode, 3);
  cfg.meshEn = !!cfg.meshEn; cfg.reqName = !!cfg.reqName; cfg.reqLoc = !!cfg.reqLoc; cfg.psave = !!cfg.psave; cfg.quakeEn = !!cfg.quakeEn; cfg.quakePost = !!cfg.quakePost;
}
void loadCfg() {
  cfgDefaults();
  size_t len = prefs.getBytesLength("cfg");          // eski (daha kisa) ayar blogu da okunur, yeni alanlar varsayilan kalir
  if (len >= 2 && len <= sizeof(cfg)) { prefs.getBytes("cfg", &cfg, len); if (cfg.magic != CFG_MAGIC) cfgDefaults(); }
  cfgFix();
}
void saveCfg() { prefs.putBytes("cfg", &cfg, sizeof(cfg)); }
bool upEnabled() { return cfg.upMode && cfg.upSsid[0]; }

// ---------------- resmi mesaj imzasi ----------------
uint32_t sigOf(const Msg &m) {
  if (!cfg.netKey[0]) return 0;
  uint8_t buf[260]; size_t n = 0;
  memcpy(buf + n, &m.id, 4); n += 4; memcpy(buf + n, &m.ts, 4); n += 4; memcpy(buf + n, &m.parent, 4); n += 4; buf[n++] = m.status;
  size_t a = strlen(m.name); memcpy(buf + n, m.name, a + 1); n += a + 1;
  a = strlen(m.loc); memcpy(buf + n, m.loc, a + 1); n += a + 1;
  a = strlen(m.text); memcpy(buf + n, m.text, a + 1); n += a + 1;
  uint8_t out[32];
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), (const uint8_t *)cfg.netKey, strlen(cfg.netKey), buf, n, out);
  uint32_t r; memcpy(&r, out, 4); return r | 1;
}
// Gelen mesajda gecersiz resmi isaretini kaldir (anahtar tanimliysa)
void verifyOfficial(Msg &m) { if ((m.flags & F_OFFICIAL) && cfg.netKey[0] && m.sig != sigOf(m)) m.flags &= ~F_OFFICIAL; }

// ---------------- mesaj deposu ----------------
int findMsg(uint32_t id) { for (int i = 0; i < nMsgs; i++) if (msgs[i].id == id) return i; return -1; }
void markDirty() { ver++; dirty = true; dirtyAt = millis(); }
void evictOne() {
  int v = -1;
  for (int i = 0; i < nMsgs && v < 0; i++) if (msgs[i].flags & F_DELETED) v = i;
  for (int i = 0; i < nMsgs && v < 0; i++) if ((msgs[i].flags & F_RESOLVED) && msgs[i].status != ST_STOCK) v = i;
  for (int i = 0; i < nMsgs && v < 0; i++) if (msgs[i].status == ST_REPLY || msgs[i].status == ST_SYS || msgs[i].status == ST_NODE) v = i;
  for (int i = 0; i < nMsgs && v < 0; i++) if ((msgs[i].status == ST_SAFE || msgs[i].status == ST_ANN) && !(msgs[i].flags & F_OFFICIAL)) v = i;
  for (int u = 1; u <= 3 && v < 0; u++) for (int i = 0; i < nMsgs && v < 0; i++) if (msgs[i].urg <= u) v = i;
  if (v < 0) v = 0;
  memmove(&msgs[v], &msgs[v + 1], sizeof(Msg) * (nMsgs - v - 1)); nMsgs--;
}
void sanitize(Msg &m) {
  m.name[sizeof(m.name) - 1] = 0; m.loc[sizeof(m.loc) - 1] = 0; m.phone[sizeof(m.phone) - 1] = 0; m.text[sizeof(m.text) - 1] = 0;
  if (m.status >= ST_COUNT) m.status = ST_ANN;
  m.urg = constrain(m.urg, 1, 3); m.needs &= BIT_MASK; m.skills &= BIT_MASK;
  m.task = min<uint8_t>(m.task, 3); m.triage = min<uint8_t>(m.triage, 4); if ((m.med & 15) > 8) m.med &= 0xF0; m.dmg &= 15;
}
bool isTaskable(const Msg &m) { return (m.status >= ST_HELP && m.status <= ST_TRAP) || m.status == ST_SEARCH || m.status == ST_BLD; }
bool isHelp(const Msg &m) { return m.status >= ST_HELP && m.status <= ST_TRAP; }
bool addMsg(Msg m, bool alert) {
  sanitize(m);
  if (m.id == 0 || findMsg(m.id) >= 0) return false;
  if (nMsgs >= MAX_MSGS) evictOne();
  msgs[nMsgs++] = m; markDirty();
  if (alert && isHelp(m) && !(m.flags & 3) && cfg.beepLvl && m.urg >= cfg.beepLvl) { alarmBeeps = 3; beepFreq = 3200; nextBeep = millis(); lastInput = millis(); }
  return true;
}
void saveStore() {
  File f = LittleFS.open("/m.tmp", "w"); if (!f) return;
  uint32_t hdr[2] = { STORE_V3, (uint32_t)nMsgs };
  f.write((uint8_t *)hdr, 8); f.write((uint8_t *)msgs, sizeof(Msg) * nMsgs); f.close();
  LittleFS.remove("/m.bin"); LittleFS.rename("/m.tmp", "/m.bin");
}
// Dosyadan oku (v1 veya v2). merge=false: depoyu degistir, merge=true: eksikleri ekle. Eklenen sayisi, gecersizse -1
int readStore(const char *path, bool merge) {
  File f = LittleFS.open(path, "r"); if (!f) return -1;
  uint32_t hdr[2];
  if (f.read((uint8_t *)hdr, 8) != 8 || hdr[1] > 1000) { f.close(); return -1; }
  size_t rs = hdr[0] == STORE_V3 ? sizeof(Msg) : hdr[0] == STORE_V2 ? sizeof(MsgV2) : hdr[0] == STORE_V1 ? sizeof(MsgV1) : 0;
  if (!rs || f.size() != 8 + hdr[1] * rs) { f.close(); return -1; }
  if (!merge) nMsgs = 0;
  int added = 0;
  for (uint32_t k = 0; k < hdr[1]; k++) {
    Msg m; memset(&m, 0, sizeof(m));
    if (hdr[0] == STORE_V3) f.read((uint8_t *)&m, sizeof(m));
    else if (hdr[0] == STORE_V2) {
      MsgV2 o; f.read((uint8_t *)&o, sizeof(o));
      o.name[29] = 0; o.loc[47] = 0; o.phone[13] = 0; o.text[131] = 0;
      m.id = o.id; m.ts = o.ts; m.parent = o.parent; m.status = o.status; m.urg = o.urg; m.people = o.people; m.flags = o.flags; m.needs = o.needs; m.skills = o.skills;
      copyStr(m.name, sizeof(m.name), o.name); copyStr(m.loc, sizeof(m.loc), o.loc); copyStr(m.phone, sizeof(m.phone), o.phone); copyStr(m.text, sizeof(m.text), o.text);
      if (m.flags & F_RESOLVED && isTaskable(m)) m.task = 3;
      m.sig = sigOf(m);
    } else {
      MsgV1 o; f.read((uint8_t *)&o, sizeof(o));
      o.name[19] = 0; o.loc[43] = 0; o.phone[13] = 0; o.text[151] = 0;
      m.id = o.id; m.ts = o.ts; m.status = o.status == 4 ? ST_ANN : min<uint8_t>(o.status, 4); m.urg = o.urg; m.people = o.people; m.flags = o.flags & 3;
      copyStr(m.name, sizeof(m.name), o.name); copyStr(m.loc, sizeof(m.loc), o.loc); copyStr(m.phone, sizeof(m.phone), o.phone); copyStr(m.text, sizeof(m.text), o.text);
    }
    if (merge) { m.flags &= F_SHARED; if (addMsg(m, false)) added++; }
    else if (nMsgs < MAX_MSGS && m.id && findMsg(m.id) < 0) { sanitize(m); msgs[nMsgs++] = m; added++; }
  }
  f.close();
  if (merge && added) markDirty();
  return added;
}
int countUrgent() { int c = 0; for (int i = 0; i < nMsgs; i++) if (msgs[i].urg == 3 && isHelp(msgs[i]) && !(msgs[i].flags & 3)) c++; return c; }
int countVisible() { int c = 0; for (int i = 0; i < nMsgs; i++) if (!(msgs[i].flags & F_DELETED) && msgs[i].status != ST_REPLY && msgs[i].status != ST_STOCK) c++; return c; }
int countUnassigned() { int c = 0; for (int i = 0; i < nMsgs; i++) { const Msg &m = msgs[i]; if (!(m.flags & 3) && isTaskable(m) && m.task == 0) c++; } return c; }

// ---------------- ESP-NOW mesh ----------------
struct RxItem { uint8_t len; uint8_t data[250]; uint8_t mac[6]; };
QueueHandle_t rxQ;
const uint8_t BCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
struct TxJob { uint32_t id; uint8_t ttl, left; uint32_t at; } txJobs[16];

uint32_t lastHello = 0, lastSync = 0, lastGwSeen = 0; int syncIdx = 0;
bool meshUp = false;
int upState = 0;   // 0 kapali, 1 baglaniyor, 2 cevrimici

void onRx(const uint8_t *mac, const uint8_t *data, int len) {
  if (len < 4 || len > 250 || data[0] != 'D' || data[1] != 'C') return;
  RxItem it; it.len = len; memcpy(it.data, data, len); memcpy(it.mac, mac, 6);
  xQueueSend(rxQ, &it, 0);
}
void sendMsgPkt(const Msg &m, uint8_t ttl) {
  if (!meshUp || !cfg.meshEn) return;
  PktMsg p; p.m0 = 'D'; p.m1 = 'C'; p.type = 1; p.ttl = ttl; p.m = m; p.m.flags &= F_SHARED;
  esp_now_send(BCAST, (uint8_t *)&p, sizeof(p));
}
void queueTx(uint32_t id, uint8_t ttl, uint8_t times) {
  for (auto &j : txJobs) if (j.left && j.id == id) { j.ttl = max(j.ttl, ttl); j.left = times; return; }
  for (auto &j : txJobs) if (!j.left) { j.id = id; j.ttl = ttl; j.left = times; j.at = millis() + random(20, 250); return; }
}
int activePeers() { int c = 0; for (auto &p : peers) if (p.seen && millis() - p.seen < 90000UL) c++; return c; }
int countUrgent();
int countUnassigned();
Peer *touchPeer(const uint8_t *mac) {
  Peer *slot = nullptr, *old = &peers[0];
  for (auto &p : peers) { if (p.seen && !memcmp(p.mac, mac, 6)) { p.seen = millis() | 1; return &p; } if (!slot && !p.seen) slot = &p; if (p.seen < old->seen) old = &p; }
  if (!slot) slot = old;                                  // en eski kaydin yerine
  memset(slot, 0, sizeof(Peer)); slot->bat = 255; memcpy(slot->mac, mac, 6); slot->seen = millis() | 1; return slot;
}
void meshLoop() {
  RxItem it;
  while (xQueueReceive(rxQ, &it, 0) == pdTRUE) {
    if (!cfg.meshEn) continue;
    Peer *pe = touchPeer(it.mac);
    if (it.data[2] == 1 && it.len == sizeof(PktMsg)) {
      PktMsg *p = (PktMsg *)it.data; p->m.flags &= F_SHARED; sanitize(p->m); verifyOfficial(p->m);
      int i = findMsg(p->m.id);
      bool changed = false;
      if (i < 0) { changed = addMsg(p->m, true); }
      else {
        Msg &L = msgs[i];
        if ((int8_t)(p->m.rev - L.rev) > 0) {             // daha yeni surum: icerigi al, isaretleri birlestir
          uint8_t keep = (L.flags & ~F_OFFICIAL) | p->m.flags;
          L = p->m; L.flags = keep; changed = true;
        } else if ((L.flags | p->m.flags) != L.flags) { L.flags |= p->m.flags & ~F_OFFICIAL; changed = true; }
        if (p->m.task > L.task && p->m.rev == L.rev) { L.task = p->m.task; changed = true; }
        if (changed) markDirty();
      }
      if (changed && p->ttl > 1) queueTx(p->m.id, p->ttl - 1, 2);
    } else if (it.data[2] == 2 && it.len == sizeof(PktHello)) {
      PktHello *h = (PktHello *)it.data;
      if (!timeBase && h->now) setTime(h->now, true);
      if (h->gw) lastGwSeen = millis() | 1;
      pe->bat = h->bat; pe->cli = h->cli; pe->chg = h->chg; pe->gw = h->gw; pe->urg = h->urg; pe->open = h->open; pe->count = h->count;
      h->place[29] = 0; strcpy(pe->place, h->place);
    }
  }
  if (!cfg.meshEn) return;
  uint32_t t = millis();
  for (auto &j : txJobs) if (j.left && (int32_t)(t - j.at) >= 0) {
    int i = findMsg(j.id); if (i < 0) { j.left = 0; continue; }
    sendMsgPkt(msgs[i], j.ttl); j.left--; j.at = t + random(150, 600);
  }
  if (meshUp && t - lastHello > 15000UL) {
    lastHello = t; PktHello h; memset(&h, 0, sizeof(h));
    h.m0 = 'D'; h.m1 = 'C'; h.type = 2; h.ttl = 1; h.now = nowEpoch(); h.count = (uint16_t)nMsgs; h.gw = upState == 2;
    int b = M5.Power.getBatteryLevel(); h.bat = b < 0 ? 255 : b; h.cli = WiFi.softAPgetStationNum(); h.chg = M5.Power.isCharging() == m5::Power_Class::is_charging;
    h.urg = (uint8_t)min(255, countUrgent()); h.open = (uint8_t)min(255, countUnassigned());
    copyStr(h.place, sizeof(h.place), cfg.place);
    esp_now_send(BCAST, (uint8_t *)&h, sizeof(h));
  }
  if (cfg.syncSec && nMsgs > 0 && t - lastSync > cfg.syncSec * 1000UL) {   // yavas es-zamanlama
    lastSync = t; if (syncIdx >= nMsgs) syncIdx = 0; sendMsgPkt(msgs[syncIdx++], 1);
  }
}
bool gwOnline() { return upState == 2 || (lastGwSeen && millis() - lastGwSeen < 60000UL); }

// ---------------- sarsinti algilama (IMU) ----------------
float gX = 0, gY = 0, gZ = 0; bool gInit = false;
uint32_t lastImu = 0, qWinStart = 0, qLastTrig = 0; int qCnt = 0, qN = 0, qHits = 0; float qWinPeak = 0;
uint32_t quakeTs = 0, quakeShowUntil = 0; float quakePeak = 0;
uint32_t newId() { uint32_t id; do { id = ((uint32_t)nodeId << 16) | (esp_random() & 0xFFFF); } while (findMsg(id) >= 0 || id == 0 || (id & 0xFFFF) == 0xFFFE); return id; }
void quakeTrigger(float peak) {
  uint32_t t = millis();
  qLastTrig = t; quakeTs = nowEpoch(); quakePeak = peak; quakeShowUntil = t + 60000UL;
  alarmBeeps = 10; beepFreq = 2200; nextBeep = t; lastInput = t; lastDraw = 0;
  if (cfg.quakePost) {
    Msg m; memset(&m, 0, sizeof(m));
    m.status = ST_SYS; m.urg = 2; m.ts = quakeTs; m.people = (uint8_t)min(255, (int)(peak * 100 + 0.5f));
    if (cfg.place[0]) copyStr(m.name, sizeof(m.name), cfg.place); else snprintf(m.name, sizeof(m.name), "Cihaz %04X", nodeId);
    m.id = newId();
    addMsg(m, false); queueTx(m.id, cfg.ttl, 3);
  }
}

// ---------------- dusuk pil uyarisi ----------------
uint32_t lastBatChk = 0; bool lowSent = false;
void batLoop() {
  uint32_t t = millis();
  if (!cfg.lowBat || t < 60000UL || t - lastBatChk < 30000UL) return;
  lastBatChk = t;
  int b = M5.Power.getBatteryLevel(); bool chg = M5.Power.isCharging() == m5::Power_Class::is_charging;
  if (b < 0) return;
  if (b > cfg.lowBat + 10 || chg) { lowSent = false; return; }
  if (b < cfg.lowBat && !lowSent) {
    lowSent = true;
    Msg m; memset(&m, 0, sizeof(m));
    m.status = ST_NODE; m.urg = 2; m.ts = nowEpoch(); m.people = b; m.id = newId();
    if (cfg.place[0]) copyStr(m.name, sizeof(m.name), cfg.place); else snprintf(m.name, sizeof(m.name), "Cihaz %04X", nodeId);
    addMsg(m, false); queueTx(m.id, cfg.ttl, 3);
    alarmBeeps = 2; beepFreq = 1500; nextBeep = t;
  }
}
void quakeLoop() {
  if (!cfg.quakeEn || !M5.Imu.isEnabled()) return;
  uint32_t t = millis();
  if (t - lastImu < 10) return;
  lastImu = t;
  float ax, ay, az;
  if (!M5.Imu.getAccel(&ax, &ay, &az)) return;
  if (!gInit) { gX = ax; gY = ay; gZ = az; gInit = true; qWinStart = t; return; }
  gX += 0.02f * (ax - gX); gY += 0.02f * (ay - gY); gZ += 0.02f * (az - gZ);
  float dx = ax - gX, dy = ay - gY, dz = az - gZ, d = sqrtf(dx * dx + dy * dy + dz * dz);
  if (t < 15000UL || M5.BtnA.isPressed() || M5.BtnB.isPressed()) { qCnt = qN = qHits = 0; qWinPeak = 0; qWinStart = t; return; }
  qN++; if (d > cfg.quakeThr / 100.0f) qCnt++; if (d > qWinPeak) qWinPeak = d;
  if (t - qWinStart >= 1000UL) {
    bool hit = qN >= 20 && qCnt * 4 >= qN;          // 1 sn icinde orneklerin %25'i esik ustunde
    qHits = hit ? qHits + 1 : 0;
    if (qHits >= 2 && t - qLastTrig > 120000UL) { quakeTrigger(qWinPeak); qHits = 0; }   // 2 sn ust uste
    qWinStart = t; qCnt = qN = 0; qWinPeak = 0;
  }
}

// ---------------- internet koprusu ----------------
struct UpJob { uint8_t kind; Msg m; };            // 0 yeni, 1 cozuldu, 2 test
struct UpRes { uint32_t id; uint8_t kind; int code; };
QueueHandle_t upReqQ, upResQ;
bool upBusy = false; uint32_t upNext = 0, upLastTry = 0, ntpAt = 0; int upLastCode = 0; bool ntpDone = false;

String placeLabel() { if (cfg.place[0]) return String(cfg.place); char b[16]; snprintf(b, sizeof(b), "Cihaz %04X", nodeId); return String(b); }
String fmtMsg(const Msg &m, uint8_t kind) {
  String s;
  if (kind == 2) { s = "✅ Deprem Acil Ağ köprü testi — "; s += placeLabel(); return s; }
  if (m.status == ST_STOCK) { s = "🏪 Dağıtım noktası stoğu: "; s += m.name; if (m.loc[0]) { s += " — "; s += m.loc; } s += "\n"; s += m.text; return s; }
  if (kind == 1) { s = "✔ ÇÖZÜLDÜ: "; s += ST_TR[m.status]; s += " — "; s += m.name; s += " (#"; s += hex8(m.id); s += ")"; return s; }
  if (isHelp(m) || m.status == ST_SEARCH) s += m.urg == 3 ? "🔴 ÇOK ACİL " : m.urg == 2 ? "🟠 " : "";
  s += ST_ICON[m.status]; s += " "; s += ST_TR[m.status];
  if (m.flags & F_OFFICIAL) s += " [RESMİ]";
  if (m.name[0]) { s += ": "; s += m.name; }
  if (m.people && m.status != ST_SYS) { s += " ("; s += m.people; s += " kişi)"; }
  if (m.status == ST_SYS) { s += "\nTepe ivme: "; s += String(m.people / 100.0f, 2); s += " g"; }
  if (m.status == ST_NODE) { s += "\nPil azaldı: %"; s += m.people; s += " — bu nokta kapanmak üzere"; }
  if (m.status == ST_BLD) { s += "\n"; s += DMG_TR[m.dmg & 3]; if (m.dmg & 4) s += " · GİRMEK YASAK"; if (m.dmg & 8) s += " · İÇERİDEN SES GELİYOR"; }
  if (m.triage) { s += "\nTriyaj: "; s += TRI_TR[m.triage]; }
  if (m.med & 15) { s += "\nKan grubu: "; s += BLOOD[m.med & 15]; }
  if (m.med >> 4) { s += "\nKritik: "; String x; for (int b = 0; b < 4; b++) if (m.med >> (4 + b) & 1) { if (x.length()) x += ", "; x += MED_TR[b]; } s += x; }
  if (m.status == ST_REPLY) { int p = findMsg(m.parent); s += "\n↪ "; s += p >= 0 ? String(msgs[p].name) : hex8(m.parent); }
  if (m.loc[0]) { s += "\n📍 "; s += m.loc; }
  if (m.phone[0]) { s += "\n📞 "; s += m.phone; }
  if (m.needs) { s += "\nİhtiyaç: "; s += bitList(m.needs, NEED_TR); }
  if (m.skills) { s += "\nBeceri: "; s += bitList(m.skills, SKILL_TR); }
  if (m.text[0]) { s += "\n"; s += m.text; }
  s += "\n— "; s += placeLabel(); s += " · "; s += fmtLocal(m.ts, true); s += " · #"; s += hex8(m.id);
  return s;
}
String msgJson(const Msg &m, uint8_t kind) {
  String o = "{\"kind\":\""; o += kind == 0 ? "new" : kind == 1 ? "resolved" : "test";
  o += "\",\"id\":\""; o += hex8(m.id); o += "\",\"ts\":"; o += m.ts; o += ",\"status\":"; o += m.status;
  o += ",\"statusName\":"; o += js(ST_TR[m.status < ST_COUNT ? m.status : 4]); o += ",\"urg\":"; o += m.urg; o += ",\"people\":"; o += m.people;
  o += ",\"task\":"; o += m.task; o += ",\"team\":"; o += m.team; o += ",\"triage\":"; o += m.triage; o += ",\"blood\":"; o += js(BLOOD[m.med & 15]);
  o += ",\"med\":"; o += m.med >> 4; o += ",\"damage\":"; o += m.dmg;
  o += ",\"official\":"; o += (m.flags & F_OFFICIAL) ? "true" : "false"; o += ",\"resolved\":"; o += (m.flags & F_RESOLVED) ? "true" : "false";
  o += ",\"needs\":"; o += js(bitList(m.needs, NEED_TR).c_str()); o += ",\"skills\":"; o += js(bitList(m.skills, SKILL_TR).c_str());
  o += ",\"parent\":\""; o += m.parent ? hex8(m.parent) : String(""); o += "\",\"name\":"; o += js(m.name); o += ",\"loc\":"; o += js(m.loc);
  o += ",\"phone\":"; o += js(m.phone); o += ",\"text\":"; o += js(m.text); o += ",\"place\":"; o += js(placeLabel().c_str());
  char n[6]; snprintf(n, sizeof(n), "%04X", nodeId); o += ",\"node\":\""; o += n; o += "\"}";
  return o;
}
int httpPost(const String &url, const char *ctype, const String &body) {
  WiFiClientSecure sc; WiFiClient pc; HTTPClient h; bool ok;
  if (url.startsWith("https")) { sc.setInsecure(); ok = h.begin(sc, url); } else ok = h.begin(pc, url);
  if (!ok) return -100;
  h.setTimeout(10000); h.addHeader("Content-Type", ctype);
  int c = h.POST(body); h.end(); return c;
}
void upTask(void *) {
  static UpJob j;
  for (;;) {
    if (xQueueReceive(upReqQ, &j, portMAX_DELAY) != pdTRUE) continue;
    int code = 200; bool any = false;
    if ((cfg.upMode & 1) && cfg.tgToken[0] && cfg.tgChat[0]) {
      any = true;
      String url = "https://api.telegram.org/bot"; url += cfg.tgToken; url += "/sendMessage";
      String body = "chat_id="; body += urlEnc(cfg.tgChat); body += "&text="; body += urlEnc(fmtMsg(j.m, j.kind));
      int c = httpPost(url, "application/x-www-form-urlencoded", body); if (c < 200 || c >= 300) code = c;
    }
    if ((cfg.upMode & 2) && cfg.hookUrl[0]) {
      any = true;
      int c = httpPost(String(cfg.hookUrl), "application/json", msgJson(j.m, j.kind)); if (c < 200 || c >= 300) code = c;
    }
    if (!any) code = -1;
    UpRes r = { j.m.id, j.kind, code };
    xQueueSend(upResQ, &r, portMAX_DELAY);
  }
}
int pendingUploads() {
  int c = 0;
  for (int i = 0; i < nMsgs; i++) { const Msg &m = msgs[i]; if (m.flags & F_DELETED) continue;
    if (!(m.flags & F_UPLOADED)) c++; else if ((m.flags & F_RESOLVED) && (m.flags & L_UPSENT) && !(m.flags & L_RESSENT)) c++; }
  return c;
}
void upLoop() {
  if (!upEnabled()) { upState = 0; return; }
  uint32_t t = millis();
  if (WiFi.status() != WL_CONNECTED) {
    upState = 1;
    if (!upLastTry || t - upLastTry > 90000UL) {       // kanal ipucu ile baglan: AP kanali korunur
      upLastTry = t | 1; WiFi.begin(cfg.upSsid, cfg.upPass[0] ? cfg.upPass : nullptr, cfg.ch);
    }
    return;
  }
  upState = 2;
  if (!ntpDone) {
    if (!ntpAt) { configTime(0, 0, "pool.ntp.org", "time.google.com"); ntpAt = t | 1; }
    time_t n = time(nullptr); if (n > 1750000000) { setTime((uint32_t)n, true); ntpDone = true; }
  }
  UpRes r;
  while (xQueueReceive(upResQ, &r, 0) == pdTRUE) {
    upBusy = false; upLastCode = r.code;
    int i = findMsg(r.id);
    if (r.code == 200) {
      if (i >= 0 && r.kind == 0) { msgs[i].flags |= F_UPLOADED | L_UPSENT; markDirty(); queueTx(r.id, cfg.ttl, 1); }
      else if (i >= 0 && r.kind == 1) { msgs[i].flags |= L_RESSENT; markDirty(); }
      upNext = t + 1500;
    } else upNext = t + 30000UL;
  }
  if (upBusy || (int32_t)(t - upNext) < 0) return;
  for (int i = 0; i < nMsgs; i++) {
    const Msg &m = msgs[i]; if (m.flags & F_DELETED) continue;
    int kind = -1;
    if (!(m.flags & F_UPLOADED)) kind = 0;
    else if ((m.flags & F_RESOLVED) && (m.flags & L_UPSENT) && !(m.flags & L_RESSENT)) kind = 1;
    if (kind < 0) continue;
    static UpJob j; j.kind = kind; j.m = m;
    if (xQueueSend(upReqQ, &j, 0) == pdTRUE) upBusy = true;
    break;
  }
}

// ---------------- web ----------------
struct Rate { uint32_t ip, at; } rates[24];
bool rateOk(uint32_t ip) {
  if (!cfg.rateSec) return true;
  uint32_t t = millis(); Rate *slot = &rates[0];
  for (auto &r : rates) { if (r.ip == ip) { if (t - r.at < cfg.rateSec * 1000UL) return false; r.at = t; return true; } if (!r.ip || t - r.at > t - slot->at) slot = &r; }
  slot->ip = ip; slot->at = t; return true;
}
bool isAdmin() { return server.arg("pin") == pinStr; }
void noCache() { server.sendHeader("Cache-Control", "no-store"); }
void sendJson(int code, const String &s) { noCache(); server.send(code, "application/json; charset=utf-8", s); }
void denied() { delay(400); sendJson(403, "{\"ok\":0,\"err\":\"auth\"}"); }
void redirectHome() { server.sendHeader("Location", "http://192.168.4.1/", true); noCache(); server.send(302, "text/plain", ""); }
void hRoot() {
  if (server.hostHeader() != "192.168.4.1") { redirectHome(); return; }
  noCache(); server.sendHeader("Content-Encoding", "gzip");
  server.send_P(200, "text/html; charset=utf-8", (const char *)PAGE_GZ, PAGE_GZ_LEN);
}
void hStat() {
  char n[6]; snprintf(n, sizeof(n), "%04X", nodeId);
  String o; o.reserve(700);
  o = "{\"ver\":"; o += ver; o += ",\"n\":"; o += countVisible(); o += ",\"urg\":"; o += countUrgent();
  o += ",\"cli\":"; o += WiFi.softAPgetStationNum(); o += ",\"max\":"; o += cfg.maxcli; o += ",\"nodes\":"; o += activePeers();
  o += ",\"now\":"; o += nowEpoch(); o += ",\"bat\":"; o += (int)M5.Power.getBatteryLevel(); o += ",\"node\":\""; o += n;
  o += "\",\"fw\":\"" FW_VER "\",\"place\":"; o += js(cfg.place); o += ",\"notice\":"; o += js(cfg.notice);
  o += ",\"gw\":"; o += gwOnline() ? 1 : 0; o += ",\"up\":"; o += upState; o += ",\"upc\":"; o += upLastCode; o += ",\"pend\":"; o += upEnabled() ? pendingUploads() : 0;
  o += ",\"ch\":"; o += WiFi.channel(); o += ",\"cfgch\":"; o += cfg.ch; o += ",\"mesh\":"; o += cfg.meshEn;
  o += ",\"una\":"; o += countUnassigned(); o += ",\"key\":"; o += cfg.netKey[0] ? 1 : 0;
  o += ",\"qts\":"; o += quakeTs; o += ",\"qpk\":"; o += (int)(quakePeak * 100); o += ",\"imu\":"; o += M5.Imu.isEnabled() ? 1 : 0; o += "}";
  sendJson(200, o);
}
void hList() {
  noCache(); server.setContentLength(CONTENT_LENGTH_UNKNOWN); server.send(200, "application/json; charset=utf-8", "");
  server.sendContent("{\"m\":[");
  bool first = true; String c; c.reserve(1600);
  uint32_t now = nowEpoch();
  for (int i = 0; i < nMsgs; i++) {
    const Msg &m = msgs[i]; if (m.flags & F_DELETED) continue;
    if (cfg.hideH && (m.flags & F_RESOLVED) && m.ts && now > m.ts + cfg.hideH * 3600UL) continue;
    if (m.status == ST_STOCK && !m.text[0]) continue;
    char h[220];
    snprintf(h, sizeof(h), "%s{\"id\":\"%08lx\",\"par\":\"%08lx\",\"ts\":%lu,\"st\":%d,\"u\":%d,\"p\":%d,\"f\":%d,\"nd\":%u,\"sk\":%u,\"tk\":%d,\"tm\":%d,\"tr\":%d,\"md\":%d,\"dm\":%d,\"n\":",
             first ? "" : ",", (unsigned long)m.id, (unsigned long)m.parent, (unsigned long)m.ts, m.status, m.urg, m.people, m.flags & F_SHARED, m.needs, m.skills,
             m.task, m.team, m.triage, m.med, m.dmg);
    first = false;
    c += h; c += js(m.name); c += ",\"l\":"; c += js(m.loc); c += ",\"ph\":"; c += js(m.phone); c += ",\"t\":"; c += js(m.text); c += "}";
    if (c.length() > 1200) { server.sendContent(c); c = ""; }
  }
  c += "]}"; server.sendContent(c); server.sendContent("");
}
void hSend() {
  bool official = isAdmin();
  if (!official && !rateOk((uint32_t)server.client().remoteIP())) { sendJson(429, "{\"ok\":0,\"err\":\"rate\"}"); return; }
  if (!timeBase) setTime(strtoul(server.arg("t").c_str(), nullptr, 10), true);
  Msg m; memset(&m, 0, sizeof(m));
  copyField(m.name, sizeof(m.name), server.arg("name"));
  copyField(m.loc, sizeof(m.loc), server.arg("loc"));
  copyField(m.phone, sizeof(m.phone), server.arg("phone"));
  copyField(m.text, sizeof(m.text), server.arg("text"));
  m.status = constrain(server.arg("status").toInt(), 0, ST_BLD);
  if (m.status == ST_SYS) m.status = ST_ANN;
  m.med = server.arg("med").toInt() & 0xFF; m.dmg = server.arg("dmg").toInt() & 15;
  m.people = constrain(server.arg("people").toInt(), 0, 99);
  m.needs = server.arg("needs").toInt() & BIT_MASK;
  m.skills = server.arg("skills").toInt() & BIT_MASK;
  if (m.status == ST_REPLY) {
    m.parent = strtoul(server.arg("parent").c_str(), nullptr, 16);
    if (!m.parent || findMsg(m.parent) < 0) { sendJson(404, "{\"ok\":0,\"err\":\"parent\"}"); return; }
    if (!m.text[0]) { sendJson(400, "{\"ok\":0,\"err\":\"empty\"}"); return; }
    m.urg = 1; m.people = 0;
  } else {
    if (!m.name[0] && !m.text[0]) { sendJson(400, "{\"ok\":0,\"err\":\"empty\"}"); return; }
    if (!official && cfg.reqName && !m.name[0] && m.status != ST_ANN) { sendJson(400, "{\"ok\":0,\"err\":\"name\"}"); return; }
    if (!official && cfg.reqLoc && isHelp(m) && !m.loc[0]) { sendJson(400, "{\"ok\":0,\"err\":\"loc\"}"); return; }
    m.urg = (isHelp(m) || m.status == ST_SEARCH) ? constrain(server.arg("urg").toInt(), 1, 3) : 1;
    if (m.status == ST_BLD) m.urg = (m.dmg & 8) ? 3 : (m.dmg & 3) == 3 ? 2 : 1;
    if (!isHelp(m)) m.med = 0;
    if (m.status != ST_BLD) m.dmg = 0;
  }
  m.ts = nowEpoch();
  m.id = newId();
  if (official) { m.flags |= F_OFFICIAL; m.sig = sigOf(m); }
  addMsg(m, false); queueTx(m.id, cfg.ttl, 3);
  String o = "{\"ok\":1,\"id\":\""; o += hex8(m.id); o += "\",\"key\":\""; o += hex8(keyFor(m.id)); o += "\"}";
  sendJson(200, o);
}
long argInt(const char *k, long def) { return server.hasArg(k) ? server.arg(k).toInt() : def; }
void argStr(const char *k, char *dst, size_t cap) { if (server.hasArg(k)) copyField(dst, cap, server.arg(k)); }
String cfgJson() {
  String o; o.reserve(900);
  o = "{\"ok\":1,\"ssid\":"; o += js(cfg.ssid); o += ",\"ch\":"; o += cfg.ch; o += ",\"txp\":"; o += cfg.txp; o += ",\"maxcli\":"; o += cfg.maxcli;
  o += ",\"place\":"; o += js(cfg.place); o += ",\"notice\":"; o += js(cfg.notice);
  o += ",\"meshEn\":"; o += cfg.meshEn; o += ",\"ttl\":"; o += cfg.ttl; o += ",\"syncSec\":"; o += cfg.syncSec;
  o += ",\"bright\":"; o += cfg.bright; o += ",\"dimSec\":"; o += cfg.dimSec; o += ",\"vol\":"; o += cfg.vol; o += ",\"beepLvl\":"; o += cfg.beepLvl;
  o += ",\"reqName\":"; o += cfg.reqName; o += ",\"reqLoc\":"; o += cfg.reqLoc; o += ",\"rateSec\":"; o += cfg.rateSec; o += ",\"hideH\":"; o += cfg.hideH;
  o += ",\"psave\":"; o += cfg.psave; o += ",\"quakeEn\":"; o += cfg.quakeEn; o += ",\"quakeThr\":"; o += cfg.quakeThr; o += ",\"quakePost\":"; o += cfg.quakePost;
  o += ",\"upMode\":"; o += cfg.upMode; o += ",\"upSsid\":"; o += js(cfg.upSsid); o += ",\"upPass\":"; o += js(cfg.upPass);
  o += ",\"tgToken\":"; o += js(cfg.tgToken); o += ",\"tgChat\":"; o += js(cfg.tgChat); o += ",\"hookUrl\":"; o += js(cfg.hookUrl);
  o += ",\"netKey\":"; o += js(cfg.netKey); o += ",\"lowBat\":"; o += cfg.lowBat; o += "}";
  return o;
}
bool applyCfgArgs() {
  Cfg o = cfg;
  argStr("ssid", cfg.ssid, sizeof(cfg.ssid)); cfg.ch = argInt("ch", cfg.ch); cfg.txp = argInt("txp", cfg.txp); cfg.maxcli = argInt("maxcli", cfg.maxcli);
  argStr("place", cfg.place, sizeof(cfg.place)); argStr("notice", cfg.notice, sizeof(cfg.notice));
  cfg.meshEn = argInt("meshEn", cfg.meshEn); cfg.ttl = argInt("ttl", cfg.ttl); cfg.syncSec = argInt("syncSec", cfg.syncSec);
  cfg.bright = constrain(argInt("bright", cfg.bright), 10, 255); cfg.dimSec = constrain(argInt("dimSec", cfg.dimSec), 0, 3600);
  cfg.vol = constrain(argInt("vol", cfg.vol), 0, 255); cfg.beepLvl = argInt("beepLvl", cfg.beepLvl);
  cfg.reqName = argInt("reqName", cfg.reqName); cfg.reqLoc = argInt("reqLoc", cfg.reqLoc);
  cfg.rateSec = constrain(argInt("rateSec", cfg.rateSec), 0, 255); cfg.hideH = constrain(argInt("hideH", cfg.hideH), 0, 720);
  cfg.psave = argInt("psave", cfg.psave);
  cfg.quakeEn = argInt("quakeEn", cfg.quakeEn); cfg.quakeThr = constrain(argInt("quakeThr", cfg.quakeThr), 3, 100); cfg.quakePost = argInt("quakePost", cfg.quakePost);
  cfg.upMode = argInt("upMode", cfg.upMode); argStr("upSsid", cfg.upSsid, sizeof(cfg.upSsid));
  if (server.hasArg("upPass")) { String p = server.arg("upPass"); if (p.length() < sizeof(cfg.upPass)) strcpy(cfg.upPass, p.c_str()); }
  argStr("tgToken", cfg.tgToken, sizeof(cfg.tgToken)); argStr("tgChat", cfg.tgChat, sizeof(cfg.tgChat)); argStr("hookUrl", cfg.hookUrl, sizeof(cfg.hookUrl));
  argStr("netKey", cfg.netKey, sizeof(cfg.netKey)); cfg.lowBat = constrain(argInt("lowBat", cfg.lowBat), 0, 50);
  cfgFix();
  return strcmp(o.ssid, cfg.ssid) || o.ch != cfg.ch || o.txp != cfg.txp || o.maxcli != cfg.maxcli || o.psave != cfg.psave ||
         o.upMode != cfg.upMode || strcmp(o.upSsid, cfg.upSsid) || strcmp(o.upPass, cfg.upPass);
}
void restartNow() { if (dirty) saveStore(); delay(500); ESP.restart(); }
void hAdmin() {
  String act = server.arg("act");
  bool adm = isAdmin();
  if (act == "check") { if (!adm) delay(400); sendJson(200, adm ? "{\"ok\":1}" : "{\"ok\":0}"); return; }
  if (act == "resolve" || act == "delete") {
    uint32_t id = strtoul(server.arg("id").c_str(), nullptr, 16);
    int i = findMsg(id);
    if (i < 0) { sendJson(404, "{\"ok\":0,\"err\":\"nf\"}"); return; }
    bool owner = server.arg("key").length() == 8 && strtoul(server.arg("key").c_str(), nullptr, 16) == keyFor(id);
    if (act == "resolve" && (adm || owner)) msgs[i].flags |= F_RESOLVED;
    else if (act == "delete" && adm) msgs[i].flags |= F_DELETED;
    else { denied(); return; }
    markDirty(); queueTx(id, cfg.ttl, 3); sendJson(200, "{\"ok\":1}"); return;
  }
  if (!adm) { denied(); return; }
  if (act == "task" || act == "triage") {
    uint32_t id = strtoul(server.arg("id").c_str(), nullptr, 16);
    int i = findMsg(id); if (i < 0) { sendJson(404, "{\"ok\":0,\"err\":\"nf\"}"); return; }
    Msg &m = msgs[i];
    if (act == "task") {
      m.task = constrain(server.arg("task").toInt(), 0, 3);
      if (server.hasArg("team")) m.team = constrain(server.arg("team").toInt(), 0, 255);
      if (m.task == 3) m.flags |= F_RESOLVED;
    } else m.triage = constrain(server.arg("tr").toInt(), 0, 4);
    m.rev++; markDirty(); queueTx(id, cfg.ttl, 3); sendJson(200, "{\"ok\":1}"); return;
  }
  if (act == "stock") {                       // q: "0:200,3:40" (ihtiyac indeksi:adet)
    uint32_t id = ((uint32_t)nodeId << 16) | 0xFFFE;
    int i = findMsg(id);
    Msg m; memset(&m, 0, sizeof(m));
    if (i >= 0) m = msgs[i];
    m.id = id; m.status = ST_STOCK; m.urg = 1; m.ts = nowEpoch(); m.flags &= ~(F_UPLOADED | L_UPSENT | L_RESSENT);
    copyField(m.text, sizeof(m.text), server.arg("q"));
    if (cfg.place[0]) copyStr(m.name, sizeof(m.name), cfg.place); else snprintf(m.name, sizeof(m.name), "Cihaz %04X", nodeId);
    copyField(m.loc, sizeof(m.loc), server.arg("loc"));
    m.flags |= F_OFFICIAL; m.sig = sigOf(m); m.rev++;
    if (i >= 0) { msgs[i] = m; markDirty(); } else addMsg(m, false);
    queueTx(id, cfg.ttl, 3); sendJson(200, "{\"ok\":1}"); return;
  }
  if (act == "addr") {
    String a = server.arg("list"); if (a.length() > 1800) a = a.substring(0, 1800);
    prefs.putString("addr", a); sendJson(200, "{\"ok\":1}"); return;
  }
  if (act == "clear") {
    for (int i = 0; i < nMsgs; i++) if (!(msgs[i].flags & F_DELETED)) { msgs[i].flags |= F_DELETED; queueTx(msgs[i].id, cfg.ttl, 1); }
    markDirty(); sendJson(200, "{\"ok\":1}");
  } else if (act == "cfg_get") {
    sendJson(200, cfgJson());
  } else if (act == "cfg_set") {
    bool rb = applyCfgArgs(); saveCfg();
    M5.Speaker.setVolume(cfg.vol); if (!dimmed) M5.Display.setBrightness(cfg.bright); lastDraw = 0;
    sendJson(200, rb ? "{\"ok\":1,\"reboot\":1}" : "{\"ok\":1,\"reboot\":0}");
  } else if (act == "newpin") {
    String np = server.arg("np"); bool ok = np.length() == 4;
    for (size_t k = 0; k < np.length(); k++) if (!isdigit(np[k])) ok = false;
    if (!ok) { sendJson(400, "{\"ok\":0,\"err\":\"PIN 4 rakam olmali\"}"); return; }
    prefs.putString("pins", np); strcpy(pinStr, np.c_str()); lastDraw = 0;
    sendJson(200, "{\"ok\":1}");
  } else if (act == "reboot") {
    sendJson(200, "{\"ok\":1}"); restartNow();
  } else if (act == "factory") {
    sendJson(200, "{\"ok\":1}"); delay(300);
    prefs.clear(); LittleFS.remove("/m.bin"); delay(300); ESP.restart();
  } else if (act == "scan") {
    bool apOnly = WiFi.getMode() == WIFI_AP;
    if (apOnly) WiFi.mode(WIFI_AP_STA);
    int n = WiFi.scanNetworks(false, false, false, 300);
    String o = "{\"ok\":1,\"n\":[";
    for (int i = 0; i < n && i < 25; i++) { if (i) o += ","; o += "{\"s\":"; o += js(WiFi.SSID(i).c_str()); o += ",\"r\":"; o += WiFi.RSSI(i); o += ",\"c\":"; o += WiFi.channel(i); o += "}"; }
    o += "]}"; WiFi.scanDelete();
    if (apOnly) WiFi.mode(WIFI_AP);
    sendJson(200, o);
  } else if (act == "uptest") {
    if (upState != 2) { sendJson(400, "{\"ok\":0,\"err\":\"Köprü internete bağlı değil\"}"); return; }
    if (upBusy) { sendJson(400, "{\"ok\":0,\"err\":\"Meşgul, birazdan tekrar deneyin\"}"); return; }
    static UpJob j; memset(&j, 0, sizeof(j)); j.kind = 2;
    if (xQueueSend(upReqQ, &j, 0) == pdTRUE) upBusy = true;
    sendJson(200, "{\"ok\":1}");
  } else sendJson(400, "{\"ok\":0,\"err\":\"bilinmeyen\"}");
}
void hNodes() {
  char n[6]; snprintf(n, sizeof(n), "%04X", nodeId);
  int b = M5.Power.getBatteryLevel();
  String o = "{\"self\":{\"id\":\""; o += n; o += "\",\"place\":"; o += js(cfg.place); o += ",\"bat\":"; o += b; o += ",\"chg\":"; o += M5.Power.isCharging() == m5::Power_Class::is_charging ? 1 : 0;
  o += ",\"cli\":"; o += WiFi.softAPgetStationNum(); o += ",\"cnt\":"; o += nMsgs; o += ",\"gw\":"; o += upState == 2 ? 1 : 0; o += ",\"urg\":"; o += countUrgent(); o += ",\"open\":"; o += countUnassigned(); o += "},\"n\":[";
  bool first = true;
  for (auto &p : peers) {
    if (!p.seen) continue;
    char h[64]; snprintf(h, sizeof(h), "%s{\"id\":\"%02X%02X\",\"age\":%lu,\"bat\":%d,", first ? "" : ",", p.mac[4], p.mac[5], (unsigned long)((millis() - p.seen) / 1000), p.bat == 255 ? -1 : p.bat);
    first = false; o += h; o += "\"place\":"; o += js(p.place); o += ",\"chg\":"; o += p.chg; o += ",\"cli\":"; o += p.cli; o += ",\"cnt\":"; o += p.count;
    o += ",\"gw\":"; o += p.gw; o += ",\"urg\":"; o += p.urg; o += ",\"open\":"; o += p.open; o += "}";
  }
  o += "]}"; sendJson(200, o);
}
void hAddr() { String a = prefs.getString("addr", ""); sendJson(200, "{\"a\":" + js(a.c_str()) + "}"); }
void hCsv() {
  if (!isAdmin()) { denied(); return; }
  server.sendHeader("Content-Disposition", "attachment; filename=deprem_kayitlar.csv");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN); server.send(200, "text/csv; charset=utf-8", "");
  server.sendContent("\xEF\xBB\xBFid;cevap_verilen;zaman_TR;durum;aciliyet;kisi;cozuldu;resmi;gorev;ekip;triyaj;kan;kritik;hasar;ad;konum;telefon;ihtiyac;beceri;mesaj\r\n");
  for (int i = 0; i < nMsgs; i++) {
    const Msg &m = msgs[i]; if (m.flags & F_DELETED) continue;
    String c = hex8(m.id); c += ';'; c += m.parent ? hex8(m.parent) : String(""); c += ';'; c += m.ts ? fmtLocal(m.ts, true) : String("");
    c += ';'; c += ST_TR[m.status]; c += ';'; c += m.urg; c += ';'; c += m.people; c += ';'; c += (m.flags & F_RESOLVED) ? 1 : 0; c += ';'; c += (m.flags & F_OFFICIAL) ? 1 : 0;
    c += ';'; c += isTaskable(m) ? TASK_TR[m.task] : ""; c += ';'; if (m.team) c += m.team; c += ';'; c += TRI_TR[m.triage]; c += ';'; c += BLOOD[m.med & 15];
    c += ';'; for (int b = 0; b < 4; b++) if (m.med >> (4 + b) & 1) { c += MED_TR[b]; c += ' '; }
    c += ';'; if (m.status == ST_BLD) { c += DMG_TR[m.dmg & 3]; if (m.dmg & 4) c += " yasak"; if (m.dmg & 8) c += " ses"; }
    c += ';'; c += csvEsc(m.name); c += ';'; c += csvEsc(m.loc); c += ';'; c += csvEsc(m.phone);
    c += ';'; c += csvEsc(bitList(m.needs, NEED_TR).c_str()); c += ';'; c += csvEsc(bitList(m.skills, SKILL_TR).c_str()); c += ';'; c += csvEsc(m.text); c += "\r\n";
    server.sendContent(c);
  }
  server.sendContent("");
}
void hBackup() {
  if (!isAdmin()) { denied(); return; }
  saveStore(); dirty = false;
  File f = LittleFS.open("/m.bin", "r"); if (!f) { sendJson(500, "{\"ok\":0}"); return; }
  char n[48]; snprintf(n, sizeof(n), "attachment; filename=deprem_yedek_%04X.bin", nodeId);
  server.sendHeader("Content-Disposition", n); server.streamFile(f, "application/octet-stream"); f.close();
}
File rFile; bool rAuth = false;
void hRestoreUp() {
  HTTPUpload &u = server.upload();
  if (u.status == UPLOAD_FILE_START) { rAuth = isAdmin(); if (rAuth) rFile = LittleFS.open("/r.tmp", "w"); }
  else if (u.status == UPLOAD_FILE_WRITE) { if (rAuth && rFile) rFile.write(u.buf, u.currentSize); }
  else if (rFile) rFile.close();
}
void hRestoreDone() {
  if (!rAuth) { denied(); return; }
  if (rFile) rFile.close();
  int added = readStore("/r.tmp", true); LittleFS.remove("/r.tmp");
  if (added < 0) { sendJson(400, "{\"ok\":0,\"err\":\"Geçersiz yedek dosyası\"}"); return; }
  String o = "{\"ok\":1,\"added\":"; o += added; o += "}"; sendJson(200, o);
}

// ---- OTA (iPhone/iPad Safari ile Wi-Fi uzerinden guncelleme) ----
const char OTA_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="tr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Yazılım Güncelleme</title><style>body{font-family:system-ui,sans-serif;background:#14171c;color:#eee;padding:16px;max-width:520px;margin:auto}
input,button{width:100%;padding:12px;margin:8px 0;border-radius:8px;border:1px solid #39414d;background:#0f1216;color:#fff;font-size:16px;box-sizing:border-box}
button{background:#ff6b5e;border:0;font-weight:700}progress{width:100%;height:22px}.m{color:#aab2bf;font-size:13px}a{color:#7ab7ff}</style></head><body>
<p><a href="/">&larr; Ana sayfa</a></p><h2>Yazılım Güncelleme</h2><p class="m">Sürüm %VER% &middot; Cihaz %NODE%<br>Uygulama (OTA) .bin dosyasını seçin — tam/ilk yükleme .bin'i DEĞİL.</p>
<input id="pin" placeholder="Görevli PIN" inputmode="numeric" maxlength="4"><input id="f" type="file" accept=".bin,application/octet-stream">
<button id="b" onclick="go()">YÜKLE</button><progress id="p" max="100" value="0"></progress><p id="s"></p>
<script>function go(){var f=document.getElementById('f').files[0],s=document.getElementById('s');if(!f){s.textContent='Dosya seçin';return}
var x=new XMLHttpRequest();x.open('POST','/update?pin='+encodeURIComponent(document.getElementById('pin').value));
x.upload.onprogress=function(e){if(e.lengthComputable)document.getElementById('p').value=e.loaded*100/e.total};
x.onload=function(){s.textContent=x.responseText;document.getElementById('b').disabled=false;if(x.status==200)setTimeout(function(){location.href='/'},12000)};
x.onerror=function(){s.textContent='Bağlantı koptu. Cihaz yeniden başlıyor olabilir.'};
document.getElementById('b').disabled=true;s.textContent='Yükleniyor... Wi-Fi bağlantısını kesmeyin.';var d=new FormData();d.append('f',f);x.send(d)}</script></body></html>)HTML";
bool otaAuth = false, otaOk = false; String otaErr;
void hOtaPage() {
  String p = FPSTR(OTA_PAGE); char n[6]; snprintf(n, sizeof(n), "%04X", nodeId);
  p.replace("%VER%", FW_VER); p.replace("%NODE%", n);
  noCache(); server.send(200, "text/html; charset=utf-8", p);
}
void hOtaUpload() {
  HTTPUpload &u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    otaAuth = isAdmin(); otaOk = false; otaErr = "";
    if (!otaAuth) { otaErr = "PIN hatalı"; return; }
    if (dirty) { dirty = false; saveStore(); }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) otaErr = Update.errorString();
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (otaAuth && !otaErr.length()) {
      if (u.totalSize == 0 && u.currentSize && u.buf[0] != 0xE9) { otaErr = "Geçersiz dosya (uygulama .bin olmalı)"; Update.abort(); return; }
      if (Update.write(u.buf, u.currentSize) != u.currentSize) otaErr = Update.errorString();
    }
  } else if (u.status == UPLOAD_FILE_END) {
    if (otaAuth && !otaErr.length()) { if (Update.end(true)) otaOk = true; else otaErr = Update.errorString(); }
  } else if (u.status == UPLOAD_FILE_ABORTED) { Update.abort(); otaErr = "Yükleme yarıda kaldı"; }
}
void hOtaDone() {
  noCache();
  if (otaOk) {
    server.send(200, "text/plain; charset=utf-8", "Başarılı. Cihaz yeniden başlıyor, mesajlar korunur. Wi-Fi'a yeniden bağlanın.");
    delay(800); ESP.restart();
  } else {
    if (!otaAuth) delay(400);
    server.send(otaAuth ? 500 : 403, "text/plain; charset=utf-8", "Hata: " + (otaErr.length() ? otaErr : String("bilinmiyor")));
  }
}

// ---------------- ekran ----------------
int order[MAX_MSGS], nOrder = 0;
void buildOrder() {
  nOrder = 0;
  for (int i = 0; i < nMsgs; i++) { const Msg &m = msgs[i]; if (!(m.flags & 3) && isTaskable(m)) order[nOrder++] = i; }
  for (int a = 1; a < nOrder; a++) { int k = order[a], b = a - 1; while (b >= 0 && (msgs[order[b]].urg < msgs[k].urg || (msgs[order[b]].urg == msgs[k].urg && order[b] < k))) { order[b + 1] = order[b]; b--; } order[b + 1] = k; }
}
void header(const char *title, uint16_t bg) {
  int W = cv.width();
  cv.fillRect(0, 0, W, 20, bg); cv.setFont(&fonts::Font2); cv.setTextColor(TFT_WHITE);
  cv.setCursor(4, 2); cv.print(title);
  char b[24]; snprintf(b, sizeof(b), "%s%d%%", muted ? "SES YOK " : "", (int)M5.Power.getBatteryLevel());
  cv.fillRect(W - cv.textWidth(b) - 8, 0, cv.textWidth(b) + 8, 20, bg);
  cv.setCursor(W - cv.textWidth(b) - 4, 2); cv.print(b);
}
void draw() {
  static const char *ST[] = { "GUVENDE", "YARDIM LAZIM", "YARALI VAR", "ENKAZ ALTINDA", "DUYURU", "ARANIYOR", "GONULLU", "CEVAP", "SARSINTI", "BINA/ENKAZ", "DAGITIM", "PIL UYARI" };
  static const char *TK[] = { "EKIP YOK", "EKIP %d ATANDI", "EKIP %d YOLDA", "TAMAM" };
  static const uint16_t UC[] = { 0, TFT_GREEN, TFT_ORANGE, TFT_RED };
  int W = cv.width();
  cv.fillSprite(TFT_BLACK); cv.setTextWrap(false); cv.setFont(&fonts::Font2);
  int urgent = countUrgent();
  if (quakeShowUntil && (int32_t)(millis() - quakeShowUntil) < 0) {
    cv.fillSprite(TFT_RED); cv.setTextColor(TFT_WHITE); cv.setFont(&fonts::Font4);
    cv.setCursor(10, 20); cv.print("SARSINTI!"); cv.setFont(&fonts::Font2);
    cv.setCursor(10, 60); cv.printf("Tepe ivme: %.2f g", quakePeak);
    cv.setCursor(10, 80); cv.print("Guvenli yere gecin.");
    cv.setCursor(10, 110); cv.print("Bir tusa basin: kapat");
    cv.pushSprite(0, 0); return;
  }
  if (screen == 0) {
    String t = cfg.place[0] ? ascii(cfg.place) : String("DEPREM ACIL AGI");
    header(t.c_str(), urgent ? TFT_RED : 0x0320);
    cv.setTextColor(TFT_YELLOW); cv.setCursor(4, 22); cv.print("WiFi: "); cv.print(ascii(cfg.ssid));
    cv.setTextColor(TFT_WHITE);
    cv.setCursor(4, 39); cv.printf("Telefon: %d/%d   Ag: %d", WiFi.softAPgetStationNum(), cfg.maxcli, activePeers());
    cv.setCursor(4, 56); cv.printf("Kayit: %d", countVisible());
    cv.setTextColor(urgent ? TFT_RED : TFT_GREEN); cv.setCursor(90, 56); cv.printf("ACIL: %d", urgent);
    int una = countUnassigned(); cv.setTextColor(una ? TFT_MAGENTA : 0x8410); cv.setCursor(160, 56); cv.printf("Ekipsiz:%d", una);
    cv.setTextColor(TFT_WHITE); cv.setCursor(4, 73);
    if (upEnabled()) cv.printf("Kopru: %s  Bekl: %d", upState == 2 ? "BAGLI" : "araniyor", pendingUploads());
    else if (gwOnline()) cv.print("Agda internet koprusu var");
    else cv.printf("Kanal %d  No %04X  v" FW_VER, WiFi.channel(), nodeId);
    cv.setTextColor(TFT_CYAN); cv.setCursor(4, 90); cv.printf("Gorevli PIN: %s", pinStr);
    cv.setTextColor(0x8410); cv.setCursor(4, 116); cv.print("A:kayit B:QR  A uzun:ses");
  } else if (screen == 1) {
    header("ACIK YARDIM KAYITLARI", urgent ? TFT_RED : 0x0320);
    buildOrder();
    if (!nOrder) { cv.setCursor(4, 50); cv.print("Acik yardim kaydi yok."); }
    else {
      if (viewIdx >= nOrder) viewIdx = 0;
      const Msg &m = msgs[order[viewIdx]];
      cv.setTextColor(UC[m.urg]); cv.setCursor(4, 22); cv.printf("%d/%d %s", viewIdx + 1, nOrder, ST[m.status]);
      cv.setTextColor(m.task ? TFT_CYAN : TFT_MAGENTA); cv.setCursor(150, 22); cv.printf(TK[m.task], m.team);
      cv.setTextColor(TFT_WHITE); cv.setCursor(4, 38);
      String l = ascii(m.name); if (m.people) { l += " ("; l += m.people; l += " kisi)"; } cv.print(l);
      cv.setTextColor(TFT_YELLOW); cv.setCursor(4, 54); cv.print(ascii(m.loc));
      cv.setTextColor(0xC618); cv.setTextWrap(true); cv.setCursor(0, 70);
      String t = ascii(m.text); if (m.phone[0]) { t += " Tel:"; t += m.phone; } cv.print(t);
    }
  } else {
    cv.fillSprite(TFT_WHITE);
    String q = "WIFI:T:nopass;S:";
    for (const char *p = cfg.ssid; *p; p++) { if (strchr("\\;,:\"", *p)) q += '\\'; q += *p; }
    q += ";;";
    int H = cv.height(); cv.qrcode(q.c_str(), 2, 2, H - 4, 2);
    cv.setTextColor(TFT_BLACK); cv.setFont(&fonts::Font2);
    int x = H + 2;
    cv.setCursor(x, 6); cv.print("Telefon");
    cv.setCursor(x, 22); cv.print("kamerasi ile");
    cv.setCursor(x, 38); cv.print("okutun:");
    cv.setCursor(x, 54); cv.print("Wi-Fi'a");
    cv.setCursor(x, 70); cv.print("baglanir.");
    cv.setTextColor(0x0320); cv.setCursor(x, 96); cv.print("Adres:");
    cv.setCursor(x, 112); cv.print("192.168.4.1");
  }
  cv.pushSprite(0, 0);
}
void wake() {
  lastInput = millis();
  if (dimmed) { if (cfg.psave) M5.Display.wakeup(); M5.Display.setBrightness(cfg.bright); dimmed = false; }
}

// ---------------- kurulum ----------------
void setup() {
  auto mc = M5.config(); M5.begin(mc);
  prefs.begin("deprem", false);
  loadCfg();
  if (cfg.psave) setCpuFrequencyMhz(80);
  M5.Display.setRotation(1); M5.Display.setBrightness(cfg.bright);
  cv.setColorDepth(8); cv.createSprite(M5.Display.width(), M5.Display.height());
  M5.Speaker.setVolume(cfg.vol);

  LittleFS.begin(true);
  secret = prefs.getUInt("secret", 0);
  if (!secret) { secret = esp_random() | 1; prefs.putUInt("secret", secret); }
  String ps = prefs.getString("pins", "");
  if (ps.length() == 4) strcpy(pinStr, ps.c_str());
  else {
    uint16_t pin = prefs.getUShort("pin", 0);
    if (pin < 1000) { pin = 1000 + esp_random() % 9000; prefs.putUShort("pin", pin); }
    snprintf(pinStr, sizeof(pinStr), "%04u", pin);
  }
  muted = prefs.getBool("mute", false);
  if (readStore("/m.bin", false) < 0) nMsgs = 0;

  if (M5.Rtc.isEnabled()) {
    auto dt = M5.Rtc.getDateTime();
    if (dt.date.year >= 2025 && dt.date.year < 2040)
      setTime(civilToEpoch(dt.date.year, dt.date.month, dt.date.date, dt.time.hours, dt.time.minutes, dt.time.seconds), false);
  }

  WiFi.persistent(false);
  WiFi.mode(upEnabled() ? WIFI_AP_STA : WIFI_AP);
  WiFi.setAutoReconnect(false);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(cfg.ssid, nullptr, cfg.ch, 0, cfg.maxcli);
  WiFi.setSleep(false);
  WiFi.setTxPower(TXP[cfg.txp]);
  esp_wifi_set_inactive_time(WIFI_IF_AP, 90);
  uint8_t mac[6]; WiFi.softAPmacAddress(mac); nodeId = (mac[4] << 8) | mac[5];

  rxQ = xQueueCreate(10, sizeof(RxItem));
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(onRx);
    esp_now_peer_info_t p = {}; memcpy(p.peer_addr, BCAST, 6); p.channel = 0; p.ifidx = WIFI_IF_AP; p.encrypt = false;
    meshUp = esp_now_add_peer(&p) == ESP_OK;
  }
  upReqQ = xQueueCreate(1, sizeof(UpJob));
  upResQ = xQueueCreate(2, sizeof(UpRes));
  xTaskCreatePinnedToCore(upTask, "upTask", 12288, nullptr, 1, nullptr, 0);

  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", apIP);
  server.on("/", HTTP_GET, hRoot);
  server.on("/api/stat", HTTP_GET, hStat);
  server.on("/api/list", HTTP_GET, hList);
  server.on("/api/send", HTTP_POST, hSend);
  server.on("/api/admin", HTTP_POST, hAdmin);
  server.on("/export.csv", HTTP_GET, hCsv);
  server.on("/api/nodes", HTTP_GET, hNodes);
  server.on("/api/addr", HTTP_GET, hAddr);
  server.on("/backup.bin", HTTP_GET, hBackup);
  server.on("/restore", HTTP_POST, hRestoreDone, hRestoreUp);
  server.on("/update", HTTP_GET, hOtaPage);
  server.on("/update", HTTP_POST, hOtaDone, hOtaUpload);
  server.on("/favicon.ico", []() { server.send(204); });
  server.onNotFound(redirectHome);   // captive portal
  server.begin();
  lastInput = millis(); draw();
}

void loop() {
  M5.update();
  dns.processNextRequest();
  server.handleClient();
  meshLoop();
  quakeLoop();
  batLoop();
  upLoop();

  bool quakeOn = quakeShowUntil && (int32_t)(millis() - quakeShowUntil) < 0;
  if (M5.BtnA.wasClicked() || M5.BtnB.wasClicked()) {
    bool a = M5.BtnA.wasClicked(), was = dimmed; wake(); lastDraw = 0;
    if (quakeOn) { quakeShowUntil = 0; alarmBeeps = 0; }
    else if (!was) {
      if (a) { if (screen == 0) { screen = 1; viewIdx = 0; } else if (screen == 1) viewIdx++; }
      else screen = (screen + 1) % 3;
    }
  }
  if (M5.BtnA.wasHold()) { wake(); muted = !muted; prefs.putBool("mute", muted); alarmBeeps = 0; lastDraw = 0; }

  uint32_t t = millis();
  if (alarmBeeps && (int32_t)(t - nextBeep) >= 0) {
    wake(); if (!muted) M5.Speaker.tone(beepFreq, 180);
    alarmBeeps--; nextBeep = t + 350; lastDraw = 0;
  }
  if (!dimmed && cfg.dimSec && !quakeOn && t - lastInput > cfg.dimSec * 1000UL) {
    M5.Display.setBrightness(0); if (cfg.psave) M5.Display.sleep(); dimmed = true;
  }
  if (!dimmed && t - lastDraw > 1000UL) { lastDraw = t; draw(); }
  if (dirty && t - dirtyAt > 4000UL) { dirty = false; saveStore(); }
  delay(2);
}
