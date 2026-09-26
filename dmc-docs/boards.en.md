# Boards

DMC firmware supports about 90 board variants. This page helps you choose and shows the
observer-capable boards in detail.

## Choosing a board

- **Repeater**: relays packets multi-hop; runs on nearly every supported board.
- **Observer/MQTT**: a repeater or room server that uplinks to MQTT. This needs an ESP32
  board with WiFi (see the table below).

All boards use the global radio defaults: `LORA_FREQ=869.618`, `LORA_BW=62.5`,
`LORA_SF=8`.

## Observer-capable boards

ESP32 boards with WiFi that can run observer/MQTT builds. Blank cells could not be
confirmed from the repository.

| Board | Target | MCU | PSRAM | Flash | LoRa | Display | GPS |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Heltec V3 | `heltec_v3` | ESP32-S3 | | | SX1262 | SSD1306 OLED | ✓ |
| Heltec V4 | `heltec_v4` | ESP32-S3 | 2 MB | 16 MB | SX1262 | OLED / TFT | ✓ |
| Heltec V4-R8 | `heltec_v4_r8` | ESP32-S3 | 8 MB | 16 MB | SX1262 | OLED / TFT | ✓ |
| WSL3 | `heltec_v3` (`Heltec_WSL3_*`) | ESP32-S3 | | | SX1262 | None | ✓ |
| Heltec T190 | `heltec_t190` | ESP32-S3 | 8 MB | 16 MB | SX1262 | ST7789 TFT | |
| Heltec Tracker | `heltec_tracker` | ESP32-S3 | | | SX1262 | ST7735 TFT | ✓ |
| Heltec Wireless Tracker | `heltec_tracker_v2` | ESP32-S3 | | 8 MB | SX1262 | ST7735 TFT | ✓ |
| LilyGo T3S3 | `lilygo_t3s3` | ESP32-S3 | yes (QSPI) | 4 MB | SX1262 | SSD1306 OLED | |
| LilyGo T-Beam 1W | `lilygo_tbeam_1w` | ESP32-S3 | yes (OPI) | 16 MB | SX1262 | SH1106 OLED | ✓ |
| LilyGo TLora V2.1 | `lilygo_tlora_v2_1` | ESP32 | | 4 MB | SX1276 | SSD1306 OLED | |
| T-Beam SX1262 | `lilygo_tbeam_SX1262` | ESP32 | | 4 MB | SX1262 | SSD1306 OLED | ✓ |
| T-Beam SX1276 | `lilygo_tbeam_SX1276` | ESP32 | | 4 MB | SX1276 | SSD1306 OLED | ✓ |
| Station G2 | `station_g2` | ESP32-S3 | yes (OPI) | 16 MB | SX1262 | SH1106 OLED | ✓ |
| Station G3 | `station_g3_esp32` | ESP32-S3 | yes (OPI) | 16 MB | SX1262 | SH1106 OLED | ✓ |
| RAK3112 | `rak3112` | ESP32-S3 | | | SX1262 | None | ✓ |
| ThinkNode M7 | `thinknode_m7` | ESP32-S3 | yes (OPI) | 8 MB | LR1110 | None | |
| Xiao S3 WIO | `xiao_s3_wio` | ESP32-S3 | | | SX1262 | SSD1306 OLED | |

PSRAM and flash cells are blank for the boards that ride on a stock PlatformIO board id
(Heltec V3, WSL3, Heltec Tracker, RAK3112, Xiao S3 WIO): those values live in the
`espressif32` platform package, not in the repository.

## Board families

The other families, with MCU platform and a few representative variants from `variants/`.

| Family | MCU platform(s) | Examples |
| --- | --- | --- |
| Heltec | ESP32 (classic/S3/C3) + nRF52 | `heltec_v2`, `heltec_v3`, `heltec_v4`, `heltec_t114`, `heltec_tower_v2`, `heltec_wireless_paper` |
| LilyGo | ESP32 (S3/C6/classic) + nRF52 | `lilygo_t3s3`, `lilygo_tbeam_1w`, `lilygo_tdeck`, `lilygo_techo`, `lilygo_tlora_c6` |
| RAK | nRF52 + RP2040 + ESP32-S3 + STM32 | `rak4631`, `rak11310`, `rak3112`, `rak3x72` |
| Seeed / Xiao | ESP32 (C3/C6/S3) + nRF52 + RP2040 | `xiao_c3`, `xiao_s3`, `xiao_nrf52`, `wio_wm1110`, `t1000-e` |
| Station | ESP32-S3 | `station_g2`, `station_g3_esp32` |
| ThinkNode | ESP32-S3 + nRF52 | `thinknode_m1` through `thinknode_m9` |
| Ikoka | nRF52 | `ikoka_handheld_nrf`, `ikoka_nano_nrf`, `ikoka_stick_nrf` |
| GAT562 | nRF52 (board `rak4631`) | `gat562_mesh_evb_pro`, `gat562_mesh_tracker_pro` |
| Others | ESP32 / nRF52 / RP2040 / STM32 | `ebyte_eora_s3`, `nano_g2_ultra`, `promicro`, `rpi_picow`, `wio-e5-mini` |

See the `variants/` folder in the firmware repository for the full list.

## Picks

- **Simplest observer**: Heltec V3.
- **Most memory for stats/observer**: Heltec V4-R8 or Station G3 (8 MB PSRAM).
- **Best transmit power (repeater)**: LilyGo T-Beam 1W.
- **Compact and headless**: RAK3112 or WSL3.
