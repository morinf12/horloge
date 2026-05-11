#include "display.h"
#include "config.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <time.h>
#include <Preferences.h>

// Backlight PWM
#define BL_PWM_CHANNEL  0
#define BL_PWM_FREQ     5000
#define BL_PWM_RES      8      // 0-255

static uint8_t s_dayBl   = DEFAULT_DAY_BL;
static uint8_t s_nightBl = DEFAULT_NIGHT_BL;
static uint8_t s_curBl   = DEFAULT_DAY_BL;

// Vertical shift applied to all drawing (panel offset compensation)
#define Y_OFFSET       20

static SPIClass s_spi(FSPI);   // ESP32-S2 has FSPI/HSPI; FSPI is the user SPI bus
static Adafruit_ST7789 tft(&s_spi, TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);

// Forward declarations of override state (defined later in the file).
static bool s_ovrActive = false;
static bool s_ovrNight = false;
static bool s_ovrNaturalAtSet = false;

// Forward declarations of indicator state (defined later in the file).
static int8_t s_lastBattPct = -1;
static float  s_lastTemp = -999.0f;

void display_begin() {
  // Setup backlight as PWM
  ledcSetup(BL_PWM_CHANNEL, BL_PWM_FREQ, BL_PWM_RES);
  ledcAttachPin(TFT_BL_PIN, BL_PWM_CHANNEL);
  ledcWrite(BL_PWM_CHANNEL, 255);   // full brightness for boot

  s_spi.begin(TFT_SCLK_PIN, -1, TFT_MOSI_PIN, TFT_CS_PIN);

  // Full hardware reset of the ST7789 controller. The MCU may have rebooted
  // (watchdog, software restart) while the panel kept its state, leaving GRAM
  // and registers in an inconsistent state. Pulsing the RST line clears that.
  pinMode(TFT_RST_PIN, OUTPUT);
  digitalWrite(TFT_RST_PIN, HIGH);
  delay(5);
  digitalWrite(TFT_RST_PIN, LOW);
  delay(20);                          // datasheet: tRW >= 10us, recommend >=10ms
  digitalWrite(TFT_RST_PIN, HIGH);
  delay(150);                         // datasheet: wait >=120ms after reset

  tft.init(TFT_WIDTH, TFT_HEIGHT);   // 240x280
  tft.setRotation(3);                // landscape 280x240
  tft.fillScreen(ST77XX_BLACK);

  // Restore persisted day/night override (set via UP button).
  Preferences p;
  p.begin("wifi", true);
  s_ovrActive       = p.getBool("ovrAct", false);
  s_ovrNight        = p.getBool("ovrNgt", false);
  s_ovrNaturalAtSet = p.getBool("ovrNat", false);
  p.end();
}

void display_showBoot(const char* hostname) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(3);
  tft.setCursor(50, 60);
  tft.println(F("Horloge"));
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(50, 120);
  tft.print(hostname);
}

void display_showIP(const char* ip) {
  tft.setTextWrap(false);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.setCursor(50, 170);
  tft.print(ip);
}

// ---- 7-segment clock (landscape 280x240, TTF font) -------------------------
#include "DSEG7_80.h"
#include "DSEG7_80_Italic.h"

// Rotation 3 → 280 wide × 240 tall
static const int LAND_W = 280;
static const int LAND_H = 240;

// Day / night color pairs
static uint16_t s_dayFg    = DEFAULT_DAY_FG;
static uint16_t s_dayDim   = DEFAULT_DAY_DIM;
static uint16_t s_nightFg  = DEFAULT_NIGHT_FG;
static uint16_t s_nightDim = DEFAULT_NIGHT_DIM;
static uint16_t s_dayMin   = DEFAULT_DAY_MIN;
static uint16_t s_nightMin = DEFAULT_NIGHT_MIN;

static uint16_t s_curFg  = DEFAULT_DAY_FG;
static uint16_t s_curDim = DEFAULT_DAY_DIM;

