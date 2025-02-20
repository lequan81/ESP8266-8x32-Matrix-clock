/*
 * ESP8266 NTP Clock is written by John Rogers.
 * john@wizworks.net
 * 
 * Permission for use is free to all, with the only condition that this notice is intact within
 * all copies or derivatives of this code.  Improvements are welcome by pull request on the github
 * master repo.  
 * 
 * ESP8266 NTP Clock:  https://github.com/K1WIZ/ESP8266-8x32-Matrix-clock
 * 
 */
#include "Arduino.h"
#include <ESP8266WiFi.h>  //ESP8266 Core WiFi Library (you most likely already have this in your sketch)
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <NTPClient.h>
#include <WiFiUdp.h>  // For NTP Client
// =============================DEFINE VARS==============================
#define MAX_DIGITS 16
byte dig[MAX_DIGITS] = { 0 };
byte digold[MAX_DIGITS] = { 0 };
byte digtrans[MAX_DIGITS] = { 0 };
#define IS_12H false
int updCnt = 0;
int dots = 0;
long dotTime = 0;
long clkTime = 0;
int dx = 0;
int dy = 0;
byte del = 0;
int h, m, s;
float utcOffset;  // UTC - Now set via WiFi Manager!
long epoch;
long localMillisAtUpdate;
int day, month, year, dayOfWeek;
int summerTime = 0;
int adjustedHour;
String clockHostname = "NTP-Clock";
const int utcOffsetInSeconds = utcOffset * 3600;  
WiFiManagerParameter custom_utc_offset("utcoffset", "UTC Offset", String(utcOffset).c_str(), 10);

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

#define NUM_MAX 4

// for NodeMCU 1.0
#define DIN_PIN 15  // D8
#define CS_PIN 13   // D7
#define CLK_PIN 12  // D6
#include "max7219.h"
#include "fonts.h"
#define UTC_OFFSET_ADDRESS 0

void setup() {
  Serial.begin(115200);
  // Initialize EEPROM
  EEPROM.begin(sizeof(float));
  WiFiManager wifiManager;
  wifiManager.setSaveParamsCallback(saveConfigCallback);
  wifiManager.addParameter(&custom_utc_offset);
  WiFi.mode(WIFI_STA);
  initMAX7219();
  sendCmdAll(CMD_SHUTDOWN, 1);
  sendCmdAll(CMD_INTENSITY, 0);
  Serial.print("Connecting WiFi ");
  WiFi.hostname(clockHostname.c_str());

  if (!wifiManager.autoConnect("NTP Clock Setup")) {
    Serial.println("Failed to connect and hit timeout");
    delay(3000);
    ESP.restart();
    delay(5000);
  }

  utcOffset = atof(custom_utc_offset.getValue());

  printStringWithShift("Connecting", 15);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // Read the utcOffset value from EEPROM
  EEPROM.get(UTC_OFFSET_ADDRESS, utcOffset);

  Serial.println("");
  Serial.print("MyIP: ");
  Serial.println(WiFi.localIP());
  Serial.print("UTC Offset: ");
  Serial.println(utcOffset);
  printStringWithShift((String("  MyIP: ") + WiFi.localIP().toString()).c_str(), 15);
  delay(1500);
  // Start NTP Client
  timeClient.begin();
}

// =======================================================================

void saveConfigCallback() {
  utcOffset = atof(custom_utc_offset.getValue());

  // Store the utcOffset value in EEPROM
  EEPROM.put(UTC_OFFSET_ADDRESS, utcOffset);
  EEPROM.commit();
}

// =======================================================================
void loop() {
  if (updCnt <= 0) {  // every 10 scrolls, ~450s=7.5m
    updCnt = 60;
    Serial.println("Getting data ...");
    printStringWithShift("   Setting Time...", 15);
    getTime();
    Serial.println("Data loaded");
    clkTime = millis();
  }

  if (millis() - clkTime > 20000 && !del && dots) {  // clock for 15s, then scrolls for about 30s
    updCnt--;
    clkTime = millis();
  }
  if (millis() - dotTime > 500) {
    dotTime = millis();
    dots = !dots;
  }
  updateTime(epoch, localMillisAtUpdate);
  setIntensity(h);
  showAnimClock();
  //showSimpleClock();
}

// =======================================================================
/*
void setIntensity(int h) {
  if ((h >= 22 || h <= 6)) {
    sendCmdAll(CMD_INTENSITY, 0);
  }
  else if ((h >= 7 && h <= 10) || (h >= 16 && h < 18)) {
    sendCmdAll(CMD_INTENSITY, 3);
  }
  else if (h >= 11 && h <= 15) {
    sendCmdAll(CMD_INTENSITY, 5);
  }
  else if (h >= 19 && h <= 22) {
    sendCmdAll(CMD_INTENSITY, 2);
  }
}
*/
void setIntensity(int h) {
  if ((adjustedHour >= 22 || adjustedHour <= 6)) {
    sendCmdAll(CMD_INTENSITY, 0);
  }
  else if ((adjustedHour >= 7 && adjustedHour <= 10) || (adjustedHour >= 16 && adjustedHour < 18)) {
    sendCmdAll(CMD_INTENSITY, 3);
  }
  else if (adjustedHour >= 11 && adjustedHour <= 15) {
    sendCmdAll(CMD_INTENSITY, 5);
  }
  else if (adjustedHour >= 19 && adjustedHour <= 22) {
    sendCmdAll(CMD_INTENSITY, 2);
  }
  //Serial.println(adjustedHour);
}


