#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include "qrcode.h"
#include <tinyxml2.h>
#include <TFT_eSPI.h>
#include <PNGdec.h>                   
#include "fancySplash.h"              // Image is stored here in an 8-bit array  https://notisrac.github.io/FileToCArray/ (select treat as binary)
#include "html_page.h"
#include <JetBrainsMono_Bold15pt7b.h> //  https://rop.nl/truetype2gfx/
#include <JetBrainsMono_Bold11pt7b.h>
#include <JetBrainsMono_Light13pt7b.h>
#include <JetBrainsMono_Medium13pt7b.h>
#include <JetBrainsMono_Light7pt7b.h>
#include <HB97DIGITS12pt7b.h>
#include <UbuntuMono_Regular8pt7b.h>
#include <time.h>

//-------------------------------------------------------------------------------

// UTC Offset
int UTCoffset = 2;

//-------------------------------------------------------------------------------

const char *solarDataUrl = "https://www.hamqsl.com/solarxml.php";
int currentPage = 0;
int scanCount = 0;
AsyncWebServer server(80);
Preferences prefs;
PNG png; // PNG decoder instance

// Prototypes
void drawQRCode(const char *text, int x, int y, int scale);
void drawQRcodeInstructions();
void startConfigurationPortal();
bool tryConnectSavedWiFi();
void displaySplashScreen();
void pngDraw(PNGDRAW *pDraw);
void fadeSplashToBlack(int steps = 50000, int delayMicros = 0);
void fetchSolarData();
void drawSolarSummaryPage0();
String formatUpdatedTimestampToUTC(const String &raw);
void drawSolarSummaryPage1();
void drawSolarSummaryPage2();
void drawSolarSummaryPage3();
void drawIntroPage(bool forceDisplay);

// Time update interval
unsigned long lastPrint = 0;

char lastUtcStr[9] = "";
char lastLocalStr[9] = "";

// TFT Display Setup
TFT_eSPI tft = TFT_eSPI();

// Struct to store all parsed solar data
struct SolarData
{
  String source;
  String updated;
  int solarFlux;
  int aIndex;
  int kIndex;
  String kIndexNT;
  String xRay;
  int sunspots;
  float heliumLine;
  String protonFlux;
  String electronFlux;
  int aurora;
  float normalization;
  float latDegree;
  float solarWind;
  float magneticField;
  String geomagneticField;
  String signalNoise;
  String fof2;
  String mufFactor;
  String muf;

  struct BandCondition
  {
    String name;
    String time;
    String condition;
  } bandConditions[8];

  struct VHFCondition
  {
    String name;
    String location;
    String condition;
  } vhfConditions[5];
};

SolarData solarData;
void setup()
{
  Serial.begin(115200);
  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);

  // Backlight pin setup
  pinMode(TFT_BLP, OUTPUT);
  digitalWrite(TFT_BLP, HIGH); // Turn backlight ON permanently

  displaySplashScreen();

  // Connect to Wi-Fi
  if (!tryConnectSavedWiFi()) {
    startConfigurationPortal();
  }

  // Configure NTP (UTC only)
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  // Wait for NTP sync
  tft.setFreeFont(&JetBrainsMono_Light7pt7b);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawCentreString("Waiting for NTP synch...", 160, 110, 1);
  Serial.print("⏳ Waiting for NTP");
  while (time(nullptr) < 100000)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n🕓 Time synced via NTP");

  // ⏱️ Check if UTCoffset is already saved
  prefs.begin("time", true);
  bool hasSavedOffset = prefs.isKey("UTCoffset");
  prefs.end();

  if (hasSavedOffset) {
    // ✅ Load previously saved offset
    prefs.begin("time", true);
    UTCoffset = prefs.getInt("UTCoffset", 2);
    prefs.end();
    Serial.printf("✅ Loaded saved UTCoffset: %d\n", UTCoffset);
  }
  else {
    // 🕓 Attempt to calculate from phone time
    prefs.begin("time", true);
    String timeStr = prefs.getString("localTime", "");
    prefs.end();

    if (timeStr.length() >= 5) {
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        int userHour = timeStr.substring(0, 2).toInt();
        int utcHour = timeinfo.tm_hour;
        int offset = userHour - utcHour;

        if (offset < -12) offset += 24;
        if (offset > 12)  offset -= 24;

        UTCoffset = offset;

        prefs.begin("time", false);
        prefs.putInt("UTCoffset", UTCoffset);
        prefs.end();

        Serial.printf("📱 Phone time: %s | 🌍 UTC: %02d:%02d\n", timeStr.c_str(), utcHour, timeinfo.tm_min);
        Serial.printf("🧭 Calculated and saved UTCoffset = %d\n", UTCoffset);
      } else {
        Serial.println("❌ getLocalTime failed, using fallback UTCoffset = 2");
        UTCoffset = 2;
      }
    } else {
      Serial.println("⚠️ No phone time found, using default UTCoffset = 2");
      UTCoffset = 2;
    }
  }

  fetchSolarData();
  fadeSplashToBlack();

  drawIntroPage(false); // set to true to force
  delay(500);
  drawSolarSummaryPage0();
}

