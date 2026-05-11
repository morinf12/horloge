#include "menu.h"
#include "display.h"
#include "config.h"
#include <Preferences.h>
#include <Adafruit_ST7789.h>
#include <WiFi.h>

// ---- Layout constants --------------------------------------------------------
static const int16_t MENU_X  = 10;
static const int16_t MENU_Y  = 8;
static const int16_t ROW_H   = 30;
static const int16_t SCR_W   = 280;
static const int16_t SCR_H   = 240;

// ---- Menu hierarchy ----------------------------------------------------------
// Level 0: main menu (list of sub-menus)
// Level 1: sub-menu items (editable values)
enum MainItem : uint8_t {
  MAIN_JOUR = 0,
  MAIN_NUIT,
  MAIN_AFFICHAGE,
  MAIN_WIFI,
  MAIN_COUNT
};

static const char* s_mainLabels[] = {
  "Parametres jour",
  "Parametres nuit",
  "Affichage",
  "WiFi"
};

// Sub-menu: Jour
enum SubJour : uint8_t {
  SJ_HEURE = 0,
  SJ_COULEUR,
  SJ_LUMINOSITE,
  SJ_COUNT
};
static const char* s_jourLabels[] = { "Heure", "Couleur", "Luminosite" };

// Sub-menu: Nuit
enum SubNuit : uint8_t {
  SN_HEURE = 0,
  SN_COULEUR,
  SN_LUMINOSITE,
  SN_COUNT
};
static const char* s_nuitLabels[] = { "Heure", "Couleur", "Luminosite" };

// Sub-menu: Affichage
enum SubAffichage : uint8_t {
  SA_ICONES = 0,
  SA_RAINBOW,
  SA_RAINBOW_SPD,
  SA_ECO,
  SA_DIM_LEVEL,
  SA_ROTATION,
  SA_SECONDES,
  SA_METEO,
  SA_BATTERY,
  SA_ITALIC,
  SA_FORMAT_12H,
  SA_MENU_CHORD,
  SA_COUNT
};
static const char* s_affLabels[] = { "Icones sol/lune", "Arc-en-ciel", "Vitesse arc-en-ciel", "Mode eco", "Intensite dim", "Rotation 180", "Secondes", "Meteo", "Batterie", "Italique", "Format 12h", "Ouverture menu" };

// Sub-menu: WiFi
enum SubWifi : uint8_t {
  SW_STATUS = 0,
  SW_IP,
  SW_SSID,
  SW_HOSTNAME,
  SW_ACTIVER,
  SW_RESTART_AP,
  SW_REBOOT,
  SW_COUNT
};
static const char* s_wifiLabels[] = { "Status", "Adresse IP", "SSID", "Nom d'hote", "WiFi actif", "Redemarrer AP", "Redemarrer" };

// ---- Menu state --------------------------------------------------------------
static bool     s_active   = false;
static bool     s_dirty    = true;
static uint8_t  s_level    = 0;     // 0=main, 1=sub-menu
static uint8_t  s_mainCur  = 0;     // cursor in main menu
static uint8_t  s_subCur   = 0;     // cursor in sub-menu
static bool     s_editing  = false;
static uint8_t  s_subField = 0;

// Editable values
static uint16_t s_dayMin, s_nightMin;
static uint16_t s_dayFg,  s_nightFg;
static uint8_t  s_dayBl,  s_nightBl;
static bool     s_showIcons;
static bool     s_rainbow;
static uint8_t  s_rainbowSpeed;
static bool     s_ecoMode;
static uint8_t  s_dimLevel;
static bool     s_rotation180;
static bool     s_showSeconds;
static bool     s_showWeather;
static bool     s_showBattery;
static bool     s_italic;
static bool     s_h12;

// Live chord setting: false = A+DOWN required to open the menu (default),
// true = bare A press. Loaded from NVS at startup, persisted on saveAll().
static bool     s_openWithA = false;
// Working copy used while editing the menu.
static bool     s_openWithAEdit = false;