// =======================================================================

void showSimpleClock() {
  dx = dy = 0;
  clr();
  if (IS_12H) {
    showDigit(h / 10 ? h / 10 : 10, 0, dig6x8);  //12H Mode
  } else {
    showDigit(h / 10, 0, dig6x8);
  }
  showDigit(h % 10, 8, dig6x8);
  showDigit(m / 10, 17, dig6x8);
  showDigit(m % 10, 25, dig6x8);
  showDigit(s / 10, 34, dig6x8);
  showDigit(s % 10, 42, dig6x8);
  setCol(15, dots ? B00100100 : 0);
  setCol(32, dots ? B00100100 : 0);
  refreshAll();
}

// =======================================================================

void showAnimClock() {
  byte digPos[6] = { 0, 8, 17, 25, 34, 42 };
  int digHt = 12;
  int num = 6;
  int i;
  if (del == 0) {
    del = digHt;
    for (i = 0; i < num; i++) digold[i] = dig[i];

    if (IS_12H) {
      dig[0] = h / 10 ? h / 10 : 10;  //12H Mode
    } else {
      dig[0] = h / 10;
    }
    dig[1] = h % 10;
    dig[2] = m / 10;
    dig[3] = m % 10;
    dig[4] = s / 10;
    dig[5] = s % 10;
    for (i = 0; i < num; i++) digtrans[i] = (dig[i] == digold[i]) ? 0 : digHt;
  } else
    del--;

  clr();
  for (i = 0; i < num; i++) {
    if (digtrans[i] == 0) {
      dy = 0;
      showDigit(dig[i], digPos[i], dig6x8);
    } else {
      dy = digHt - digtrans[i];
      showDigit(digold[i], digPos[i], dig6x8);
      dy = -digtrans[i];
      showDigit(dig[i], digPos[i], dig6x8);
      digtrans[i]--;
    }
  }
  dy = 0;
  setCol(15, dots ? B00100100 : 0);
  setCol(32, dots ? B00100100 : 0);
  refreshAll();
  delay(30);
}

// =======================================================================

void showDigit(char ch, int col, const uint8_t* data) {
  if (dy<-8 | dy> 8) return;
  int len = pgm_read_byte(data);
  int w = pgm_read_byte(data + 1 + ch * len);
  col += dx;
  for (int i = 0; i < w; i++)
    if (col + i >= 0 && col + i < 8 * NUM_MAX) {
      byte v = pgm_read_byte(data + 1 + ch * len + 1 + i);
      if (!dy) scr[col + i] = v;
      else scr[col + i] |= dy > 0 ? v >> dy : v << -dy;
    }
}

// =======================================================================

void setCol(int col, byte v) {
  if (dy<-8 | dy> 8) return;
  col += dx;
  if (col >= 0 && col < 8 * NUM_MAX)
    if (!dy) scr[col] = v;
    else scr[col] |= dy > 0 ? v >> dy : v << -dy;
}

// =======================================================================

int showChar(char ch, const uint8_t* data) {
  int len = pgm_read_byte(data);
  int i, w = pgm_read_byte(data + 1 + ch * len);
  for (i = 0; i < w; i++)
    scr[NUM_MAX * 8 + i] = pgm_read_byte(data + 1 + ch * len + 1 + i);
  scr[NUM_MAX * 8 + i] = 0;
  return w;
}


// =======================================================================

void printCharWithShift(unsigned char c, int shiftDelay) {
  if (c < ' ' || c > '~' + 25) return;
  c -= 32;
  int w = showChar(c, font);
  for (int i = 0; i < w + 1; i++) {
    delay(shiftDelay);
    scrollLeft();
    refreshAll();
  }
}

// =======================================================================

void printStringWithShift(const char* s, int shiftDelay) {
  while (*s) {
    printCharWithShift(*s, shiftDelay);
    s++;
  }
}
// =======================================================================

long getTime() {
  timeClient.update();

  epoch = timeClient.getEpochTime();
  Serial.println(epoch);
  h = ((epoch % 86400L) / 3600) % 24;
  m = (epoch % 3600) / 60;
  s = epoch % 60;
  summerTime = checkSummerTime();
  //Serial.println(h);
  //Serial.println(m);
  //Serial.println(s);

  if (h + utcOffset + summerTime > 23) {
    if (++day > 31) {
      day = 1;
      month++;
    };  // needs better patch
    if (++dayOfWeek > 7) dayOfWeek = 1;
  }
  localMillisAtUpdate = millis();
  return epoch, localMillisAtUpdate;
}
// =======================================================================

int checkSummerTime() {
  if (month > 3 && month < 10) return 1;
  if (month == 3 && day >= 31 - (((5 * year / 4) + 4) % 7)) return 1;
  if (month == 10 && day < 31 - (((5 * year / 4) + 1) % 7)) return 1;
  return 0;
}
// =======================================================================
/*
void updateTime(long epoch, long localMillisAtUpdate) {
  epoch = epoch + ((millis() - localMillisAtUpdate) / 1000);
  h = ((epoch % 86400L) / 3600) % 24;
  m = (epoch % 3600) / 60;
  s = epoch % 60;
}
*/

void updateTime(long epoch, long localMillisAtUpdate) {
  epoch = epoch + (long)(utcOffset * 3600) + ((millis() - localMillisAtUpdate) / 1000);
  h = ((epoch % 86400L) / 3600) % 24;
  m = (epoch % 3600) / 60;
  s = epoch % 60;
  adjustedHour = h;
}

// =======================================================================
