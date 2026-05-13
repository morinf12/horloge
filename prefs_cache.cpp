#include "prefs_cache.h"
#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>
#include <stdlib.h>

static nvs_handle_t s_h        = 0;
static bool         s_open     = false;
static bool         s_dirty    = false;
static uint32_t     s_lastChange = 0;

// Idle delay before an NVS commit is flushed to flash.
static const uint32_t COMMIT_DELAY_MS = 60UL * 1000UL;  // 1 minute

static void ensureOpen() {
  if (s_open) return;
  esp_err_t err = nvs_open("wifi", NVS_READWRITE, &s_h);
  if (err == ESP_ERR_NVS_NOT_INITIALIZED) {
    nvs_flash_init();
    err = nvs_open("wifi", NVS_READWRITE, &s_h);
  }
  s_open = (err == ESP_OK);
  if (!s_open) {
    Serial.printf("[prefs] nvs_open failed: %d\n", (int)err);
  }
}

static void mark() {
  s_dirty = true;
  s_lastChange = millis();
}

void prefs_begin() {
  ensureOpen();
}

void prefs_loop() {
  if (!s_dirty) return;
  if ((millis() - s_lastChange) >= COMMIT_DELAY_MS) {
    prefs_flush();
  }
}

void prefs_flush() {
  if (!s_open || !s_dirty) { s_dirty = false; return; }
  esp_err_t err = nvs_commit(s_h);
  if (err != ESP_OK) Serial.printf("[prefs] commit failed: %d\n", (int)err);
  s_dirty = false;
}

void prefs_putBool(const char* k, bool v) {
  ensureOpen(); if (!s_open) return;
  uint8_t cur = 0;
  uint8_t want = v ? 1 : 0;
  if (nvs_get_u8(s_h, k, &cur) == ESP_OK && cur == want) return;
  if (nvs_set_u8(s_h, k, want) == ESP_OK) mark();
}

void prefs_putUChar(const char* k, uint8_t v) {
  ensureOpen(); if (!s_open) return;
  uint8_t cur = 0;
  if (nvs_get_u8(s_h, k, &cur) == ESP_OK && cur == v) return;
  if (nvs_set_u8(s_h, k, v) == ESP_OK) mark();
}

void prefs_putUShort(const char* k, uint16_t v) {
  ensureOpen(); if (!s_open) return;
  uint16_t cur = 0;
  if (nvs_get_u16(s_h, k, &cur) == ESP_OK && cur == v) return;
  if (nvs_set_u16(s_h, k, v) == ESP_OK) mark();
}

void prefs_putUInt(const char* k, uint32_t v) {
  ensureOpen(); if (!s_open) return;
  uint32_t cur = 0;
  if (nvs_get_u32(s_h, k, &cur) == ESP_OK && cur == v) return;
  if (nvs_set_u32(s_h, k, v) == ESP_OK) mark();
}

void prefs_putInt(const char* k, int32_t v) {
  ensureOpen(); if (!s_open) return;
  int32_t cur = 0;
  if (nvs_get_i32(s_h, k, &cur) == ESP_OK && cur == v) return;
  if (nvs_set_i32(s_h, k, v) == ESP_OK) mark();
}

void prefs_putString(const char* k, const char* v) {
  ensureOpen(); if (!s_open || v == nullptr) return;
  // Skip when the stored string is byte-identical.
  size_t sz = 0;
  if (nvs_get_str(s_h, k, nullptr, &sz) == ESP_OK && sz == strlen(v) + 1) {
    char* buf = (char*)malloc(sz);
    if (buf) {
      bool same = (nvs_get_str(s_h, k, buf, &sz) == ESP_OK) && (strcmp(buf, v) == 0);
      free(buf);
      if (same) return;
    }
  }
  if (nvs_set_str(s_h, k, v) == ESP_OK) mark();
}

void prefs_remove(const char* k) {
  ensureOpen(); if (!s_open) return;
  esp_err_t err = nvs_erase_key(s_h, k);
  if (err == ESP_OK) mark();
  // ESP_ERR_NVS_NOT_FOUND is fine: nothing to do.
}