// ---- Color helpers ----------------------------------------------------------
static void rgb565_to_rgb(uint16_t c, uint8_t& r, uint8_t& g, uint8_t& b) {
  r = ((c >> 11) & 0x1F) << 3;
  g = ((c >> 5)  & 0x3F) << 2;
  b = (c & 0x1F) << 3;
}

static uint16_t rgb_to_565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

// Hue (0..359) -> RGB565, full saturation & value.
static uint16_t hue_to_565(uint16_t h) {
  h %= 360;
  uint8_t sector = h / 60;
  uint8_t f = (h % 60) * 255 / 60; // 0..255
  uint8_t p = 0;
  uint8_t q = 255 - f;
  uint8_t t = f;
  uint8_t r=0, g=0, b=0;
  switch (sector) {
    case 0: r=255; g=t;   b=p;   break;
    case 1: r=q;   g=255; b=p;   break;
    case 2: r=p;   g=255; b=t;   break;
    case 3: r=p;   g=q;   b=255; break;
    case 4: r=t;   g=p;   b=255; break;
    case 5: r=255; g=p;   b=q;   break;
  }
  return rgb_to_565(r, g, b);
}

// RGB565 -> hue (0..359). For grayscale colors, returns 0.
static uint16_t rgb565_to_hue(uint16_t c) {
  uint8_t r, g, b;
  rgb565_to_rgb(c, r, g, b);
  uint8_t mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
  uint8_t mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
  if (mx == mn) return 0;
  int d = mx - mn;
  int h;
  if (mx == r)      h = ((int)(g - b) * 60) / d;
  else if (mx == g) h = ((int)(b - r) * 60) / d + 120;
  else              h = ((int)(r - g) * 60) / d + 240;
  if (h < 0) h += 360;
  return (uint16_t)(h % 360);
}

// ---- Preferences save -------------------------------------------------------

static void saveAll() {
  Preferences prefs;
  prefs.begin("wifi", false);
  prefs.putUShort("day_m",   s_dayMin);
  prefs.putUShort("night_m", s_nightMin);
  prefs.putUShort("day_c",   s_dayFg);
  prefs.putUShort("night_c", s_nightFg);
  prefs.putUChar("day_bl",   s_dayBl);
  prefs.putUChar("night_bl", s_nightBl);
  prefs.putBool("icons",     s_showIcons);
  prefs.putBool("rainbow",   s_rainbow);
  prefs.putUChar("rb_spd",   s_rainbowSpeed);
  prefs.putBool("eco",       s_ecoMode);
  prefs.putUChar("dim_lvl",  s_dimLevel);
  prefs.putBool("rot180",    s_rotation180);
  prefs.putBool("showSec",   s_showSeconds);
  prefs.putBool("showWx",    s_showWeather);
  prefs.putBool("italic",    s_italic);
  prefs.putBool("h12",       s_h12);
  prefs.putBool("showBat",   s_showBattery);
  prefs.putBool("menuA",     s_openWithAEdit);
  prefs.end();

  s_openWithA = s_openWithAEdit;

  display_setSchedule(s_dayMin, s_nightMin);
  display_setColors(s_dayFg, s_nightFg);
  display_setBacklight(s_dayBl, s_nightBl);
  display_setShowIcons(s_showIcons);
  display_setRainbow(s_rainbow);
  display_setEcoMode(s_ecoMode);
  display_setDimLevel(s_dimLevel);
  display_setRotation180(s_rotation180);
  display_setShowSeconds(s_showSeconds);
  display_setShowWeather(s_showWeather);
  display_setItalic(s_italic);
  display_setShowBattery(s_showBattery);
}

// ---- Sub-menu item count helper ---------------------------------------------
static uint8_t subCount() {
  switch (s_mainCur) {
    case MAIN_JOUR:      return SJ_COUNT;
    case MAIN_NUIT:      return SN_COUNT;
    case MAIN_AFFICHAGE: return SA_COUNT;
    case MAIN_WIFI:      return SW_COUNT;
    default:             return 0;
  }
}

static const char** subLabels() {
  switch (s_mainCur) {
    case MAIN_JOUR:      return s_jourLabels;
    case MAIN_NUIT:      return s_nuitLabels;
    case MAIN_AFFICHAGE: return s_affLabels;
    case MAIN_WIFI:      return s_wifiLabels;
    default:             return nullptr;
  }
}