void loop()
{
  unsigned long nowMillis = millis();

  // ⏱ Time display every second
  if (nowMillis - lastPrint >= 1000)
  {
    lastPrint = nowMillis;

    time_t now = time(nullptr);
    struct tm *utc_tm = gmtime(&now);

    // --- Format UTC ---
    char utcStr[9];
    strftime(utcStr, sizeof(utcStr), "%H:%M:%S", utc_tm);

    // --- Convert to local time manually ---
    struct tm local_tm = *utc_tm;
    local_tm.tm_hour += UTCoffset;
    mktime(&local_tm);

    char localStr[9];
    strftime(localStr, sizeof(localStr), "%H:%M:%S", &local_tm);

    if (currentPage == 0)
    {
      // Erase old time
      tft.setFreeFont(&HB97DIGITS12pt7b);
      tft.setTextColor(TFT_BLACK);
      tft.drawCentreString(lastLocalStr, 80, 205, 1);
      tft.drawCentreString(lastUtcStr, 240, 205, 1);

      // Draw new time
      tft.setTextColor(TFT_WHITE);
      tft.drawCentreString(localStr, 80, 205, 1);
      tft.drawCentreString(utcStr, 240, 205, 1);
    }

    strncpy(lastLocalStr, localStr, sizeof(lastLocalStr));
    strncpy(lastUtcStr, utcStr, sizeof(lastUtcStr));
  }

  // 🔁 Auto-refresh solar data every 15 minutes
  static unsigned long lastSolarFetch = 0;
  const unsigned long refreshInterval = 15 * 60 * 1000UL; // 15 min

  if (nowMillis - lastSolarFetch > refreshInterval)
  {
    Serial.println("🔄 Refreshing solar data...");
    fetchSolarData();

    // Redraw current page
    switch (currentPage)
    {
    case 0:
      drawSolarSummaryPage0();
      break;
    case 1:
      drawSolarSummaryPage1();
      break;
    case 2:
      drawSolarSummaryPage2();
      break;
    case 3:
      drawSolarSummaryPage3();
      break;
    }

    lastSolarFetch = nowMillis;
  }

  // 👆 Touch detection to switch pages
  uint16_t x, y;
  if (tft.getTouch(&x, &y))
  {
    delay(200); // debounce
    currentPage = (currentPage + 1) % 4;

    switch (currentPage)
    {
    case 0:
      drawSolarSummaryPage0();
      break;
    case 1:
      drawSolarSummaryPage1();
      break;
    case 2:
      drawSolarSummaryPage2();
      break;
    case 3:
      drawSolarSummaryPage3();
      break;
    }
  }
}

void displaySplashScreen()
{
  digitalWrite(TFT_BLP, LOW);

  // https://notisrac.github.io/FileToCArray/
  int16_t rc = png.openFLASH((uint8_t *)fancySplash, sizeof(fancySplash), pngDraw);

  if (rc == PNG_SUCCESS)
  {

    Serial.println("Successfully opened png file");
    Serial.printf("image specs: (%d x %d), %d bpp, pixel type: %d\n", png.getWidth(), png.getHeight(), png.getBpp(), png.getPixelType());
    tft.startWrite();
    uint32_t dt = millis();
    rc = png.decode(NULL, 0);
    Serial.print("Displayed in ");
    Serial.print(millis() - dt);
    Serial.println(" ms");
    tft.endWrite();
  }

  digitalWrite(TFT_BLP, HIGH);
}

