#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Arduino_DriveBus_Library.h>
#include <HWCDC.h>
#include <Wire.h>
#include <Dexcom.h>
#include <time.h>
#include <WiFiManager.h>
#include "pin_config.h"

#include "secrets.h"

HWCDC USBSerial;

#define DELAY_TIME 60000
Dexcom dexcom(dexcom_ous, USBSerial);

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS /* CS */, LCD_SCLK /* SCK */, LCD_SDIO0 /* SDIO0 */, LCD_SDIO1 /* SDIO1 */,
  LCD_SDIO2 /* SDIO2 */, LCD_SDIO3 /* SDIO3 */);

Arduino_SH8601 *gfx = new Arduino_SH8601(
    bus, GFX_NOT_DEFINED /* RST */, 0 /* rotation */, LCD_WIDTH /* width */, LCD_HEIGHT /* height */);

std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus =
  std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);

void Arduino_IIC_Touch_Interrupt(void);
void displayStatus(const char* message);
bool runConfigPortal();

std::unique_ptr<Arduino_IIC> FT3168(new Arduino_FT3x68(
    IIC_Bus, FT3168_DEVICE_ADDRESS,
    DRIVEBUS_DEFAULT_VALUE, TP_INT, Arduino_IIC_Touch_Interrupt));

void setup() {
  USBSerial.begin(115200);
  USBSerial.println("Arduino_GFX Hello World example");

  while (FT3168->begin() == false) {
    USBSerial.println("FT3168 initialization fail");
    delay(2000);
  }
  USBSerial.println("FT3168 initialization successfully");

#ifdef GFX_EXTRA_PRE_INIT
  GFX_EXTRA_PRE_INIT();
#endif

  // Init Display
  if (!gfx->begin())
  {
    USBSerial.println("gfx->begin() failed!");
  }
  gfx->fillScreen(RGB565_BLACK);

  gfx->setBrightness(255);

  gfx->setCursor(10, 10);
  gfx->setTextColor(RGB565_RED);
  gfx->setTextSize(3);

  if (!runConfigPortal()) {
    displayStatus("Failed to connect and hit timeout");
    while (true) {
      delay(1000);
    }
  }

  displayStatus("Getting time...");
  configTime(GMT_OFFSET_SEC, IS_DAYLIGHT_SAVINGS ? 3600 : 0, "pool.ntp.org");

  displayStatus("Connecting to Dexcom...");
  USBSerial.printf("Connecting to Dexcom account %s ", dexcom_username);
  dexcom.createSession(dexcom_username, dexcom_password);
  if (dexcom.accountStatus == DexcomStatus::LoggedIn) {
    displayStatus("Connected!");
  } else {
    switch (dexcom.accountStatus) {
        case DexcomStatus::SessionNotValid:   displayStatus("Session ID invalid"); break;
        case DexcomStatus::SessionNotFound:   displayStatus("Session not found"); break;
        case DexcomStatus::AccountNotFound:   displayStatus("Account not found"); break;
        case DexcomStatus::PasswordInvalid:   displayStatus("Password invalid"); break;
        case DexcomStatus::MaxAttempts:       displayStatus("Maximum authentication attempts exceeded"); break;
        case DexcomStatus::UsernameNullEmpty: displayStatus("Username NULL or empty"); break;
        case DexcomStatus::PasswordNullEmpty: displayStatus("Password NULL or empty"); break;
        default: displayStatus("Unknown error"); break;
    }
    // TODO: start config portal again?
  }
}

void loop() {
  if (FT3168->IIC_Interrupt_Flag == true) {
    FT3168->IIC_Interrupt_Flag = false;
    gfx->fillScreen(RGB565_BLACK);
  }

  if (dexcom.accountStatus != DexcomStatus::LoggedIn) {
    delay(DELAY_TIME);
    return;
  }

  displayStatus("Getting data...");

  GlucoseData d = dexcom.getLastGlucose();
  if (d.glucose == -1) {
    displayStatus("No glucose data");
    delay(DELAY_TIME);
    return;
  }
  USBSerial.print("Glucose: ");
  USBSerial.println(d.glucose);

  // trend to string
  const char* trendStr;
  switch (d.trend) {
    case GlucoseTrend::DoubleUp: trendStr = "Rising fast"; break;
    case GlucoseTrend::SingleUp: trendStr = "Rising"; break;
    case GlucoseTrend::FortyFiveUp: trendStr = "Slightly rising"; break;
    case GlucoseTrend::Flat: trendStr = "Steady :)"; break;
    case GlucoseTrend::FortyFiveDown: trendStr = "Slightly falling"; break;
    case GlucoseTrend::SingleDown: trendStr = "Falling"; break;
    case GlucoseTrend::DoubleDown: trendStr = "Falling fast"; break;
    case GlucoseTrend::NotComputable:
    case GlucoseTrend::RateOutOfRange:
    default:
      trendStr = "Not computable/Value out of range";
      break;
  }

  gfx->fillScreen(RGB565_BLACK);
  gfx->setCursor(0, gfx->height()/3);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setTextSize(4);
  gfx->println("Glucose:");
  gfx->setTextSize(6);
  gfx->printf("%d mg/dL\n", d.glucose);
  gfx->setTextSize(4);
  gfx->println(trendStr);

  gfx->setCursor(40, gfx->height() - 40);
  struct tm now;
  getLocalTime(&now);
  gfx->setTextSize(3);
  gfx->printf("%02d:%02d", now.tm_hour, now.tm_min);

  delay(DELAY_TIME);
}

void Arduino_IIC_Touch_Interrupt(void) {
  FT3168->IIC_Interrupt_Flag = true;
}

void displayStatus(const char* message) {
  // gfx->fillScreen(RGB565_BLACK);
  // gfx->setCursor(0, 0);
  USBSerial.println(message);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->println(message);
}

void wifiManagerCallback(WiFiManager *wm) {
  displayStatus("Config Portal SSID:\n");
  displayStatus(wm->getConfigPortalSSID().c_str());
}

bool runConfigPortal() {
  WiFiManager wm(USBSerial);
  wm.setAPCallback(wifiManagerCallback);
  // wm.setConfigPortalTimeout(180); // 3 minutes
  bool res;
  res = wm.autoConnect(); // auto generated AP name from chipid
  // res = wm.autoConnect(WM_AP, WM_PASSWORD);
  if (res) {
    displayStatus("WiFi connected!");
  } else {
    displayStatus("Failed to connect");
  }
  return res;
}
