#pragma once
// ESP32-C3 SuperMini. Tested build target: ESP32C3 Dev Module, Arduino core 3.3.0.
// Replace these locally. Do not share a sketch containing your real bot token.
// This file ships as a SANITIZED TEMPLATE: fill in your own credentials before
// compiling. Never commit your real Wi-Fi password, bot token or chat IDs.
static const char WIFI_SSID[] = "YOUR_WIFI_SSID";
static const char WIFI_PASSWORD[] = "CHANGE_WIFI_PASSWORD";
static const char BOT_TOKEN[] = "PASTE_YOUR_BOT_TOKEN_HERE";
static const char* const CHAT_IDS[] = {"YOUR_TELEGRAM_CHAT_ID_1", "YOUR_TELEGRAM_CHAT_ID_2"};
static constexpr int RECIPIENTS = sizeof(CHAT_IDS) / sizeof(CHAT_IDS[0]);
static const char OTA_PASSWORD[] = "CHANGE_TO_A_UNIQUE_PASSWORD"; // At least 12 characters
static constexpr int SDA_PIN=8, SCL_PIN=9, SOS_PIN=2, BUZZER_PIN=3;
static constexpr bool PASSIVE_BUZZER = true;
static constexpr int BUZZER_HZ = 2500;
// Engineering starting values, NOT validated medical/fall thresholds.
static constexpr float FREEFALL_MS2=4.0f, IMPACT_MS2=20.0f;
static constexpr int BPM_LOW=45, BPM_HIGH=120, SPO2_LOW=92;
static constexpr uint32_t CONTACT_IR=50000;
// Prototype estimates only; optical geometry at the chest has not been validated.
static constexpr bool ENABLE_VITAL_ADVISORIES=true;
static constexpr uint32_t VITAL_PERSIST_MS=10000, FALL_COOLDOWN_MS=15000;
