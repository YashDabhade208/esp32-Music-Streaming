# 🎶 ESP32 Music Player with Web UI and DuckDNS

This project turns your **ESP32** into a Wi-Fi-connected music server that:
- Hosts and streams `.mp3`, `.wav`, and `.ogg` files via a web UI.
- Allows uploading music from the browser.
- Manages a custom playlist with a built-in audio player.
- Can be accessed remotely via **DuckDNS** with a static IP.

---

## 📦 Features

- 🎵 Upload & play music files stored in ESP32's SPIFFS.
- 🌐 Serve a mobile-friendly HTML+CSS+JS interface.
- 📜 Playlist support with play/next/remove.
- 🔁 Auto-play next song.
- 🔍 Search functionality.
- 🧾 REST API to list files.
- ☁️ Remote access via [DuckDNS](https://www.duckdns.org/).
- 🔐 Static IP configuration for LAN stability.

---

## 🧰 Requirements

- **ESP32 Dev Board**
- **Arduino IDE** or **PlatformIO**
- Libraries:
  - `WiFi.h`
  - `HTTPClient.h`
  - `WebServer.h`
  - `SPIFFS.h`
  - `FS.h`

---

## ⚙️ Setup Instructions

### 1. **Clone or Copy the Code**

Save the `.ino` file in a folder named `ESP32MusicPlayer`.

### 2. **Install Required Libraries**

In the Arduino IDE:
- Go to `Sketch → Include Library → Manage Libraries`
- Search and install:
  - `ESPAsyncWebServer` *(if used in future updates)*
  - `ESP32 FS (SPIFFS)`
  - `WebServer`

### 3. **Configure Wi-Fi and DuckDNS**
In the sketch, update these lines:

```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

const char* duckdns_token = "YOUR_DUCKDNS_TOKEN";
const char* duckdns_domain = "yourduckdnsname";
