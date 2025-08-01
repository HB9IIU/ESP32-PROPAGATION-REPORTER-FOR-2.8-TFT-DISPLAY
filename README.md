# ⚠️ Work in Progress

> This project is currently under development. Features, layout, and functionality are subject to change.

---

## 🌐 ESP32 Solar Band Monitor

This project uses an **ESP32 with TFT display** to fetch and visualize real-time **solar and HF propagation data** from [hamqsl.com](https://www.hamqsl.com/solarxml.php). It displays band conditions, geomagnetic data, and VHF propagation metrics on a touchscreen interface, with a built-in configuration portal for Wi-Fi setup.

---

### ✨ Features

- TFT display (e.g. ILI9488) with custom fonts and color-coded solar data
- Real-time solar data fetched via XML over HTTP
- Auto-refresh every 15 minutes
- Touchscreen interface with multiple display pages:
  - HF band conditions (day/night)
  - Geophysical solar parameters (K, A index, flux, etc.)
  - VHF conditions
  - Intro/about screen
- Captive portal for Wi-Fi configuration using QR code
- Uses NTP for accurate time display (Local + UTC)

---

### 📦 Dependencies

- PlatformIO / Arduino with ESP32 support
- Libraries:
  - `ESPAsyncWebServer`
  - `Preferences`
  - `WiFi`
  - `TFT_eSPI`
  - `PNGdec`
  - `tinyxml2`
  - QR Code library

---

### 🛠️ Setup

1. Clone the repository.
2. Configure `platformio.ini` with your display and ESP32 variant.
3. Compile and upload via PlatformIO or Arduino IDE.

---

### 🖼️ Screenshot

![App Screenshot](./doc/ScreenShots/snapshot.png)

---

### 📡 Source of Solar Data

Data fetched from:

- [https://www.hamqsl.com/solarxml.php](https://www.hamqsl.com/solarxml.php)  
  by Dr. Paul Herrman, N0NBH

---

### 🛠️ License

Non-commercial use only. Respect data source license at [hamqsl.com].

---

