#include "weather.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

static String urlEncode(const String& s) {
  String out; out.reserve(s.length() * 3);
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); ++i) {
    uint8_t c = (uint8_t)s[i];
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') || c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

static float    s_temp = NAN;
static char     s_desc[32] = "";
static bool     s_valid = false;
static uint32_t s_lastFetch = 0;
static int      s_lastCode = -1;
static uint32_t s_lastAttempt = 0;
static const uint32_t FETCH_INTERVAL = 600000; // 10 minutes

static String s_apiKey;
static String s_city;

void weather_begin() {
  Preferences prefs;
  prefs.begin("wifi", true);
  s_apiKey = prefs.getString("w_key", "");
  s_city   = prefs.getString("w_city", "");
  prefs.end();
}

void weather_update() {
  s_lastAttempt = millis();
  // Only fetch if WiFi connected in STA mode and we have config
  if (WiFi.status() != WL_CONNECTED) { s_lastCode = -2; return; }
  if (s_apiKey.isEmpty() || s_city.isEmpty()) { s_lastCode = -3; return; }

  uint32_t now = millis();
  if (s_valid && (now - s_lastFetch < FETCH_INTERVAL)) return;

  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?q="
             + urlEncode(s_city) + "&appid=" + urlEncode(s_apiKey) + "&units=metric&lang=fr";

  http.begin(url);
  http.setTimeout(5000);
  int code = http.GET();
  s_lastCode = code;

  if (code == 200) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      s_temp = doc["main"]["temp"].as<float>();
      const char* desc = doc["weather"][0]["description"];
      if (desc) {
        strncpy(s_desc, desc, sizeof(s_desc) - 1);
        s_desc[sizeof(s_desc) - 1] = '\0';
      }
      s_valid = true;
      s_lastFetch = now;
    } else {
      s_lastCode = -4;
    }
  }
  http.end();
}

int weather_lastCode() { return s_lastCode; }
uint32_t weather_lastAttempt() { return s_lastAttempt; }

float weather_temp()       { return s_temp; }
const char* weather_desc() { return s_desc; }
bool weather_valid()       { return s_valid; }