static char    s_lastClockStr[6] = "";   // "HH:MM" + NUL
static bool    s_lastColonOn     = true;
static bool    s_clockInited     = false;
static int8_t  s_lastNight       = -1;   // -1=unknown, 0=day, 1=night
static int8_t  s_lastSec         = -1;   // last drawn seconds
static bool    s_showIcons       = true;  // show sun/moon icons
static bool    s_rainbow         = false; // rainbow color cycling
static uint8_t s_rainbowHue      = 0;     // current hue 0-255
static uint8_t s_rainbowSpeed    = 1;     // hue increment per refresh (1..32)
static bool    s_ecoMode         = false; // power saving mode
static uint8_t s_dimLevel        = 25;    // dim digit intensity % (1-100)
static bool    s_italic          = false; // use italic DSEG7 variant

// Manual day/night override: when active, force the chosen mode until the
// natural (time-based) state would change at the next scheduled transition.
// (state vars declared near the top of this TU)

// Forward decl (definition lives further down in this TU)
static bool isNightTime(int hour, int minute);

// Pointer to the currently active large clock font
static inline const GFXfont* clockFont() {
  return s_italic ? &DSEG7_80_Italic : &DSEG7_80;
}
static bool    s_showSeconds     = true;  // show seconds display
static bool    s_showWeather     = true;  // show weather temperature
static bool    s_showBattery     = true;  // show battery indicator
static bool    s_h12             = false; // 12-hour format when true, else 24h
static bool    s_displaySleeping = false; // TFT in sleep mode

// Off-screen buffer for flicker-free clock updates
static GFXcanvas16* s_clockCanvas = nullptr;
static GFXcanvas16* s_secCanvas   = nullptr;  // seconds at full size (to be scaled 1/2)
static int16_t      s_canvasX = 0;    // screen position of canvas
static int16_t      s_canvasY = 0;
static int16_t      s_secDispX = 0;   // screen position of seconds (half-scale)
static int16_t      s_secDispY = 0;
static int16_t      s_secFullW = 0;   // full-size canvas width for seconds
static int16_t      s_secFullH = 0;   // full-size canvas height for seconds

void display_setSchedule(uint16_t dayMin, uint16_t nightMin) {
  s_dayMin  = dayMin;
  s_nightMin = nightMin;
}

// Derive a dim version of an RGB565 color using s_dimLevel percentage
static uint16_t dimColor(uint16_t c) {
  uint16_t r = (c >> 11) & 0x1F;
  uint16_t g = (c >> 5)  & 0x3F;
  uint16_t b =  c        & 0x1F;
  r = (r * s_dimLevel) / 100;
  g = (g * s_dimLevel) / 100;
  b = (b * s_dimLevel) / 100;
  return (r << 11) | (g << 5) | b;
}

void display_setColors(uint16_t dayFg, uint16_t nightFg) {
  s_dayFg    = dayFg;
  s_dayDim   = dimColor(dayFg);
  s_nightFg  = nightFg;
  s_nightDim = dimColor(nightFg);
}

void display_setBacklight(uint8_t dayPct, uint8_t nightPct) {
  if (dayPct > 100) dayPct = 100;
  if (nightPct > 100) nightPct = 100;
  s_dayBl  = dayPct;
  s_nightBl = nightPct;
}

// Getters
uint16_t display_getDayMin()  { return s_dayMin; }
uint16_t display_getNightMin(){ return s_nightMin; }
uint16_t display_getDayFg()   { return s_dayFg; }
uint16_t display_getNightFg() { return s_nightFg; }
uint8_t  display_getDayBl()   { return s_dayBl; }
uint8_t  display_getNightBl() { return s_nightBl; }
bool     display_getShowIcons(){ return s_showIcons; }
void     display_setShowIcons(bool show) { s_showIcons = show; }
bool     display_getRainbow() { return s_rainbow; }
void     display_setRainbow(bool on) { s_rainbow = on; }
uint8_t  display_getRainbowSpeed() { return s_rainbowSpeed; }
void     display_setRainbowSpeed(uint8_t v) {
  if (v < 1)  v = 1;
  if (v > 32) v = 32;
  s_rainbowSpeed = v;
}
bool     display_getItalic() { return s_italic; }
void     display_setItalic(bool on) {
  if (on == s_italic) return;
  s_italic = on;
  display_resetClock();   // force re-layout (italic has different metrics)
}