void pngDraw(PNGDRAW *pDraw)
{
  uint16_t lineBuffer[480];
  png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  tft.pushImage(0, 0 + pDraw->y, pDraw->iWidth, 1, lineBuffer);
}
void fadeSplashToBlackFIRST(int steps, int delayMicros)
{
  for (int i = 0; i < steps; i++)
  {
    int x = random(0, 320);
    int y = random(0, 240);
    tft.drawPixel(x, y, TFT_BLACK);
    if (delayMicros > 0)
      delayMicroseconds(delayMicros); // Optional slowdown
  }
}

void fadeSplashToBlack(int steps, int delayMicros)
{
  const int screenWidth = 320;
  const int screenHeight = 240;
  const int dotSize = 3; // Try 3x3 black squares

  for (int i = 0; i < steps; i++)
  {
    int x = random(screenWidth - dotSize);
    int y = random(screenHeight - dotSize);
    tft.fillRect(x, y, dotSize, dotSize, TFT_BLACK);

    if (delayMicros > 0)
      delayMicroseconds(delayMicros);
  }
}

void fetchSolarData()
{

  // Fetch XML
  HTTPClient http;
  http.begin(solarDataUrl);
  int httpCode = http.GET();

  if (httpCode <= 0)
  {
    Serial.println("HTTP request failed");
    return;
  }

  String payload = http.getString();
  tinyxml2::XMLDocument doc;
  doc.Parse(payload.c_str());
  if (doc.ErrorID() != 0)
  {
    Serial.print("XML parse error: ");
    Serial.println(doc.ErrorStr());
    return;
  }

  tinyxml2::XMLElement *solardataXML = doc.RootElement()->FirstChildElement("solardata");

  auto get = [&](const char *tag)
  {
    tinyxml2::XMLElement *e = solardataXML->FirstChildElement(tag);
    return e && e->GetText() ? String(e->GetText()) : String("");
  };

  // Assign fields to struct
  solarData.source = get("source");
  // solarData.updated = get("updated"); reformatted because like this 31 Jul 2025 1321 GMT
  solarData.updated = formatUpdatedTimestampToUTC(get("updated"));
  solarData.solarFlux = get("solarflux").toInt();
  solarData.aIndex = get("aindex").toInt();
  solarData.kIndex = get("kindex").toInt();
  solarData.kIndexNT = get("kindexnt");
  solarData.xRay = get("xray");
  solarData.sunspots = get("sunspots").toInt();
  solarData.heliumLine = get("heliumline").toFloat();
  solarData.protonFlux = get("protonflux");
  solarData.electronFlux = get("electonflux");
  solarData.aurora = get("aurora").toInt();
  solarData.normalization = get("normalization").toFloat();
  solarData.latDegree = get("latdegree").toFloat();
  solarData.solarWind = get("solarwind").toFloat();
  solarData.magneticField = get("magneticfield").toFloat();
  solarData.geomagneticField = get("geomagfield");
  solarData.signalNoise = get("signalnoise");
  solarData.fof2 = get("fof2");
  solarData.mufFactor = get("muffactor");
  solarData.muf = get("muf");

  // Parse band conditions
  int bIndex = 0;
  tinyxml2::XMLElement *band = solardataXML->FirstChildElement("calculatedconditions")->FirstChildElement("band");
  while (band && bIndex < 8)
  {
    solarData.bandConditions[bIndex].name = band->Attribute("name");
    solarData.bandConditions[bIndex].time = band->Attribute("time");
    solarData.bandConditions[bIndex].condition = band->GetText();
    band = band->NextSiblingElement("band");
    bIndex++;
  }

  // Parse VHF conditions
  int vIndex = 0;
  tinyxml2::XMLElement *phen = solardataXML->FirstChildElement("calculatedvhfconditions")->FirstChildElement("phenomenon");
  while (phen && vIndex < 5)
  {
    solarData.vhfConditions[vIndex].name = phen->Attribute("name");
    solarData.vhfConditions[vIndex].location = phen->Attribute("location");
    solarData.vhfConditions[vIndex].condition = phen->GetText();
    phen = phen->NextSiblingElement("phenomenon");
    vIndex++;
  }

  http.end();

  // --- Serial Debug Output ---
  Serial.println("\n=== Solar Data ===");
  Serial.println("Source: " + solarData.source);
  Serial.println("Updated: " + solarData.updated);
  Serial.printf("Solar Flux: %d\n", solarData.solarFlux);
  Serial.printf("A Index: %d\n", solarData.aIndex);
  Serial.printf("K Index: %d\n", solarData.kIndex);
  Serial.println("K Index NT: " + solarData.kIndexNT);
  Serial.println("X-Ray: " + solarData.xRay);
  Serial.printf("Sunspots: %d\n", solarData.sunspots);
  Serial.printf("Helium Line: %.1f\n", solarData.heliumLine);
  Serial.println("Proton Flux: " + solarData.protonFlux);
  Serial.println("Electron Flux: " + solarData.electronFlux);
  Serial.printf("Aurora: %d\n", solarData.aurora);
  Serial.printf("Normalization: %.2f\n", solarData.normalization);
  Serial.printf("Lat Degree: %.2f\n", solarData.latDegree);
  Serial.printf("Solar Wind: %.1f\n", solarData.solarWind);
  Serial.printf("Magnetic Field: %.1f\n", solarData.magneticField);
  Serial.println("Geomagnetic Field: " + solarData.geomagneticField);
  Serial.println("Signal Noise: " + solarData.signalNoise);
  Serial.println("foF2: " + solarData.fof2);
  Serial.println("MUF Factor: " + solarData.mufFactor);
  Serial.println("MUF: " + solarData.muf);

  Serial.println("--- Band Conditions ---");
  for (int i = 0; i < 8; i++)
  {
    if (solarData.bandConditions[i].name.isEmpty())
      break;
    Serial.printf("[%s] %s: %s\n",
                  solarData.bandConditions[i].time.c_str(),
                  solarData.bandConditions[i].name.c_str(),
                  solarData.bandConditions[i].condition.c_str());
  }

  Serial.println("--- VHF Conditions ---");
  for (int i = 0; i < 5; i++)
  {
    if (solarData.vhfConditions[i].name.isEmpty())
      break;
    Serial.printf("%s (%s): %s\n",
                  solarData.vhfConditions[i].name.c_str(),
                  solarData.vhfConditions[i].location.c_str(),
                  solarData.vhfConditions[i].condition.c_str());
  }
}