static const char* subTitle() {
  return s_mainLabels[s_mainCur];
}

// ---- Public API -------------------------------------------------------------
void menu_begin() {
  Preferences p;
  p.begin("wifi", true);
  s_openWithA = p.getBool("menuA", false);
  p.end();
}

bool menu_getOpenWithA() { return s_openWithA; }
void menu_setOpenWithA(bool on) { s_openWithA = on; }

bool menu_isActive() { return s_active; }

static void openMenu();
void menu_open() { if (!s_active) openMenu(); }

static void openMenu() {
  s_active   = true;
  s_dirty    = true;
  s_level    = 0;
  s_mainCur  = 0;
  s_subCur   = 0;
  s_editing  = false;
  s_subField = 0;
  // Load current values
  s_dayMin    = display_getDayMin();
  s_nightMin  = display_getNightMin();
  s_dayFg     = display_getDayFg();
  s_nightFg   = display_getNightFg();
  s_dayBl     = display_getDayBl();
  s_nightBl   = display_getNightBl();
  s_showIcons = display_getShowIcons();
  s_rainbow   = display_getRainbow();
  s_rainbowSpeed = display_getRainbowSpeed();
  s_ecoMode   = display_getEcoMode();
  s_dimLevel  = display_getDimLevel();
  s_rotation180 = display_getRotation180();
  s_showSeconds = display_getShowSeconds();
  s_showWeather = display_getShowWeather();
  s_italic      = display_getItalic();
  s_h12         = display_get12h();
  s_showBattery = display_getShowBattery();
  s_openWithAEdit = s_openWithA;
}

static void closeMenu() {
  s_active  = false;
  s_editing = false;
  saveAll();
  display_resetClock();
}

// ---- Value adjustment -------------------------------------------------------
static uint8_t maxSubFields() {
  if (s_mainCur == MAIN_JOUR) {
    if (s_subCur == SJ_HEURE)    return 2;
    if (s_subCur == SJ_COULEUR)  return 1;
  } else if (s_mainCur == MAIN_NUIT) {
    if (s_subCur == SN_HEURE)    return 2;
    if (s_subCur == SN_COULEUR)  return 1;
  }
  return 1;
}