void display_toggleNightOverride() {
  // Compute current natural state from the clock
  struct tm ti;
  time_t now = time(nullptr);
  localtime_r(&now, &ti);
  bool validTime = (ti.tm_year >= 124);
  bool natural = validTime && isNightTime(ti.tm_hour, ti.tm_min);

  if (s_ovrActive) {
    // Already overriding -> cancel it
    s_ovrActive = false;
  } else {
    // Engage override forcing the opposite of the natural state
    s_ovrActive = true;
    s_ovrNight = !natural;
    s_ovrNaturalAtSet = natural;
  }
  // Force a redraw so the new colors / backlight take effect immediately
  s_lastNight = -1;
  s_lastClockStr[0] = '\0';

  // Persist so the override survives a reboot.
  Preferences p;
  p.begin("wifi", false);
  p.putBool("ovrAct",  s_ovrActive);
  p.putBool("ovrNgt",  s_ovrNight);
  p.putBool("ovrNat",  s_ovrNaturalAtSet);
  p.end();
}

bool display_isNightOverrideActive() { return s_ovrActive; }
bool     display_getEcoMode() { return s_ecoMode; }
void     display_setEcoMode(bool on) { s_ecoMode = on; }
uint8_t  display_getDimLevel() { return s_dimLevel; }
static bool s_rot180 = false;
bool     display_getRotation180() { return s_rot180; }
bool     display_getShowSeconds() { return s_showSeconds; }
void     display_setShowSeconds(bool on) {
  if (on == s_showSeconds) return;
  s_showSeconds = on;
  if (!on) {
    // Clear seconds area
    int16_t halfW = s_secFullW / 2;
    int16_t halfH = s_secFullH / 2;
    tft.fillRect(s_secDispX, s_secDispY, halfW, halfH, ST77XX_BLACK);
    s_lastSec = -1;
  }
}
bool     display_getShowWeather() { return s_showWeather; }
void     display_setShowWeather(bool on) {
  if (on == s_showWeather) return;
  s_showWeather = on;
  if (!on) {
    // Clear temperature area
    const int16_t tx = LAND_W - 158;
    const int16_t ty = LAND_H - 50;
    tft.fillRect(tx, ty, 140, 50, ST77XX_BLACK);
  }
}
bool     display_get12h() { return s_h12; }
void     display_set12h(bool on) {
  if (on == s_h12) return;
  s_h12 = on;
  display_resetClock();
}
bool     display_getShowBattery() { return s_showBattery; }
void     display_setShowBattery(bool on) {
  if (on == s_showBattery) return;
  s_showBattery = on;
  if (!on) {
    // Clear the indicator area (must mirror the geometry in display_showBattery).
    const int16_t bw = 28, bh = 12;
    const int16_t bx = LAND_W - bw - 8 - 14;
    const int16_t by = 6;
    const int16_t areaX = bx - 38;
    const int16_t areaW = LAND_W - areaX - 2;
    const int16_t areaH = bh + 4;
    tft.fillRect(areaX, by - 1, areaW, areaH, ST77XX_BLACK);
    s_lastBattPct = -1;
  } else {
    s_lastBattPct = -1;  // force redraw on next call
  }
}
void     display_setRotation180(bool on) {
  s_rot180 = on;
  tft.setRotation(on ? 1 : 3);  // 3=landscape, 1=landscape flipped 180
  display_resetClock();
}
void     display_setDimLevel(uint8_t pct) {
  if (pct < 1) pct = 1; if (pct > 100) pct = 100;
  s_dimLevel = pct;
  // Recompute dim colors
  s_dayDim   = dimColor(s_dayFg);
  s_nightDim = dimColor(s_nightFg);
}

void display_sleep(bool on) {
  if (on && !s_displaySleeping) {
    tft.sendCommand(0x10); // SLPIN
    ledcWrite(BL_PWM_CHANNEL, 0);
    s_displaySleeping = true;
  } else if (!on && s_displaySleeping) {
    tft.sendCommand(0x11); // SLPOUT
    delay(120);
    s_displaySleeping = false;
    s_clockInited = false; // force full redraw
  }
}

