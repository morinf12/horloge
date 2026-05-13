// =============================================================================
// prefs_cache - deferred commit wrapper around the "wifi" NVS namespace.
//
// All known persisted settings live in the same NVS namespace ("wifi").
// Direct use of the Arduino Preferences class commits to flash on every
// putXxx() call, which causes excessive flash wear when settings are changed
// rapidly (e.g. holding a button in the menu, or scrubbing a slider in the
// web UI).
//
// This module opens a single long-lived nvs_handle and applies writes via
// nvs_set_xxx without nvs_commit(). A commit is scheduled and only flushed
// to flash after the configured idle delay (default 1 minute) or when
// prefs_flush() is called explicitly (e.g. before ESP.restart()).
//
// Reads via the regular Preferences class continue to work: ESP-IDF NVS
// makes uncommitted writes visible to subsequent reads in the same RAM
// session, so no behavioural change is observed at runtime.
// =============================================================================
#pragma once
#include <Arduino.h>

// Open the underlying NVS handle. Safe to call multiple times.
void prefs_begin();

// Call from the main loop. Commits to flash if a write is pending and the
// idle delay has elapsed since the last change.
void prefs_loop();

// Force an immediate commit if any writes are pending. Call before reboot
// or any operation that could lose RAM state before the timer expires.
void prefs_flush();

// Deferred-commit setters. Each compares against the currently stored value
// and is a no-op when unchanged, so spamming the same value does not mark
// the namespace dirty.
void prefs_putBool   (const char* key, bool v);
void prefs_putUChar  (const char* key, uint8_t v);
void prefs_putUShort (const char* key, uint16_t v);
void prefs_putUInt   (const char* key, uint32_t v);
void prefs_putInt    (const char* key, int32_t v);
void prefs_putString (const char* key, const char* v);

// Deferred-commit remove. No-op if the key does not exist.
void prefs_remove    (const char* key);
