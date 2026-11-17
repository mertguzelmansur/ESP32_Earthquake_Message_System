# M5Stick Earthquake Emergency Network — Deprem Acil Haberleşme Ağı

> Baz istasyonları ve internet çalışmadığında devreye giren, M5StickC Plus üzerinde çalışan açık Wi‑Fi acil durum haberleşme ağı. Telefonlar uygulama kurmadan bağlanır, durumunu ve aciliyetini bildirir; cihazlar ESP‑NOW mesh ile mesajları birbirine aktarır.
>
> *EN: An offline emergency communication network for earthquakes, running on the M5StickC Plus. Phones join an open Wi‑Fi network with a captive portal (no app needed) to report their status. Devices relay messages to each other over an ESP‑NOW mesh.*

Bu klasör, aynı repodaki ESP32 + MPU6050 sürümlerinin (v1, v1.1) ikinci kuşağıdır.

## Nasıl çalışır?

1. Cihaz şifresiz `0000-DEPREM-ACIL-AG` ağını yayınlar. Ağ adı sıfırlarla başladığı için Wi‑Fi listesinin en üstünde görünür.
2. Bağlanan telefonda captive portal açılır. Açılmazsa tarayıcıya `192.168.4.1` yazılır.
3. Kullanıcı durumunu bildirir: güvendeyim, yardım lazım, yaralı var, enkaz altındayım, bina/enkaz noktası, yakınımı arıyorum, gönüllüyüm, duyuru.
4. Mesajlar cihaza kaydedilir ve ESP‑NOW ile yakındaki diğer cihazlara aktarılır. Her cihaz bütün ağın kayıtlarını gösterir.
5. Bir cihaz internet bulursa (telefon hotspot, Starlink vb.) tüm kayıtları Telegram'a veya bir webhook adresine iletir.

## Özellikler

**Halk için (Türkçe, İngilizce, Arapça arayüz)**
- Durum bildirimi: aciliyet, kişi sayısı, adres, telefon, ihtiyaçlar (su, gıda, ilaç, çadır…)
- Kan grubu ve kritik tıbbi ihtiyaç (insülin, diyaliz, oksijen, düzenli ilaç)
- Bina/enkaz noktası kaydı: hasar derecesi, "girmek yasak", "içeriden ses geliyor" (otomatik ÇOK ACİL)
- **Yakınımı arıyorum:** aranan isim "Güvendeyim" ve yardım kayıtlarıyla eşleştirilir; Türkçe karakter farkı sorun olmaz ("Ayse" = "Ayşe")
- Gönüllü kaydı (13 beceri), kayıtlara cevap yazma, arama ve filtreler
- Dağıtım noktaları ve stokları, sabit duyuru, hazır adres listesi

**Görevli paneli (PIN ile)**
- Görev takibi: yeni → ekip atandı → yolda → tamamlandı
- START triyaj etiketi (yeşil / sarı / kırmızı / siyah)
- İhtiyaç özeti ve ağdaki toplam stokla karşılaştırma, gönüllü listesi
- Ağ haritası: her cihazın adı, pili, bağlı telefon sayısı, son görülme zamanı
- İmzalı resmî duyurular (HMAC‑SHA256, ortak ağ anahtarı)
- Tüm ayarlar: Wi‑Fi adı/kanal/güç, mesh, ekran ve ses, mesaj kuralları, pil tasarrufu, sarsıntı sensörü, internet köprüsü
- CSV dışa aktarma, yedek indirme ve geri yükleme, fabrika ayarları
- **OTA güncelleme:** iPhone/iPad Safari'den `192.168.4.1/update`

**Cihaz**
- Durum ekranı, açık yardım kayıtları listesi, Wi‑Fi QR kodu (B tuşu)
- Çok acil kayıtta buzzer alarmı
- Dahili IMU ile sarsıntı algılama; istenirse ağa "sarsıntı" kaydı yayınlar
- Düşük pil uyarısını tüm ağa yayınlar

## Donanım

- M5StickC Plus veya Plus2 (ESP32, 4 MB flash)
- Uzun süreli çalışma için powerbank önerilir; dahili pil kısa dayanır.

## Yükleme

`releases/v2.1/` klasöründe iki dosya var:

| Dosya | Ne zaman? | Nasıl? |
| --- | --- | --- |
| `DepremAcilAg_v2.1_TAM_kabloyla_0x0.bin` | İlk yükleme veya bölümleme değişikliği | Kabloyla **0x0** adresine (esptool, M5Burner vb.) |
| `DepremAcilAg_v2.1_OTA_guncelleme.bin` | v2.1 yüklü cihazı güncellemek | Wi‑Fi üzerinden `192.168.4.1/update` |

```bash
esptool.py --chip esp32 --baud 921600 write_flash 0x0 DepremAcilAg_v2.1_TAM_kabloyla_0x0.bin
```

İlk açılışta görevli PIN'i cihaz ekranında görünür.

> **Not:** v2.1 büyük bölümleme kullanır (2 × 1.875 MB uygulama alanı, OTA korunur). Eski sürümden geçerken mesajlar silinir. Önce panelden yedek alın, yüklemeden sonra geri yükleyin. v2.0 ve v2.1 cihazları aynı mesh'te konuşamaz.

## Kaynaktan derleme

- Arduino ESP32 core **2.0.17**
- Kütüphaneler: [M5Unified](https://github.com/m5stack/M5Unified), [M5GFX](https://github.com/m5stack/M5GFX)
- Kart: `M5Stick-C`. `partitions.csv` taslak klasöründe olduğu için otomatik kullanılır.

```bash
# bu klasörün içinden; web arayüzünü değiştirdiyseniz önce gzip'li başlık dosyasını üretin
python3 tools/gen_page.py web/page.html firmware/deprem_ag/page.h

arduino-cli compile --fqbn esp32:esp32:m5stick-c \
  --build-property upload.maximum_size=1966080 firmware/deprem_ag
```

## Proje yapısı

```
firmware/deprem_ag/   Arduino taslağı (deprem_ag.ino, page.h, partitions.csv)
web/page.html         Web arayüzünün kaynağı (TR/EN/AR)
tools/gen_page.py     page.html → gzip'li page.h
releases/             Derlenmiş .bin dosyaları
```

## Sınırlar

- ESP32 aynı anda en fazla 10 telefon kabul eder. Kullanıcılar gönderdikten sonra ağdan çıkmalıdır.
- Wi‑Fi menzili açık alanda kabaca 30–50 m'dir.
- Ağ açıktır: yazılan her şeyi bağlanan herkes görebilir.
- Sarsıntı algılama erken uyarı sistemi değildir. Yanlış alarm olmaması için cihaz sabitlenmelidir.
- Resmî bir kurum hizmeti değildir.

## Yol haritası

- Raspberry Pi tabanlı mahalle istasyonu (Pi + dış ortam erişim noktası, yüzlerce kullanıcı, çevrimdışı harita)
- İstasyonlar arası 5 GHz noktadan noktaya omurga ve ESP32 noktalarıyla köprü
- LoRa / Meshtastic yedek hattı

---
MGM · Mert Güzelmansur