void drawSolarSummaryPage0()
{

  tft.fillScreen(TFT_BLACK);
  // draw frames
  //  Define positions and dimensions
  int dayX = 10;
  int nightX = 170;
  int blockY = 12;
  int blockWidth = 140;
  int blockHeight = 143;
  int cornerRadius = 8;

  tft.drawRoundRect(dayX, blockY, blockWidth, blockHeight, cornerRadius, TFT_DARKGREY);
  tft.drawRoundRect(nightX, blockY, blockWidth, blockHeight, cornerRadius, TFT_DARKGREY);
  tft.fillRect(80 - 27, 0, 54, 20, TFT_BLACK);
  tft.fillRect(240 - 38, 0, 76, 20, TFT_BLACK);

  // Draw headers
  tft.setFreeFont(&JetBrainsMono_Bold11pt7b);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.drawCentreString("DAY", 80, 2, 1);
  tft.drawCentreString("NIGHT", 240, 2, 1);

  // Band conditions by time
  tft.setFreeFont(&JetBrainsMono_Bold15pt7b);

  int yStart = 22;
  for (int i = 0; i < 4; i++)
  {
    // DAY

    String band = solarData.bandConditions[i].name;
    String cond = solarData.bandConditions[i].condition;
    uint16_t color = cond == "Good" ? TFT_GREEN : cond == "Fair" ? TFT_YELLOW
                                                                 : TFT_RED;
    tft.setTextColor(color);
    tft.drawCentreString(band, 80, yStart + i * 32, 1);

    // NIGHT
    band = solarData.bandConditions[i + 4].name;
    cond = solarData.bandConditions[i + 4].condition;
    color = cond == "Good" ? TFT_GREEN : cond == "Fair" ? TFT_YELLOW
                                                        : TFT_RED;
    tft.setTextColor(color);

    tft.drawCentreString(band, 240, yStart + i * 32, 1);
  }

  tft.setFreeFont(&JetBrainsMono_Light7pt7b);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.drawCentreString("Updated: " + solarData.updated, 160, 160, 1);
  // tft.drawCentreString("Updating...", 80, 185, 1);
  // tft.drawCentreString("Updating...", 240, 185, 1);

  int LocalX = 10;
  int UTCX = 170;
  blockY = 190;
  blockWidth = 140;
  blockHeight = 48;
  cornerRadius = 8;

  tft.drawRoundRect(LocalX, blockY, blockWidth, blockHeight, cornerRadius, TFT_DARKGREY);
  tft.drawRoundRect(UTCX, blockY, blockWidth, blockHeight, cornerRadius, TFT_DARKGREY);
  tft.fillRect(80 - 36, blockY - 15, 72, 35, TFT_BLACK);
  tft.fillRect(240 - 26, blockY - 15, 52, 35, TFT_BLACK);

  // Draw headers
  tft.setFreeFont(&JetBrainsMono_Bold11pt7b);
  tft.setTextColor(TFT_LIGHTGREY);

  tft.drawCentreString("Local", 80, 179, 1);
  tft.drawCentreString("UTC", 240, 179, 1);
}