static void adjustValue(int8_t dir) {
  if (s_mainCur == MAIN_JOUR) {
    switch (s_subCur) {
      case SJ_HEURE:
        if (s_subField == 0) {
          int h = s_dayMin / 60 + dir;
          if (h < 0) h = 23; if (h > 23) h = 0;
          s_dayMin = h * 60 + s_dayMin % 60;
        } else {
          int m = (s_dayMin % 60) + dir * 5;
          if (m < 0) m = 55; if (m > 55) m = 0;
          s_dayMin = (s_dayMin / 60) * 60 + m;
        }
        display_setSchedule(s_dayMin, s_nightMin);
        break;
      case SJ_COULEUR: {
        int h = (int)rgb565_to_hue(s_dayFg) + dir * 10;
        while (h < 0)   h += 360;
        while (h >= 360) h -= 360;
        s_dayFg = hue_to_565((uint16_t)h);
        display_setColors(s_dayFg, s_nightFg);
        break;
      }
      case SJ_LUMINOSITE: {
        int v = s_dayBl + dir * 5;
        if (v < 1) v = 1; if (v > 100) v = 100;
        s_dayBl = v;
        display_setBacklight(s_dayBl, s_nightBl);
        break;
      }
    }
  } else if (s_mainCur == MAIN_NUIT) {
    switch (s_subCur) {
      case SN_HEURE:
        if (s_subField == 0) {
          int h = s_nightMin / 60 + dir;
          if (h < 0) h = 23; if (h > 23) h = 0;
          s_nightMin = h * 60 + s_nightMin % 60;
        } else {
          int m = (s_nightMin % 60) + dir * 5;
          if (m < 0) m = 55; if (m > 55) m = 0;
          s_nightMin = (s_nightMin / 60) * 60 + m;
        }
        display_setSchedule(s_dayMin, s_nightMin);
        break;
      case SN_COULEUR: {
        int h = (int)rgb565_to_hue(s_nightFg) + dir * 10;
        while (h < 0)   h += 360;
        while (h >= 360) h -= 360;
        s_nightFg = hue_to_565((uint16_t)h);
        display_setColors(s_dayFg, s_nightFg);
        break;
      }
      case SN_LUMINOSITE: {
        int v = s_nightBl + dir * 5;
        if (v < 1) v = 1; if (v > 100) v = 100;
        s_nightBl = v;
        display_setBacklight(s_dayBl, s_nightBl);
        break;
      }
    }
  } else if (s_mainCur == MAIN_AFFICHAGE) {
    if (s_subCur == SA_ICONES) {
      s_showIcons = !s_showIcons;
      display_setShowIcons(s_showIcons);
    } else if (s_subCur == SA_RAINBOW) {
      s_rainbow = !s_rainbow;
      display_setRainbow(s_rainbow);
    } else if (s_subCur == SA_RAINBOW_SPD) {
      int v = s_rainbowSpeed + dir;
      if (v < 1) v = 1; if (v > 32) v = 32;
      s_rainbowSpeed = v;
      display_setRainbowSpeed(s_rainbowSpeed);
    } else if (s_subCur == SA_ECO) {
      s_ecoMode = !s_ecoMode;
      display_setEcoMode(s_ecoMode);
    } else if (s_subCur == SA_DIM_LEVEL) {
      int v = s_dimLevel + dir * 5;
      if (v < 1) v = 1; if (v > 100) v = 100;
      s_dimLevel = v;
      display_setDimLevel(s_dimLevel);
    } else if (s_subCur == SA_ROTATION) {
      s_rotation180 = !s_rotation180;
      display_setRotation180(s_rotation180);
    } else if (s_subCur == SA_SECONDES) {
      s_showSeconds = !s_showSeconds;
      display_setShowSeconds(s_showSeconds);
    } else if (s_subCur == SA_METEO) {
      s_showWeather = !s_showWeather;
      display_setShowWeather(s_showWeather);
    } else if (s_subCur == SA_BATTERY) {
      s_showBattery = !s_showBattery;
      display_setShowBattery(s_showBattery);
    } else if (s_subCur == SA_ITALIC) {
      s_italic = !s_italic;
      display_setItalic(s_italic);
    } else if (s_subCur == SA_FORMAT_12H) {
      s_h12 = !s_h12;
      display_set12h(s_h12);
    } else if (s_subCur == SA_MENU_CHORD) {
      s_openWithAEdit = !s_openWithAEdit;
    }
  } else if (s_mainCur == MAIN_WIFI) {
    if (s_subCur == SW_ACTIVER) {
      if (WiFi.getMode() == WIFI_OFF) {
        // Re-enable: try stored STA, else AP
        Preferences prefs;
        prefs.begin("wifi", true);
        String ssid = prefs.getString("ssid", "");
        String pass = prefs.getString("pass", "");
        prefs.end();
        if (ssid.length()) {
          WiFi.mode(WIFI_STA);
          WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);
        } else {
          WiFi.mode(WIFI_AP);
          WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS, WIFI_AP_CHAN);
        }
      } else {
        WiFi.disconnect(true, true);
        WiFi.mode(WIFI_OFF);
      }
    } else if (s_subCur == SW_RESTART_AP) {
      WiFi.disconnect(true, true);
      WiFi.mode(WIFI_AP);
      WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS, WIFI_AP_CHAN);
    } else if (s_subCur == SW_REBOOT) {
      delay(200);
      ESP.restart();
    }
  }
  s_dirty = true;
}

