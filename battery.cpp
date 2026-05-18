#include "battery.h"
#include "config.h"

// ESP32-S2 ADC: 13-bit (0-8191), default attenuation 11dB → 0-2.5V range
// With Wemos battery shield divider 130k/100k: Vadc = Vbat * 0.435 (max ~1.83V at 4.2V).

static float   s_voltage = 0.0f;
static uint8_t s_percent = 0;

// Li-ion 18650 discharge curve (voltage → percent)
// Lookup table: voltage thresholds for SoC estimation
static uint8_t voltToPercent(float v) {
  if (v >= 4.15f) return 100;
  if (v >= 4.05f) return 90;
  if (v >= 3.95f) return 80;
  if (v >= 3.85f) return 70;
  if (v >= 3.78f) return 60;
  if (v >= 3.72f) return 50;
  if (v >= 3.67f) return 40;
  if (v >= 3.62f) return 30;
  if (v >= 3.55f) return 20;
  if (v >= 3.45f) return 10;
  if (v >= 3.30f) return 5;
  return 0;
}

void battery_begin() {
  analogReadResolution(13);           // 13-bit on ESP32-S2
  analogSetAttenuation(ADC_11db);     // 0-2.5V range
  // Take a few dummy reads to stabilize ADC
  for (int i = 0; i < 5; i++) analogRead(BATT_ADC_PIN);
  battery_update();
}

void battery_update() {
  // Average 16 samples for noise reduction
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) {
    sum += analogRead(BATT_ADC_PIN);
  }
  float adcAvg = (float)sum / 16.0f;

  // Convert to voltage: ADC max 8191 = 2.5V (with 11dB attenuation)
  float adcVoltage = (adcAvg / 8191.0f) * 2.5f;

  // Apply 2-point linear calibration (ESP32-S2 ADC is non-linear)
  s_voltage = BATT_CAL_SLOPE * adcVoltage + BATT_CAL_OFFSET;

  // Clamp to valid range
  if (s_voltage > 4.25f) s_voltage = 4.25f;
  if (s_voltage < 2.5f)  s_voltage = 0.0f;  // no battery connected

  s_percent = voltToPercent(s_voltage);
}

float battery_voltage() { return s_voltage; }
uint8_t battery_percent() { return s_percent; }
