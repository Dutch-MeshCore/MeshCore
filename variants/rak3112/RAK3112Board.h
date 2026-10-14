#pragma once

#include <Arduino.h>
#include <helpers/RefCountedDigitalPin.h>
#include <helpers/ESP32Board.h>

// built-ins
#ifndef PIN_VBAT_READ
  #define  PIN_VBAT_READ    1     // WB_A0, VBAT via the WisBlock base board divider
#endif
// NOTE: no ADC enable pin on the RAK3112. GPIO33-37 belong to the octal PSRAM and must not be touched.
// WisBlock base boards (RAK19007 etc.) divide VBAT with 1M / 1.5M (nominal x1.667); 1.73 compensates
// for ADC loading of the high-impedance divider (same empirical value as Meshtastic's RAK4631)
#define  ADC_MULTIPLIER   1.73f
#define  BATTERY_SAMPLES 8

class RAK3112Board : public ESP32Board {
public:
  RefCountedDigitalPin periph_power;

  RAK3112Board() : periph_power(PIN_VEXT_EN) { }

  void begin() {
    ESP32Board::begin();

    periph_power.begin();

    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      long wakeup_source = esp_sleep_get_ext1_wakeup_status();
      if (wakeup_source & (1 << P_LORA_DIO_1)) {  // received a LoRa packet (while in deep sleep)
        startup_reason = BD_STARTUP_RX_PACKET;
      }

      rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
      rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
    }
  }

  uint16_t getBattMilliVolts() override {
    analogReadResolution(12);

    // analogReadMilliVolts() applies the eFuse ADC calibration
    uint32_t mv = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
      mv += analogReadMilliVolts(PIN_VBAT_READ);
    }
    mv = mv / BATTERY_SAMPLES;

    return (uint16_t)(ADC_MULTIPLIER * mv);
  }

  const char* getManufacturerName() const override {
    return "RAK 3112";
  }
};