String formatUpdatedTimestampToUTC(const String &raw)
{
  int gmtPos = raw.indexOf("GMT");
  if (gmtPos == -1 || gmtPos < 5)
    return raw; // malformed or too short

  // Extract 4 characters before "GMT" → should be the time
  String timePart = raw.substring(gmtPos - 5, gmtPos - 1); // e.g. "1321"
  if (timePart.length() != 4)
    return raw;

  // Insert colon in the time
  String formattedTime = timePart.substring(0, 2) + ":" + timePart.substring(2, 4);

  // Everything before the time
  String datePart = raw.substring(0, gmtPos - 5);
  datePart.trim(); // remove any leading/trailing whitespace

  return datePart + " " + formattedTime + " UTC";
}

void drawSolarSummary()
{
  int y = 13;
  int lineSpacing = 18;
  tft.setFreeFont(&UbuntuMono_Regular8pt7b);
  tft.setTextSize(1);
  // Helper to print each line with label and value
  auto printLine = [&](const String &label, const String &value, uint16_t color = TFT_WHITE)
  {
    tft.setTextColor(color, TFT_BLACK); // foreground, background
    tft.setCursor(10, y);
    tft.print(label);
    tft.setCursor(150, y); // aligned values
    tft.print(": ");
    tft.print(value);
    y += lineSpacing;
  };

  // Basic condition → color mapping (for geomagnetic, signal noise, etc.)
  auto colorByCondition = [](const String &cond) -> uint16_t
  {
    if (cond.equalsIgnoreCase("Good"))
      return TFT_GREEN;
    if (cond.equalsIgnoreCase("Fair"))
      return TFT_YELLOW;
    if (cond.equalsIgnoreCase("Poor"))
      return TFT_RED;
    if (cond.indexOf("Storm") >= 0)
      return TFT_RED;
    if (cond.indexOf("Unsettled") >= 0)
      return TFT_ORANGE;
    return TFT_WHITE;
  };

  printLine("Solar Flux", String(solarData.solarFlux));
  printLine("A Index", String(solarData.aIndex));
  printLine("K Index", String(solarData.kIndex));
  printLine("K Index NT", solarData.kIndexNT);
  printLine("X-Ray", solarData.xRay);
  printLine("Sunspots", String(solarData.sunspots));
  printLine("Helium Line", String(solarData.heliumLine, 1));
  printLine("Proton Flux", solarData.protonFlux);
  printLine("Electron Flux", solarData.electronFlux);
  printLine("Aurora", String(solarData.aurora));
  printLine("Normalization", String(solarData.normalization, 2));
  printLine("Lat Degree", String(solarData.latDegree, 2));
  printLine("Solar Wind", String(solarData.solarWind, 1));
  printLine("Mag Field", String(solarData.magneticField, 1));
  printLine("Geo Field", solarData.geomagneticField, colorByCondition(solarData.geomagneticField));
  printLine("S/N", solarData.signalNoise, colorByCondition(solarData.signalNoise));
  printLine("foF2", solarData.fof2);
  printLine("MUF Fact", solarData.mufFactor);
  printLine("MUF", solarData.muf);
}