Adafruit_ST7789& display_getTft() { return tft; }

void display_resetClock() {
  s_clockInited = false;
  s_lastNight = -1;
  s_lastClockStr[0] = '\0';
  s_lastBattPct = -1;          // force battery indicator redraw
  s_lastTemp = -999.0f;        // force temp redraw
}

static void applyBacklight(uint8_t pct) {
  if (pct != s_curBl) {
    s_curBl = pct;
    ledcWrite(BL_PWM_CHANNEL, (uint32_t)pct * 255 / 100);
  }
}

static bool isNightTime(int hour, int minute) {
  uint16_t now = (uint16_t)(hour * 60 + minute);
  if (s_nightMin > s_dayMin) {
    // e.g. day=8:00, night=22:00 → night is [22:00..8:00)
    return (now >= s_nightMin || now < s_dayMin);
  } else {
    // e.g. day=22:00, night=8:00 → night is [8:00..22:00)
    return (now >= s_nightMin && now < s_dayMin);
  }
}

// Helper: get xAdvance for a character from the GFXfont glyph table
static int16_t fontCharAdvance(const GFXfont* f, char c) {
  uint8_t idx = (uint8_t)c - pgm_read_byte(&f->first);
  return (int16_t)pgm_read_byte(&((GFXglyph*)pgm_read_ptr(&f->glyph))[idx].xAdvance);
}

