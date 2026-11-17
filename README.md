# ESP32 Earthquake Message System

> İnternet ve hücresel şebeke olmadan çalışan, ESP32 tabanlı deprem algılama ve acil durum mesajlaşma sistemi.
>
> *EN: An offline emergency messaging system on an ESP32. An MPU6050 detects shaking, the ESP32 runs its own Wi-Fi access point with a captive portal, and people nearby can post messages and help requests from their phones.*

ESP32 kendi Wi-Fi ağını yayınlar. Ağa bağlanan telefon, captive portal ile otomatik olarak acil durum sayfasına yönlenir. Sayfa üzerinden mesaj gönderilebilir, yardım istenebilir ve konum paylaşılabilir. MPU6050 ivmeölçer güçlü bir sarsıntı algılarsa sistem deprem uyarısı verir.

![Yönetici paneli](<MGM_Esp32_earthquake_system_v1.1/1 (1).png>)

## Özellikler

- **Deprem algılama:** MPU6050'den okunan ivme büyüklüğü eşik değerini (varsayılan 2.5 g) aşarsa uyarı verir. Aynı sarsıntıda tekrar uyarı vermemek için 10 saniyelik bekleme süresi vardır. Uyarı sırasında LED belirli bir desenle yanıp söner.
- **Kendi Wi-Fi ağı:** Varsayılan ağ adı `EarthquakeAlertSystem`, şifresizdir. Ağ adı yönetici panelinden değiştirilebilir.
- **Captive portal:** DNS sunucusu tüm istekleri ESP32'ye yönlendirir. Android ve Windows'un bağlantı kontrol adresleri de karşılanır.
- **Mesajlaşma:** Son 50 mesajı saklar.
- **Yardım talepleri:** Kullanıcı adıyla ve tarayıcı konumuyla yardım isteği gönderilir. Son 20 talep ve en fazla 10 kullanıcının konumu tutulur.
- **Yönetici paneli** (`/admin`): Çalışma süresi, boş bellek, bağlı cihaz sayısı. ESP32'yi yeniden başlatma, acil durum modunu açıp kapatma, ağ adını ve deprem hassasiyetini değiştirme, mesajları, yardım taleplerini ve tüm veriyi temizleme.

## Donanım

- ESP32 geliştirme kartı
- MPU6050 ivmeölçer (I²C). Varsayılan I²C pinleri kullanılır (ESP32 DevKit'te SDA = GPIO21, SCL = GPIO22).
- Kart üzerindeki LED (GPIO2)

## Yazılım

Arduino IDE ile derlenir. Kullanılan kütüphaneler: `WiFi`, `WebServer`, `DNSServer`, `LittleFS`, `Wire`, `MPU6050`, `ArduinoJson`.

| Klasör | Açıklama |
| --- | --- |
| `MGM_Esp32_earthquake_system_v1` | İlk sürüm: mesajlaşma, yardım talebi, yönetici paneli ve ağ adı değiştirme |
| `MGM_Esp32_earthquake_system_v1.1` | ESP32 + MPU6050 son sürümü: captive portal, konum paylaşımı, sistem istatistikleri, hassasiyet ayarı ve genişletilmiş arayüz |
| `MGM_M5Stick_earthquake_system_v2.1` | **Yeni nesil (M5StickC Plus/Plus2):** ESP-NOW mesh ile cihazlar arası mesaj aktarımı, TR/EN/AR arayüz, görevli paneli (görev takibi, triyaj, ağ haritası), yakınımı arıyorum eşleştirmesi, OTA güncelleme, Telegram/webhook internet köprüsü. Ayrıntılar klasördeki README'de. |

Her sürümün `data/` klasöründe web arayüzü dosyaları (HTML, CSS, JS) bulunur.

## Kurulum

1. Arduino IDE'ye ESP32 kart desteğini ve yukarıdaki kütüphaneleri ekleyin.
2. `.ino` dosyasını kartınıza yükleyin.
3. `data/` klasörünü LittleFS'e yükleyin (Arduino IDE için LittleFS yükleme eklentisi ile).
4. Telefonla `EarthquakeAlertSystem` ağına bağlanın. Sayfa otomatik açılmazsa tarayıcıdan `192.168.4.1` adresine gidin.

## Güvenlik notu

Yönetici paneli kullanıcı adı ve şifresi kodda sabittir (`adminUsername`, `adminPassword`). Gerçek kullanımdan önce mutlaka değiştirin. Bu proje bir deneme/prototiptir ve resmî bir erken uyarı sisteminin yerine geçmez.

— Mert Güzelmansur (MGM)