void drawSolarSummaryPage1()
{
  int y = 13;
  int lineSpacing = 18;
  tft.fillScreen(TFT_BLACK);

  tft.setFreeFont(&UbuntuMono_Regular8pt7b);
  tft.setTextSize(1);

  // Adjust spacing
  const int labelX = 10;
  const int valueX = 120;
  const int commentX = 200;

  auto printLine = [&](const String &label, const String &value, uint16_t color, const String &comment = "")
  {
    tft.setTextColor(color, TFT_BLACK); // foreground on black
    tft.setCursor(labelX, y);
    tft.print(label);
    tft.setCursor(valueX, y);
    tft.print(": ");
    tft.print(value);
    if (comment.length() > 0)
    {
      tft.setCursor(commentX, y);
      tft.print("(" + comment + ")");
    }
    y += lineSpacing;
  };

  // Color + comment logic
  auto kIndexColorComment = [](int k)
  {
    if (k >= 7)
      return std::make_pair(TFT_RED, "Severe");
    if (k >= 5)
      return std::make_pair(TFT_RED, "Storm Risk");
    if (k >= 4)
      return std::make_pair(TFT_ORANGE, "Unsettled");
    if (k >= 2)
      return std::make_pair(TFT_YELLOW, "Quiet");
    return std::make_pair(TFT_GREEN, "Very Quiet");
  };

  auto aIndexColorComment = [](int a)
  {
    if (a >= 30)
      return std::make_pair(TFT_RED, "Disturbed");
    if (a >= 20)
      return std::make_pair(TFT_ORANGE, "Unsettled");
    if (a >= 10)
      return std::make_pair(TFT_YELLOW, "Normal");
    return std::make_pair(TFT_GREEN, "Quiet");
  };

  auto solarFluxColorComment = [](int sfi)
  {
    if (sfi >= 150)
      return std::make_pair(TFT_GREEN, "Excellent");
    if (sfi >= 100)
      return std::make_pair(TFT_YELLOW, "Good");
    return std::make_pair(TFT_RED, "Poor");
  };

  auto xrayColorComment = [](const String &x)
  {
    if (x.startsWith("X"))
      return std::make_pair(TFT_RED, "Extreme");
    if (x.startsWith("M"))
      return std::make_pair(TFT_ORANGE, "Moderate");
    if (x.startsWith("C"))
      return std::make_pair(TFT_YELLOW, "Low");
    return std::make_pair(TFT_GREEN, "Quiet");
  };

  auto [sfColor, sfComment] = solarFluxColorComment(solarData.solarFlux);
  printLine("Solar Flux", String(solarData.solarFlux), sfColor, sfComment);

  auto [aColor, aComment] = aIndexColorComment(solarData.aIndex);
  printLine("A Index", String(solarData.aIndex), aColor, aComment);

  auto [kColor, kComment] = kIndexColorComment(solarData.kIndex);
  printLine("K Index", String(solarData.kIndex), kColor, kComment);

  printLine("K Index NT", solarData.kIndexNT, TFT_WHITE);

  auto [xrColor, xrComment] = xrayColorComment(solarData.xRay);
  printLine("X-Ray", solarData.xRay, xrColor, xrComment);

  printLine("Sunspots", String(solarData.sunspots), TFT_WHITE);
  printLine("Helium Line", String(solarData.heliumLine, 1), TFT_WHITE);
  printLine("Proton Flux", solarData.protonFlux, TFT_WHITE);
  printLine("Electron Flux", solarData.electronFlux, TFT_WHITE);
  printLine("Aurora", String(solarData.aurora), TFT_WHITE);
  printLine("Normalization", String(solarData.normalization, 2), TFT_WHITE);
  printLine("Lat Degree", String(solarData.latDegree, 2), TFT_WHITE);
  printLine("Solar Wind", String(solarData.solarWind, 1), TFT_WHITE);
}

void drawSolarSummaryPage2()
{
  int y = 13;
  int lineSpacing = 18;
  tft.fillScreen(TFT_BLACK);
  tft.setFreeFont(&UbuntuMono_Regular8pt7b);
  tft.setTextSize(1);

  const int labelX = 10;
  const int valueX = 120;
  const int commentX = 200;

  auto printLine = [&](const String &label, const String &value, uint16_t color = TFT_WHITE, const String &comment = "")
  {
    tft.setTextColor(color, TFT_BLACK);
    tft.setCursor(labelX, y);
    tft.print(label);
    tft.setCursor(valueX, y);
    tft.print(": ");
    tft.print(value);
    if (comment.length() > 0)
    {
      tft.setCursor(commentX, y);
      tft.print("(" + comment + ")");
    }
    y += lineSpacing;
  };

  // Color and comment logic for text conditions
  auto conditionColorComment = [](const String &cond)
  {
    if (cond.equalsIgnoreCase("Good"))
      return std::make_pair(TFT_GREEN, "Good");
    if (cond.equalsIgnoreCase("Fair"))
      return std::make_pair(TFT_YELLOW, "Fair");
    if (cond.equalsIgnoreCase("Poor"))
      return std::make_pair(TFT_RED, "Poor");
    if (cond.indexOf("Storm") >= 0)
      return std::make_pair(TFT_RED, "Storm");
    if (cond.indexOf("Unsettled") >= 0)
      return std::make_pair(TFT_ORANGE, "Unsettled");
    return std::make_pair(TFT_WHITE, "");
  };

  printLine("Mag Field", String(solarData.magneticField, 1), TFT_WHITE);

  auto [geoColor, geoComment] = conditionColorComment(solarData.geomagneticField);
  printLine("Geo Field", solarData.geomagneticField, geoColor, geoComment);

  auto [snrColor, snrComment] = conditionColorComment(solarData.signalNoise);
  printLine("S/N", solarData.signalNoise, snrColor, snrComment);

  printLine("foF2", solarData.fof2, TFT_WHITE);
  printLine("MUF Fact", solarData.mufFactor, TFT_WHITE);
  printLine("MUF", solarData.muf, TFT_WHITE);
}