// ---- Button handling --------------------------------------------------------
void menu_handleButton(Button btn) {
  if (!s_active) {
    // Open chord: either A+DOWN (default) or bare A, per user setting.
    if (s_openWithA) {
      if (btn == BTN_A) openMenu();
    } else {
      if (btn == BTN_A && buttons_isHeld(BTN_DOWN)) openMenu();
      else if (btn == BTN_DOWN && buttons_isHeld(BTN_A)) openMenu();
    }
    return;
  }

  if (s_level == 0) {
    // Main menu navigation
    switch (btn) {
      case BTN_B:
        closeMenu();
        break;
      case BTN_UP:
        s_mainCur = (s_mainCur > 0) ? s_mainCur - 1 : MAIN_COUNT - 1;
        s_dirty = true;
        break;
      case BTN_DOWN:
        s_mainCur = (s_mainCur < MAIN_COUNT - 1) ? s_mainCur + 1 : 0;
        s_dirty = true;
        break;
      case BTN_A:
      case BTN_RIGHT:
        s_level = 1;
        s_subCur = 0;
        s_editing = false;
        s_dirty = true;
        break;
      default: break;
    }
  } else {
    // Sub-menu
    if (!s_editing) {
      switch (btn) {
        case BTN_B:
        case BTN_LEFT:
          s_level = 0;
          s_dirty = true;
          break;
        case BTN_UP:
          s_subCur = (s_subCur > 0) ? s_subCur - 1 : subCount() - 1;
          s_dirty = true;
          break;
        case BTN_DOWN:
          s_subCur = (s_subCur < subCount() - 1) ? s_subCur + 1 : 0;
          s_dirty = true;
          break;
        case BTN_A:
        case BTN_RIGHT:
          // WiFi info items are read-only
          if (s_mainCur == MAIN_WIFI && (s_subCur == SW_STATUS || s_subCur == SW_IP || s_subCur == SW_SSID || s_subCur == SW_HOSTNAME)) {
            break;
          }
          // WiFi action items trigger immediately
          if (s_mainCur == MAIN_WIFI && (s_subCur == SW_ACTIVER || s_subCur == SW_RESTART_AP)) {
            adjustValue(+1);
            break;
          }
          s_editing = true;
          s_subField = 0;
          s_dirty = true;
          break;
        default: break;
      }
    } else {
      // Editing a value
      switch (btn) {
        case BTN_B:
          s_editing = false;
          s_dirty = true;
          break;
        case BTN_UP:
          adjustValue(+1);
          break;
        case BTN_DOWN:
          adjustValue(-1);
          break;
        case BTN_LEFT:
          if (s_subField > 0) { s_subField--; s_dirty = true; }
          break;
        case BTN_RIGHT:
          if (s_subField < maxSubFields() - 1) { s_subField++; s_dirty = true; }
          break;
        case BTN_A:
          s_editing = false;
          s_dirty = true;
          break;
        default: break;
      }
    }
  }
}

// ---- Format helpers ---------------------------------------------------------
static void formatTime(char* buf, uint16_t mins, uint8_t subF, bool editing) {
  int h = mins / 60, m = mins % 60;
  if (editing) {
    if (subF == 0) sprintf(buf, "[%02d]:%02d", h, m);
    else           sprintf(buf, "%02d:[%02d]", h, m);
  } else {
    sprintf(buf, "%02d:%02d", h, m);
  }
}

static void formatColor(char* buf, uint16_t c565, uint8_t subF, bool editing) {
  (void)subF;
  uint16_t h = rgb565_to_hue(c565);
  if (editing) sprintf(buf, "[%3u\xF8]", h);
  else         sprintf(buf, " %3u\xF8",  h);
}

static void formatPct(char* buf, uint8_t pct, bool editing) {
  if (editing) sprintf(buf, "[%3d%%]", pct);
  else         sprintf(buf, "%3d%%", pct);
}

