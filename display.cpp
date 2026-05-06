#include "display.h"
#include "config.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <time.h>

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

void display_begin() {
  // Setup backlight as PWM
  ledcSetup(BL_PWM_CHANNEL, BL_PWM_FREQ, BL_PWM_RES);
  ledcAttachPin(TFT_BL_PIN, BL_PWM_CHANNEL);
  ledcWrite(BL_PWM_CHANNEL, 255);   // full brightness for boot

  s_spi.begin(TFT_SCLK_PIN, -1, TFT_MOSI_PIN, TFT_CS_PIN);

  tft.init(TFT_WIDTH, TFT_HEIGHT);   // 240x280
  tft.setRotation(3);                // landscape 280x240
  tft.fillScreen(ST77XX_BLACK);
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
#include "SevenSeg128.h"

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
static bool    s_ecoMode         = false; // power saving mode
static uint8_t s_dimLevel        = 25;    // dim digit intensity % (1-100)
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
bool     display_getEcoMode() { return s_ecoMode; }
void     display_setEcoMode(bool on) { s_ecoMode = on; }
uint8_t  display_getDimLevel() { return s_dimLevel; }
static bool s_rot180 = false;
bool     display_getRotation180() { return s_rot180; }
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
    d[0] = (ti.tm_hour >= 10) ? ('0' + ti.tm_hour / 10) : ' ';
    d[1] = '0' + ti.tm_hour % 10;
    d[2] = '0' + ti.tm_min / 10;
    d[3] = '0' + ti.tm_min % 10;
    snprintf(timeStr, sizeof(timeStr), "%c%c%c%c", d[0], d[1], d[2], d[3]);
  } else {
    d[0] = ' '; d[1] = '-'; d[2] = '-'; d[3] = '-';
    strcpy(timeStr, " ---");
  }

  // Pick day/night colors and backlight based on current time
  uint16_t fg, dim;
  bool night = validTime && isNightTime(ti.tm_hour, ti.tm_min);
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
    s_rainbowHue += 1;
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
    tft.setFont(&SevenSeg128);
    tft.setTextSize(1);

    int16_t digitCellW = fontCharAdvance(&SevenSeg128, '8');
    int16_t colonCellW = fontCharAdvance(&SevenSeg128, ':');
    int16_t totalW = 4 * digitCellW + colonCellW;

    int16_t bx, by;
    uint16_t bw, bh;
    tft.getTextBounds("8", 0, 0, &bx, &by, &bw, &bh);

    // Canvas covers the text bounding box with some padding
    int16_t cvW = totalW;
    int16_t cvH = (int16_t)bh + 4;   // +4 for safety margin

    s_canvasX = (LAND_W - totalW) / 2;
    s_canvasY = (LAND_H - (int16_t)bh) / 2 + by - 2 + 44;  // by is negative, +44 to lower

    if (s_clockCanvas) delete s_clockCanvas;
    s_clockCanvas = new GFXcanvas16(cvW, cvH);

    // Seconds canvas: 2 digits at full SevenSeg128 size, will be displayed at half scale
    s_secFullW = 2 * digitCellW;
    s_secFullH = (int16_t)bh + 4;
    if (s_secCanvas) delete s_secCanvas;
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
  cv.setFont(&SevenSeg128);
  cv.setTextSize(1);

  // Layout within canvas (origin at 0,0)
  int16_t digitCellW = fontCharAdvance(&SevenSeg128, '8');
  int16_t colonCellW = fontCharAdvance(&SevenSeg128, ':');

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

  // Draw dim "8" background for all digit cells
  cv.setTextColor(s_curDim);
  for (int i = 0; i < 4; i++) {
    int ci = (i < 2) ? i : i + 1;
    cv.setCursor(cellX[ci], textY);
    cv.print("8");
  }

  // Draw dim colon background
  cv.setCursor(cellX[2], textY);
  cv.print(":");

  // Overdraw bright digits, right-aligned in their cells
  cv.setTextColor(s_curFg);
  for (int i = 0; i < 4; i++) {
    if (d[i] == ' ') continue;   // skip blank leading digit
    int ci = (i < 2) ? i : i + 1;
    int16_t charAdv = fontCharAdvance(&SevenSeg128, d[i]);
    int16_t xOff = digitCellW - charAdv;
    char s[2] = { d[i], '\0' };
    cv.setCursor(cellX[ci] + xOff, textY);
    cv.print(s);
  }

  // Overdraw bright colon if on
  if (colonOn) {
    cv.setCursor(cellX[2], textY);
    cv.print(":");
  }

  // Push canvas to display in one shot
  tft.drawRGBBitmap(s_canvasX, s_canvasY, cv.getBuffer(), cv.width(), cv.height());

  // Draw seconds below the clock using SevenSeg128 at half scale, right-aligned
  if (validTime && s_secCanvas && (ti.tm_sec != s_lastSec || colorChanged || digitsChanged)) {
    GFXcanvas16& sc = *s_secCanvas;
    sc.fillScreen(ST77XX_BLACK);
    sc.setFont(&SevenSeg128);
    sc.setTextSize(1);

    int16_t digitCellW = fontCharAdvance(&SevenSeg128, '8');
    int16_t bx, by;
    uint16_t bw, bh;
    sc.getTextBounds("8", 0, 0, &bx, &by, &bw, &bh);
    int16_t textY = -by + 2;

    // Draw dim "88" background
    sc.setTextColor(s_curDim);
    sc.setCursor(0, textY);
    sc.print("88");

    // Draw bright seconds digits, right-aligned in cells
    sc.setTextColor(s_curFg);
    char secBuf[3];
    snprintf(secBuf, sizeof(secBuf), "%02d", ti.tm_sec);
    for (int i = 0; i < 2; i++) {
      int16_t charAdv = fontCharAdvance(&SevenSeg128, secBuf[i]);
      int16_t xOff = digitCellW - charAdv;
      char s[2] = { secBuf[i], '\0' };
      sc.setCursor(i * digitCellW + xOff, textY);
      sc.print(s);
    }

    // Blit at half scale to TFT (nearest-neighbor 2:1 downscale)
    int16_t halfW = s_secFullW / 2;
    int16_t halfH = s_secFullH / 2;
    uint16_t* buf = sc.getBuffer();
    for (int16_t y = 0; y < halfH; y++) {
      for (int16_t x = 0; x < halfW; x++) {
        uint16_t px = buf[(y * 2) * s_secFullW + (x * 2)];
        tft.drawPixel(s_secDispX + x, s_secDispY + y, px);
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