void drawSolarSummaryPage3()
{
  int y = 20;
  int lineSpacing = 18;
  int paragraphSpacing = 6;

  tft.fillScreen(TFT_BLACK);
  tft.setFreeFont(&UbuntuMono_Regular8pt7b);
  tft.setTextSize(1);

  const int titleX = 10;
  const int resultX = 20;

  auto beautifyLocation = [](const String &raw) -> String
  {
    if (raw == "europe")
      return "Europe";
    if (raw == "north_america")
      return "North America";
    if (raw == "northern_hemi")
      return "Northern Hemisphere";
    if (raw == "europe_6m")
      return "Europe 6m";
    if (raw == "europe_4m")
      return "Europe 4m";
    return raw;
  };

  auto annotatePhenomenon = [](const String &name) -> String
  {
    if (name.equalsIgnoreCase("E-Skip"))
      return "E-Skip (Sporadic-E)";
    return name;
  };

  auto vhfColorComment = [](const String &val) -> std::pair<uint16_t, String>
  {
    if (val.equalsIgnoreCase("Band Open"))
      return {TFT_GREEN, "Excellent"};
    if (val.equalsIgnoreCase("Band Weak"))
      return {TFT_YELLOW, "Marginal"};
    if (val.equalsIgnoreCase("Band Closed"))
      return {TFT_RED, "No Propagation"};
    if (val.indexOf("ES") >= 0)
      return {TFT_GREEN, "Sporadic-E Active"};
    return {TFT_WHITE, ""};
  };

  auto printLine = [&](const String &title, const String &value, uint16_t color = TFT_WHITE, const String &comment = "")
  {
    tft.setTextColor(TFT_WHITE, TFT_BLACK); // title line always white
    tft.setCursor(titleX, y);
    tft.print(title);
    y += lineSpacing;

    tft.setTextColor(color, TFT_BLACK);
    tft.setCursor(resultX, y);
    tft.print(value);
    if (!comment.isEmpty())
    {
      tft.print("   (" + comment + ")");
    }
    y += lineSpacing + paragraphSpacing;
  };

  for (int i = 0; i < 5; i++)
  {
    if (solarData.vhfConditions[i].name.isEmpty())
      break;

    String name = annotatePhenomenon(solarData.vhfConditions[i].name);
    String location = beautifyLocation(solarData.vhfConditions[i].location);
    String condition = solarData.vhfConditions[i].condition;

    auto [color, comment] = vhfColorComment(condition);
    String title = name + " (" + location + ")";

    printLine(title, condition, color, comment);
  }
}