// Get formatted value string for current sub-menu item
static void getItemValue(uint8_t idx, bool editing, char* buf) {
  if (s_mainCur == MAIN_JOUR) {
    switch (idx) {
      case SJ_HEURE:      formatTime(buf, s_dayMin, s_subField, editing); return;
      case SJ_COULEUR:    formatColor(buf, s_dayFg, s_subField, editing); return;
      case SJ_LUMINOSITE: formatPct(buf, s_dayBl, editing); return;
    }
  } else if (s_mainCur == MAIN_NUIT) {
    switch (idx) {
      case SN_HEURE:      formatTime(buf, s_nightMin, s_subField, editing); return;
      case SN_COULEUR:    formatColor(buf, s_nightFg, s_subField, editing); return;
      case SN_LUMINOSITE: formatPct(buf, s_nightBl, editing); return;
    }
  } else if (s_mainCur == MAIN_AFFICHAGE) {
    if (idx == SA_ICONES) {
      strcpy(buf, s_showIcons ? "OUI" : "NON");
      return;
    } else if (idx == SA_RAINBOW) {
      strcpy(buf, s_rainbow ? "OUI" : "NON");
      return;
    } else if (idx == SA_RAINBOW_SPD) {
      if (editing) sprintf(buf, "[%2d]", s_rainbowSpeed);
      else         sprintf(buf, "%2d", s_rainbowSpeed);
      return;
    } else if (idx == SA_ECO) {
      strcpy(buf, s_ecoMode ? "OUI" : "NON");
      return;
    } else if (idx == SA_DIM_LEVEL) {
      formatPct(buf, s_dimLevel, editing);
      return;
    } else if (idx == SA_ROTATION) {
      strcpy(buf, s_rotation180 ? "OUI" : "NON");
      return;
    } else if (idx == SA_SECONDES) {
      strcpy(buf, s_showSeconds ? "OUI" : "NON");
      return;
    } else if (idx == SA_METEO) {
      strcpy(buf, s_showWeather ? "OUI" : "NON");
      return;
    } else if (idx == SA_BATTERY) {
      strcpy(buf, s_showBattery ? "OUI" : "NON");
      return;
    } else if (idx == SA_ITALIC) {
      strcpy(buf, s_italic ? "OUI" : "NON");
      return;
    } else if (idx == SA_FORMAT_12H) {
      strcpy(buf, s_h12 ? "12h" : "24h");
      return;
    } else if (idx == SA_MENU_CHORD) {
      strcpy(buf, s_openWithAEdit ? "A" : "A+v");
      return;
    }
  } else if (s_mainCur == MAIN_WIFI) {
    switch (idx) {
      case SW_STATUS: {
        wifi_mode_t mode = WiFi.getMode();
        if (mode == WIFI_OFF)       strcpy(buf, "Desactive");
        else if (mode == WIFI_AP)   strcpy(buf, "Point d'acces");
        else if (WiFi.status() == WL_CONNECTED) strcpy(buf, "Connecte");
        else                        strcpy(buf, "Deconnecte");
        return;
      }
      case SW_IP: {
        wifi_mode_t mode = WiFi.getMode();
        if (mode == WIFI_AP)
          strcpy(buf, WiFi.softAPIP().toString().c_str());
        else if (mode == WIFI_STA && WiFi.status() == WL_CONNECTED)
          strcpy(buf, WiFi.localIP().toString().c_str());
        else
          strcpy(buf, "---");
        return;
      }
      case SW_SSID: {
        wifi_mode_t mode = WiFi.getMode();
        if (mode == WIFI_AP)
          strcpy(buf, WIFI_AP_SSID);
        else if (mode == WIFI_STA)
          strncpy(buf, WiFi.SSID().c_str(), 20);
        else
          strcpy(buf, "---");
        buf[20] = '\0';
        return;
      }
      case SW_HOSTNAME: {
        const char* h = WiFi.getHostname();
        strncpy(buf, h ? h : "---", 20);
        buf[20] = '\0';
        return;
      }
      case SW_ACTIVER:
        strcpy(buf, (WiFi.getMode() != WIFI_OFF) ? "OUI" : "NON");
        return;
      case SW_RESTART_AP:
        strcpy(buf, "[Executer]");
        return;
    }
  }
  buf[0] = '\0';
}