// 4-bit alpha blend of fg over bg in RGB565. a in [0..15]: 0=bg, 15=fg.
static inline uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t a) {
  if (a == 0)  return bg;
  if (a >= 15) return fg;
  uint8_t fr = (fg >> 11) & 0x1F;
  uint8_t fG = (fg >> 5)  & 0x3F;
  uint8_t fb =  fg        & 0x1F;
  uint8_t br = (bg >> 11) & 0x1F;
  uint8_t bG = (bg >> 5)  & 0x3F;
  uint8_t bb =  bg        & 0x1F;
  uint8_t inv = 15 - a;
  uint8_t r = (uint8_t)((fr * a + br * inv) / 15);
  uint8_t g = (uint8_t)((fG * a + bG * inv) / 15);
  uint8_t b = (uint8_t)((fb * a + bb * inv) / 15);
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// Draw a 4bpp grayscale glyph onto a GFXcanvas16 with alpha blending against
// whatever pixels are already in the canvas. (cursorX, cursorY) match
// Adafruit_GFX print() semantics: cursorX is left edge of the cell, cursorY
// is the baseline. The GFXfont struct layout is unchanged -- only the bitmap
// data is interpreted as 4bpp (two pixels per byte, high nibble first).
static void drawCharAA(GFXcanvas16& cv, const GFXfont* f, char c,
                       int16_t cursorX, int16_t cursorY, uint16_t color) {
  uint8_t first = pgm_read_byte(&f->first);
  uint8_t last  = pgm_read_byte(&f->last);
  if ((uint8_t)c < first || (uint8_t)c > last) return;
  GFXglyph* glyphs = (GFXglyph*)pgm_read_ptr(&f->glyph);
  GFXglyph* g = &glyphs[(uint8_t)c - first];
  uint8_t* bitmap = (uint8_t*)pgm_read_ptr(&f->bitmap);

  uint16_t bo = pgm_read_word(&g->bitmapOffset);
  uint8_t  w  = pgm_read_byte(&g->width);
  uint8_t  h  = pgm_read_byte(&g->height);
  int8_t   xo = pgm_read_byte(&g->xOffset);
  int8_t   yo = pgm_read_byte(&g->yOffset);

  int16_t  cw  = cv.width();
  int16_t  ch  = cv.height();
  uint16_t* fb = cv.getBuffer();

  uint32_t total = (uint32_t)w * h;
  for (uint32_t i = 0; i < total; i++) {
    uint8_t byte = pgm_read_byte(&bitmap[bo + (i >> 1)]);
    uint8_t a    = (i & 1) ? (byte & 0x0F) : (byte >> 4);
    if (a == 0) continue;
    int16_t px = cursorX + xo + (int16_t)(i % w);
    int16_t py = cursorY + yo + (int16_t)(i / w);
    if ((uint16_t)px >= (uint16_t)cw || (uint16_t)py >= (uint16_t)ch) continue;
    uint16_t bg = fb[(uint32_t)py * cw + px];
    fb[(uint32_t)py * cw + px] = blend565(color, bg, a);
  }
}

void display_showClock() {
  struct tm ti;
  time_t now = time(nullptr);
  localtime_r(&now, &ti);

  bool validTime = (ti.tm_year >= 124);
  bool colonOn   = (ti.tm_sec % 2 == 0);

  // Format time into 4 separate digit slots + colon flag
  // d[0]=tens-of-hours (0 for blank), d[1]=units-of-hours, d[2]=tens-of-min, d[3]=units-of-min
  char d[4];
  char timeStr[6];   // for change detection
  if (validTime) {
    int displayHour = ti.tm_hour;
    if (s_h12) {
      displayHour = ti.tm_hour % 12;
      if (displayHour == 0) displayHour = 12;
    }
    d[0] = (displayHour >= 10) ? ('0' + displayHour / 10) : ' ';
    d[1] = '0' + displayHour % 10;
    d[2] = '0' + ti.tm_min / 10;
    d[3] = '0' + ti.tm_min % 10;
    snprintf(timeStr, sizeof(timeStr), "%c%c%c%c", d[0], d[1], d[2], d[3]);
  } else {
    d[0] = ' '; d[1] = '-'; d[2] = '-'; d[3] = '-';
    strcpy(timeStr, " ---");
  }

  // Pick day/night colors and backlight based on current time
  uint16_t fg, dim;
  bool natural = validTime && isNightTime(ti.tm_hour, ti.tm_min);
  // Manual override clears itself once the next scheduled transition happens
  // (i.e. the natural state has flipped away from what it was when the user
  // pressed the override button).
  if (s_ovrActive && natural != s_ovrNaturalAtSet) {
    s_ovrActive = false;
  }
  bool night = s_ovrActive ? s_ovrNight : natural;
  if (night) {
    fg  = s_nightFg;
    dim = s_nightDim;
    applyBacklight(s_nightBl);
  } else {
    fg  = s_dayFg;
    dim = s_dayDim;
    applyBacklight(s_dayBl);
  }

  // Rainbow mode: override fg with cycling hue
  if (s_rainbow) {
    s_rainbowHue += s_rainbowSpeed;
    // HSV to RGB565 (S=255, V=255)
    uint8_t h = s_rainbowHue;
    uint8_t region = h / 43;
    uint8_t rem = (h % 43) * 6;
    uint8_t r, g, b;
    switch (region) {
      case 0:  r = 255; g = rem;       b = 0;         break;
      case 1:  r = 255 - rem; g = 255; b = 0;         break;
      case 2:  r = 0;   g = 255;       b = rem;       break;
      case 3:  r = 0;   g = 255 - rem; b = 255;       break;
      case 4:  r = rem; g = 0;         b = 255;       break;
      default: r = 255; g = 0;         b = 255 - rem; break;
    }
    fg = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    dim = dimColor(fg);
  }

  bool colorChanged = (fg != s_curFg || dim != s_curDim);
  if (colorChanged) { s_curFg = fg; s_curDim = dim; }

  bool digitsChanged = (strcmp(timeStr, s_lastClockStr) != 0);
  bool colonChanged  = (colonOn != s_lastColonOn);

  // First-time init: compute layout, allocate canvas
  if (!s_clockInited) {
    tft.fillScreen(ST77XX_BLACK);
    // Indicators were just wiped; force them to redraw on the next tick.
    s_lastBattPct = -1;
    s_lastTemp = -999.0f;
    s_lastNight = -1;
    const GFXfont* fnt = clockFont();
    tft.setFont(fnt);
    tft.setTextSize(1);

    int16_t digitCellW = fontCharAdvance(fnt, '8');
    int16_t colonCellW = fontCharAdvance(fnt, ':');
    int16_t totalW = 4 * digitCellW + colonCellW;

    int16_t bx, by;
    uint16_t bw, bh;
    tft.getTextBounds("8", 0, 0, &bx, &by, &bw, &bh);

    // Canvas covers the text bounding box with some padding
    int16_t cvW = totalW;
    int16_t cvH = (int16_t)bh + 4;   // +4 for safety margin

    s_canvasX = (LAND_W - totalW) / 2;
    s_canvasY = (LAND_H - (int16_t)bh) / 2 + by - 2 + 44;  // by is negative, +44 to lower

    if (s_clockCanvas) { delete s_clockCanvas; s_clockCanvas = nullptr; }
    s_clockCanvas = new GFXcanvas16(cvW, cvH);

    // Seconds canvas: 2 digits at full DSEG7_80 size, will be displayed at half scale
    s_secFullW = 2 * digitCellW;
    s_secFullH = (int16_t)bh + 4;
    if (s_secCanvas) { delete s_secCanvas; s_secCanvas = nullptr; }
    s_secCanvas = new GFXcanvas16(s_secFullW, s_secFullH);
    // Half-scale position: right-aligned with main clock, below it
    s_secDispX = s_canvasX + totalW - s_secFullW / 2;
    s_secDispY = s_canvasY + cvH + 2;

    tft.fillScreen(ST77XX_BLACK);
    s_clockInited = true;
    s_lastClockStr[0] = '\0';
    s_lastColonOn = !colonOn;
    s_lastNight = -1;        // force icon draw
    s_lastSec   = -1;        // force seconds draw
    digitsChanged = true;    // force full draw
    colonChanged  = true;
  }

  if (!digitsChanged && !colonChanged && !colorChanged) return;
  if (!s_clockCanvas) return;

  GFXcanvas16& cv = *s_clockCanvas;
  cv.fillScreen(ST77XX_BLACK);
  const GFXfont* fnt = clockFont();
  cv.setFont(fnt);
  cv.setTextSize(1);

  // Layout within canvas (origin at 0,0)
  int16_t digitCellW = fontCharAdvance(fnt, '8');
  int16_t colonCellW = fontCharAdvance(fnt, ':');

  int16_t bx, by;
  uint16_t bw, bh;
  cv.getTextBounds("8", 0, 0, &bx, &by, &bw, &bh);
  int16_t textY = -by + 2;   // baseline Y within canvas

  int16_t cellX[5] = {
    0,
    digitCellW,
    (int16_t)(2 * digitCellW),
    (int16_t)(2 * digitCellW + colonCellW),
    (int16_t)(3 * digitCellW + colonCellW),
  };

  // Draw dim "8" background for all digit cells (anti-aliased)
  for (int i = 0; i < 4; i++) {
    int ci = (i < 2) ? i : i + 1;
    drawCharAA(cv, fnt, '8', cellX[ci], textY, s_curDim);
  }

  // Draw dim colon as two circles (centered vertically, more spacing)
  {
    int16_t colonMidX = cellX[2] + colonCellW / 2;
    int16_t colonMidY = (int16_t)(bh / 2) + 2; // vertical center of canvas
    int16_t dotR = 6;
    int16_t dotGap = 22; // distance from center to each dot
    cv.fillCircle(colonMidX, colonMidY - dotGap, dotR, s_curDim);
    cv.fillCircle(colonMidX, colonMidY + dotGap, dotR, s_curDim);
  }

  // Overdraw bright digits, right-aligned in their cells (anti-aliased)
  for (int i = 0; i < 4; i++) {
    if (d[i] == ' ') continue;   // skip blank leading digit
    int ci = (i < 2) ? i : i + 1;
    int16_t charAdv = fontCharAdvance(fnt, d[i]);
    int16_t xOff = digitCellW - charAdv;
    drawCharAA(cv, fnt, d[i], cellX[ci] + xOff, textY, s_curFg);
  }

  // Overdraw bright colon if on
  if (colonOn) {
    int16_t colonMidX = cellX[2] + colonCellW / 2;
    int16_t colonMidY = (int16_t)(bh / 2) + 2;
    int16_t dotR = 6;
    int16_t dotGap = 22;
    cv.fillCircle(colonMidX, colonMidY - dotGap, dotR, s_curFg);
    cv.fillCircle(colonMidX, colonMidY + dotGap, dotR, s_curFg);
  }

  // Push canvas to display in one shot
  tft.drawRGBBitmap(s_canvasX, s_canvasY, cv.getBuffer(), cv.width(), cv.height());

  // Draw seconds below the clock using DSEG7_80 at half scale, right-aligned
  if (s_showSeconds && validTime && s_secCanvas && (ti.tm_sec != s_lastSec || colorChanged || digitsChanged)) {
    GFXcanvas16& sc = *s_secCanvas;
    sc.fillScreen(ST77XX_BLACK);
    const GFXfont* fnt2 = clockFont();
    sc.setFont(fnt2);
    sc.setTextSize(1);

    int16_t digitCellW = fontCharAdvance(fnt2, '8');
    int16_t bx, by;
    uint16_t bw, bh;
    sc.getTextBounds("8", 0, 0, &bx, &by, &bw, &bh);
    int16_t textY = -by + 2;

    // Draw dim "88" background (anti-aliased)
    drawCharAA(sc, fnt2, '8', 0,            textY, s_curDim);
    drawCharAA(sc, fnt2, '8', digitCellW,   textY, s_curDim);

    // Draw bright seconds digits, right-aligned in cells (anti-aliased)
    char secBuf[3];
    snprintf(secBuf, sizeof(secBuf), "%02d", ti.tm_sec);
    for (int i = 0; i < 2; i++) {
      int16_t charAdv = fontCharAdvance(fnt2, secBuf[i]);
      int16_t xOff = digitCellW - charAdv;
      drawCharAA(sc, fnt2, secBuf[i], i * digitCellW + xOff, textY, s_curFg);
    }

    // Blit at half scale to TFT (2x2 box-average for smoother downscale)
    int16_t halfW = s_secFullW / 2;
    int16_t halfH = s_secFullH / 2;
    uint16_t* buf = sc.getBuffer();
    for (int16_t y = 0; y < halfH; y++) {
      for (int16_t x = 0; x < halfW; x++) {
        uint16_t p0 = buf[(y * 2)     * s_secFullW + (x * 2)];
        uint16_t p1 = buf[(y * 2)     * s_secFullW + (x * 2 + 1)];
        uint16_t p2 = buf[(y * 2 + 1) * s_secFullW + (x * 2)];
        uint16_t p3 = buf[(y * 2 + 1) * s_secFullW + (x * 2 + 1)];
        uint16_t r = (((p0 >> 11) & 0x1F) + ((p1 >> 11) & 0x1F)
                    + ((p2 >> 11) & 0x1F) + ((p3 >> 11) & 0x1F)) >> 2;
        uint16_t g = (((p0 >>  5) & 0x3F) + ((p1 >>  5) & 0x3F)
                    + ((p2 >>  5) & 0x3F) + ((p3 >>  5) & 0x3F)) >> 2;
        uint16_t b = ((p0 & 0x1F) + (p1 & 0x1F)
                    + (p2 & 0x1F) + (p3 & 0x1F)) >> 2;
        tft.drawPixel(s_secDispX + x, s_secDispY + y,
                      (r << 11) | (g << 5) | b);
      }
    }
    s_lastSec = ti.tm_sec;
  }

  // Draw sun (day) or moon (night) icon when state changes
  int8_t nightState = night ? 1 : 0;
  if (s_showIcons && nightState != s_lastNight) {
    // Icon geometry — 90px diameter
    const int16_t iconR   = 45;   // main circle radius
    const int16_t iconY   = LAND_H - 50;
    const int16_t iconX   = 55;
    const int16_t clearW  = 100;
    const int16_t clearH  = 100;

    // Clear icon area
    tft.fillRect(iconX - clearW/2, iconY - clearH/2, clearW, clearH, ST77XX_BLACK);

    if (!night) {
      // Draw sun: yellow circle + 8 rays
      const uint16_t sunCol = 0xFFE0; // yellow
      tft.fillCircle(iconX, iconY, iconR - 8, sunCol);
      for (int a = 0; a < 8; a++) {
        float rad = a * 0.7854f; // PI/4
        int16_t x1 = iconX + (int16_t)((iconR - 6) * cosf(rad));
        int16_t y1 = iconY + (int16_t)((iconR - 6) * sinf(rad));
        int16_t x2 = iconX + (int16_t)((iconR + 2) * cosf(rad));
        int16_t y2 = iconY + (int16_t)((iconR + 2) * sinf(rad));
        tft.drawLine(x1, y1, x2, y2, sunCol);
      }
    } else {
      // Draw crescent moon using night foreground color
      tft.fillCircle(iconX, iconY, iconR, fg);
      tft.fillCircle(iconX + 20, iconY - 16, iconR, ST77XX_BLACK);
    }
    s_lastNight = nightState;
  }
  if (!s_showIcons && s_lastNight != -1) {
    // Clear icon areas when icons just got disabled
    const int16_t iconY   = LAND_H - 50;
    const int16_t iconX   = 55;
    tft.fillRect(iconX - 50, iconY - 50, 100, 100, ST77XX_BLACK);
    s_lastNight = -1;
  }

  strcpy(s_lastClockStr, timeStr);
  s_lastColonOn = colonOn;
}

// ---- External temperature display (bottom-right) ----------------------------
static uint16_t s_lastTempColor = 0;

void display_showTemp(float tempC) {
  if (!s_showWeather) return;
  // Redraw if temp changed or color changed
  bool colorChanged = (s_curFg != s_lastTempColor);
  if (fabsf(tempC - s_lastTemp) < 0.1f && !colorChanged) return;
  s_lastTemp = tempC;
  s_lastTempColor = s_curFg;

  // Position: bottom-right (offset left to avoid rounded corners)
  const int16_t tx = LAND_W - 158;
  const int16_t ty = LAND_H - 50;

  // Clear area
  tft.fillRect(tx, ty, 140, 50, ST77XX_BLACK);

  // Draw temperature using current day/night color
  tft.setFont(NULL);
  tft.setTextSize(6);
  tft.setTextColor(s_curFg);
  tft.setCursor(tx, ty + 4);

  char buf[10];
  int t = (int)roundf(tempC);
  snprintf(buf, sizeof(buf), "%d%cC", t, (char)247);  // 247 = degree symbol in default font
  tft.print(buf);
}

// ---- Battery indicator (top-right) ------------------------------------------
void display_showBattery(uint8_t pct, float volts) {
  if (!s_showBattery) return;
  // Layout (top-right corner, shifted 14px left to avoid rounded corner)
  const int16_t bw = 28, bh = 12;            // body width / height
  const int16_t bx = LAND_W - bw - 8 - 14;
  const int16_t by = 6;
  const int16_t txtX = bx - 36;              // percent text left of icon
  const int16_t txtY = by;
  const int16_t areaX = txtX - 2;
  const int16_t areaW = LAND_W - areaX - 2;
  const int16_t areaH = bh + 4;

  // Skip if value unchanged (avoid flicker)
  if ((int8_t)pct == s_lastBattPct) return;
  s_lastBattPct = (int8_t)pct;

  // Clear the indicator area
  tft.fillRect(areaX, by - 1, areaW, areaH, ST77XX_BLACK);

  // Pick color based on charge level
  uint16_t col;
  if (volts <= 0.05f)   col = 0x7BEF;        // gray (no battery)
  else if (pct <= 15)   col = 0xF800;        // red
  else if (pct <= 35)   col = 0xFD20;        // orange
  else                  col = 0x07E0;        // green

  // Battery body: outline + nib
  tft.drawRect(bx, by, bw, bh, col);
  tft.fillRect(bx + bw, by + 3, 2, bh - 6, col);

  // Fill proportional to %
  int16_t fillW = ((bw - 4) * pct) / 100;
  if (fillW > 0) tft.fillRect(bx + 2, by + 2, fillW, bh - 4, col);

  // Percent text (size 1, default font ~6px wide)
  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(col, ST77XX_BLACK);
  tft.setCursor(txtX, txtY + 2);
  if (volts <= 0.05f) tft.print(F("---"));
  else                { char b[6]; snprintf(b, sizeof(b), "%3u%%", pct); tft.print(b); }
}