void drawIntroPage(bool forceDisplay)
{
  
  prefs.begin("solar", false);

  // Check user preference
  bool showAbout = prefs.getBool("showAbout", true);
  if (!forceDisplay && !showAbout)
  {
    prefs.end();
    return; // Skip page
  }

  tft.fillScreen(TFT_BLACK);
  tft.setFreeFont(&JetBrainsMono_Light7pt7b);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);

  int y = 10;
  int lineSpacing = 16;

  auto print = [&](const String &text, uint16_t color = TFT_WHITE)
  {
    tft.setTextColor(color);
    tft.setCursor(10, y);
    tft.print("   "); // 3-space indent
    tft.println(text);
    y += lineSpacing;
  };

  print("Solar & Band Data from:");
  print("https://www.hamqsl.com", TFT_YELLOW);
  print("");
  print("Maintained by:");
  print("Dr. Paul Herrman, N0NBH");
  print("");
  print("Free for non-commercial use");
  print("Refreshes every 15 minutes");
  print("");
  print("Courtesy of HB9IIU");
  print("Supporting the ham radio community!");
  print(""); // add a blank line

  // Final line without 3-space indent, aligned at x = 1
  tft.setTextColor(TFT_GOLD);
  tft.setCursor(1, y + 10);
  tft.println("Touch the screen now to hide this page");
  y += lineSpacing;

  // Wait for touch (anywhere)
  unsigned long timeout = millis() + 10000;
  while (millis() < timeout)
  {
    uint16_t tx, ty;
    if (tft.getTouch(&tx, &ty))
    {
      prefs.putBool("showAbout", false);
      break;
    }
    delay(20);
  }

  prefs.end();
}

void drawQRCode(const char *text, int x, int y, int scale) {
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(3)];
  qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, text);

  for (uint8_t row = 0; row < qrcode.size; row++) {
    for (uint8_t col = 0; col < qrcode.size; col++) {
      int color = qrcode_getModule(&qrcode, col, row) ? TFT_BLACK : TFT_WHITE;
      tft.fillRect(x + col * scale, y + row * scale, scale, scale, color);
    }
  }
}
void drawQRcodeInstructions(){;
 // Draw QR instructions 
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString("Time Wi-Fi Configuration", 160, 10, 4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawCentreString("1", 80, 38, 4);
 tft.drawCentreString("2", 160+80, 38, 4);
  tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.drawCentreString("Scan to Join", 80, 85, 2);
drawQRCode("WIFI:T:nopass;S:HB9IIUSetup;;", 80 - 116 / 2, 105, 4);

  tft.drawCentreString("Open config page", 240, 85, 2);
  drawQRCode("http://192.168.4.1", 240 - 116 / 2, 105, 4);
 }
void startConfigurationPortal() {
  Serial.println("🌐 Starting Captive Portal...");
drawQRcodeInstructions();
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("HB9IIUSetup");
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  Serial.println("📡 Scanning for networks...");
scanCount = WiFi.scanNetworks();
  Serial.printf("📶 Found %d networks\n", scanCount);

  // Routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });

  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "[";
    for (int i = 0; i < scanCount; i++) {
      if (i > 0) json += ",";
      json += "\"" + WiFi.SSID(i) + "\"";
    }
    json += "]";
    request->send(200, "application/json", json);
  });
server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
  if (request->hasParam("ssid", true) &&
      request->hasParam("password", true) &&
      request->hasParam("time", true)) {

    String ssid = request->getParam("ssid", true)->value();
    String pass = request->getParam("password", true)->value();
    String timeStr = request->getParam("time", true)->value();  // "14:35"

    prefs.begin("wifi", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();

    prefs.begin("time", false);
    prefs.putString("localTime", timeStr);  // Save user’s local time string
    prefs.end();

    Serial.printf("✅ Saved WiFi and phone time: %s\n", timeStr.c_str());

    request->send(200, "text/html", "<h3>✅ WiFi and time saved. Rebooting...</h3>");
    delay(1000);
    ESP.restart();
  } else {
    request->send(400, "text/plain", "Missing fields.");
  }
});

  server.begin();
  Serial.println("🚀 Web server started.");
  // 🔁 Wait here until Wi-Fi is connected
while (WiFi.status() != WL_CONNECTED) {
  delay(500);
}

esp_restart();
}
bool tryConnectSavedWiFi() {
  Serial.println("🔍 Attempting to load saved WiFi credentials...");

  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();

  if (ssid.isEmpty() || pass.isEmpty()) {
    Serial.println("⚠️ No saved credentials found.");
    return false;
  }

  Serial.printf("📡 Found SSID: %s\n", ssid.c_str());
  Serial.printf("🔐 Found Password: %s\n", pass.c_str()); 

  Serial.printf("🔌 Connecting to WiFi: %s...\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), pass.c_str());

  for (int i = 0; i < 20; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("✅ Connected to WiFi!");
      Serial.print("📶 IP Address: ");
      Serial.println(WiFi.localIP());
      return true;
    }
    Serial.print(".");
    delay(500);
  }
  Serial.println("\n❌ Failed to connect to saved WiFi.");
  startConfigurationPortal();
}