// ---- Drawing ----------------------------------------------------------------
void menu_draw() {
  // WiFi sub-menu: check if live values changed and mark dirty only then
  if (s_active && s_level == 1 && s_mainCur == MAIN_WIFI && !s_editing) {
    static wl_status_t s_prevWlStatus = WL_IDLE_STATUS;
    static wifi_mode_t s_prevWlMode   = WIFI_OFF;
    wl_status_t curSt = WiFi.status();
    wifi_mode_t curMd = WiFi.getMode();
    if (curSt != s_prevWlStatus || curMd != s_prevWlMode) {
      s_prevWlStatus = curSt;
      s_prevWlMode   = curMd;
      s_dirty = true;
    }
  }

  if (!s_active || !s_dirty) return;

  Adafruit_ST7789& tft = display_getTft();
  tft.fillScreen(ST77XX_BLACK);
  tft.setFont(nullptr);

  // Hint bar at bottom
  tft.setTextSize(1);
  tft.setTextColor(0x7BEF);
  tft.setCursor(MENU_X, SCR_H - 12);
  if (s_level == 0)
    tft.print(F("UP/DN:naviguer  A/R:entrer  B:quitter"));
  else if (s_editing)
    tft.print(F("UP/DN:valeur  L/R:champ  B/A:ok"));
  else
    tft.print(F("UP/DN:naviguer  A/R:editer  B/L:retour"));

  if (s_level == 0) {
    // ---- Main menu ----
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(MENU_X, MENU_Y);
    tft.print(F("MENU"));

    int16_t y = MENU_Y + 30;
    for (uint8_t i = 0; i < MAIN_COUNT; i++) {
      bool sel = (i == s_mainCur);
      if (sel) {
        tft.fillRect(MENU_X - 2, y - 2, SCR_W - 2*MENU_X + 4, ROW_H, 0x1082);
      }
      tft.setTextSize(2);
      tft.setTextColor(sel ? ST77XX_WHITE : 0x9CF3);
      tft.setCursor(MENU_X + 10, y + 6);
      tft.print(s_mainLabels[i]);
      // Arrow indicator
      if (sel) {
        tft.setCursor(SCR_W - 30, y + 6);
        tft.print(F(">"));
      }
      y += ROW_H + 8;
    }
  } else {
    // ---- Sub-menu ----
    // Title
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(MENU_X, MENU_Y);
    tft.print(subTitle());

    // Separator line
    tft.drawFastHLine(MENU_X, MENU_Y + 22, SCR_W - 2*MENU_X, 0x3186);

    int16_t yStart = MENU_Y + 28;
    uint8_t count = subCount();
    const char** labels = subLabels();
    char valBuf[32];

    // Compact: label(size1) + value(size2) = 32px per row
    const int16_t rowH = 32;
    const int16_t editExtraH = 12;  // for RGB hint

    // Compute available height
    const int16_t availH = (SCR_H - 16) - yStart;
    uint8_t maxVisible = availH / rowH;
    if (maxVisible > count) maxVisible = count;

    // Scroll offset to keep selected item visible
    static uint8_t scrollOff = 0;
    if (s_subCur < scrollOff) scrollOff = s_subCur;
    if (s_subCur >= scrollOff + maxVisible) scrollOff = s_subCur - maxVisible + 1;
    if (scrollOff + maxVisible > count) scrollOff = count - maxVisible;

    // Draw scroll indicators
    if (scrollOff > 0) {
      tft.setTextSize(1);
      tft.setTextColor(0x7BEF);
      tft.setCursor(SCR_W - 16, MENU_Y + 24);
      tft.print(F("\x18"));
    }
    if (scrollOff + maxVisible < count) {
      tft.setTextSize(1);
      tft.setTextColor(0x7BEF);
      tft.setCursor(SCR_W - 16, SCR_H - 22);
      tft.print(F("\x19"));
    }

    int16_t y = yStart;
    for (uint8_t vi = 0; vi < maxVisible; vi++) {
      uint8_t i = scrollOff + vi;
      bool sel = (i == s_subCur);
      bool itemEdit = sel && s_editing;

      int16_t thisRowH = rowH;
      bool isColor = false;
      uint16_t swCol = 0;
      if (s_mainCur == MAIN_JOUR && i == SJ_COULEUR)  { isColor = true; swCol = s_dayFg; }
      if (s_mainCur == MAIN_NUIT && i == SN_COULEUR)  { isColor = true; swCol = s_nightFg; }
      if (itemEdit && isColor) thisRowH += editExtraH;

      // Selection highlight
      if (sel) {
        tft.fillRect(MENU_X - 2, y - 1, SCR_W - 2*MENU_X + 4, thisRowH, 0x1082);
      }

      // Label (small, top)
      tft.setTextSize(1);
      tft.setTextColor(sel ? ST77XX_WHITE : 0x9CF3);
      tft.setCursor(MENU_X + 4, y + 1);
      tft.print(labels[i]);

      // Value (size 2, below label)
      getItemValue(i, itemEdit, valBuf);
      tft.setTextSize(2);
      tft.setTextColor(itemEdit ? ST77XX_GREEN : ST77XX_YELLOW);
      tft.setCursor(MENU_X + 4, y + 12);
      tft.print(valBuf);

      // Color swatch (right side)
      if (isColor) {
        tft.fillRect(SCR_W - 30, y + 8, 18, 18, swCol);
        tft.drawRect(SCR_W - 30, y + 8, 18, 18, ST77XX_WHITE);
      }

      // RGB sub-field hint
      if (itemEdit && isColor) {
        tft.setTextSize(1);
        tft.setTextColor(0x7BEF);
        tft.setCursor(MENU_X + 4, y + rowH);
        tft.print(F("  R    G    B"));
      }

      y += thisRowH;
    }
  }

  s_dirty = false;
}

// ---- JSON state for debug web page ------------------------------------------
static void jsonEscape(String& out, const char* s) {
  while (*s) {
    if (*s == '"') out += "\\\"";
    else if (*s == '\\') out += "\\\\";
    else out += *s;
    s++;
  }
}

void menu_getStateJson(String& out) {
  out = "{\"active\":";
  out += s_active ? "true" : "false";

  if (!s_active) { out += "}"; return; }

  out += ",\"level\":";
  out += s_level;
  out += ",\"editing\":";
  out += s_editing ? "true" : "false";

  // Title
  out += ",\"title\":\"";
  if (s_level == 0) out += "MENU";
  else { jsonEscape(out, subTitle()); }
  out += "\"";

  // Hint
  out += ",\"hint\":\"";
  if (s_level == 0)
    out += "UP/DN:naviguer  A/R:entrer  B:quitter";
  else if (s_editing)
    out += "UP/DN:valeur  L/R:champ  B/A:ok";
  else
    out += "UP/DN:naviguer  A/R:editer  B/L:retour";
  out += "\"";

  // Items
  out += ",\"items\":[";
  if (s_level == 0) {
    for (uint8_t i = 0; i < MAIN_COUNT; i++) {
      if (i) out += ",";
      out += "{\"label\":\"";
      jsonEscape(out, s_mainLabels[i]);
      out += "\",\"value\":\"\",\"sel\":";
      out += (i == s_mainCur) ? "true" : "false";
      out += ",\"edit\":false}";
    }
  } else {
    uint8_t count = subCount();
    const char** labels = subLabels();
    char valBuf[32];
    for (uint8_t i = 0; i < count; i++) {
      bool sel = (i == s_subCur);
      bool itemEdit = sel && s_editing;
      getItemValue(i, itemEdit, valBuf);
      if (i) out += ",";
      out += "{\"label\":\"";
      jsonEscape(out, labels[i]);
      out += "\",\"value\":\"";
      jsonEscape(out, valBuf);
      out += "\",\"sel\":";
      out += sel ? "true" : "false";
      out += ",\"edit\":";
      out += itemEdit ? "true" : "false";

      // Color swatch
      bool isColor = false;
      uint16_t swCol = 0;
      if (s_mainCur == MAIN_JOUR && i == SJ_COULEUR)  { isColor = true; swCol = s_dayFg; }
      if (s_mainCur == MAIN_NUIT && i == SN_COULEUR)  { isColor = true; swCol = s_nightFg; }
      if (isColor) {
        uint8_t r, g, b;
        rgb565_to_rgb(swCol, r, g, b);
        char hex[8];
        sprintf(hex, "#%02X%02X%02X", r, g, b);
        out += ",\"color\":\"";
        out += hex;
        out += "\"";
      }
      out += "}";
    }
  }
  out += "]}";
}
