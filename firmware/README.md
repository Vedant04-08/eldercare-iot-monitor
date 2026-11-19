# Eldercare C3 prototype — replacement firmware

This is an engineering prototype, not a validated medical monitor or guaranteed fall detector. No physical ESP32 or sensor was available for hardware tests. Chest-mounted MAX30102 accuracy has not been established. Never interpret missing/invalid data as a normal health result.

## 1. First upload: compile before connecting it to a person

Extract the whole `ElderCare_C3` folder. Open `ElderCare_C3.ino`; keep every companion `.h` file in that folder.

Arduino IDE Boards Manager:

- Install **esp32 by Espressif Systems, version 3.3.0**.
- Select **ESP32C3 Dev Module**.
- Set **USB CDC On Boot: Enabled** for the SuperMini USB Serial Monitor.
- Select the correct USB port.
- Flash size: **4 MB**, only if your module actually has 4 MB flash.
- Partition scheme: **Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)**. It includes OTA application slots. Do not use Huge APP / No OTA. If the build exceeds this slot, use an OTA partition with larger app slots, not a no-OTA partition.

Library Manager:

- Adafruit MPU6050 (accept Adafruit BusIO and Adafruit Unified Sensor dependencies).
- SparkFun MAX3010x Pulse and Proximity Sensor Library.
- ArduinoJson **7.4.2**.

In `config.h`, set your `WIFI_SSID`, `WIFI_PASSWORD`, `BOT_TOKEN`, your two recipient chat IDs and a unique `OTA_PASSWORD` of at least 12 characters. Password placeholders deliberately do not connect/update.

Click **Verify** first. After compilation succeeds, upload by USB. Serial Monitor: **115200 baud**, newline ending. Keep the original sketch as your backup.

## 2. Wiring

| Component | Connection |
|---|---|
| MPU6050 + MAX30102 SDA | GPIO 8 |
| MPU6050 + MAX30102 SCL | GPIO 9 |
| Both sensor modules VCC | Appropriate 3.3 V input on your breakout |
| MPU6050 AD0 | GND (address 0x68) |
| Passive piezo positive | GPIO 3 |
| Passive piezo negative | GND |
| Tactile button | GPIO 2 to GND, internal pull-up |
| All module grounds | Common GND |

MAX30102 address is 0x57. This assumes breakout modules, not an unregulated bare MAX30102 IC. Only a low-current passive piezo should be connected directly to a GPIO; a magnetic/active buzzer requiring more current needs a suitable driver. `PASSIVE_BUZZER=false` selects a DC active-buzzer output, but does not make a high-current buzzer safe to drive directly.

The shared I2C bus is 400 kHz. GPIO 2, 8 and 9 also participate in C3 boot strapping: external wiring must not force incompatible levels during reset. Do not hold the SOS button during reset. If boot becomes unreliable, inspect the pull-ups, button and sensor wiring first.

## 3. Telegram setup

Both recipients must independently open the bot and press **Start**. Only the two listed private-chat owners are accepted. Group chats are intentionally unsupported. Do not run another program polling this same bot. If the bot previously used a webhook, remove that webhook before using getUpdates.

In **@BotFather**, send `/setcommands`, choose this bot, and paste the contents of `BOT_COMMANDS.txt` (without slash prefixes). This sets the visible menu; firmware implements the commands.

| Command | Behavior |
|---|---|
| `/start`, `/status` | Current estimates, validity, motion, reading age, open alert |
| `/health` | Sensor availability, sample gaps, lost-event count, heap, IP |
| `/test` | One-second finite buzzer test and labelled TEST event to both recipients |
| `/demo on` | Explicit drop/impact demonstration mode for 120 seconds |
| `/demo off` | Return to normal fall heuristic |
| `/live` | Edit a dashboard every five seconds for up to 60 seconds |
| `/stop` | Stop that chat's live dashboard; monitoring continues |
| `/ack NUMBER` | Acknowledge the current alert; does not declare the wearer safe |
| `/update` | Explain maintenance and open a 30-second confirmation window |
| `/update confirm` | Reboot into five-minute phone update mode if no unacknowledged alert |
| `/help` | Command guide |

Remote `/status` and `/health` replies go only to the requesting chat. Events and alarms go to both recipients. Each recipient has separate retry state. A Telegram `ok=true` response means the API accepted the message; it does not prove the caregiver read it. An ambiguous network timeout after server acceptance can still produce a duplicate on retry; the boot/event ID helps identify it. Invalid tokens are reported on Serial Monitor and stop repeated unauthorized requests until reboot.

HTTPS certificate validation is enabled using public trust anchors in `certs.h`; there is no `setInsecure()`. Wi-Fi, working DNS and time synchronization are required. If NTP is blocked, local monitoring continues, but Telegram waits. CA rotation may require updating the trust anchors. Connection/delivery latency is not guaranteed.

## 4. Demonstration sequence (after verifying the hardware)

1. Power on; keep the MPU stationary for at least three seconds. Read `/health` and check both sensors.
2. Send `/test`. Expect a finite series of beeps and a TEST message in both chats. Holding the SOS button must produce one event, then release/repress to create another.
3. Place a finger gently over the MAX30102 optical face. Keep both sensor and finger still, with ambient light shielded. Expect approximately 5–10 seconds before valid estimates. Do not press hard or use a covering over the optical face.
4. Send `/status`, then `/live`. Local estimates update from a rolling four-second window once per second. Telegram dashboard updates are deliberately slower. The values are not independently validated medical measurements.
5. Send `/demo on`; wait for confirmation. With the device unworn, use a small controlled drop onto thick padding. Low acceleration or impact triggers a labelled DEMO event and a three-second buzzer pattern. Do not throw a battery-powered assembly onto a hard floor or perform human fall tests.
6. Send `/demo off`. Send `/ack NUMBER` for the current alert after checking it. Acknowledgement is logged but does not stop sensor monitoring.
7. Disconnect Wi-Fi, press SOS once, reconnect Wi-Fi. The queued event should reach each recipient with its original snapshot and elapsed age. Repeat while monitoring `/health` for gaps and lost events.

Do not fake low oxygen or dangerous heart rates in a human demo. `/test` explicitly tests alarm delivery without inventing physiological readings.

## 5. Coordination and timing

The ESP32-C3 has one core. FreeRTOS schedules a priority-3 sensor task and priority-1 network task; this is concurrency, not two CPU cores.

| Operation | Configured behavior |
|---|---|
| MPU6050 | 100 Hz device output and approximately 10 ms host service |
| MAX30102 | 100 Hz acquisition averaged by four → 25 FIFO samples/s |
| Optical algorithm | SparkFun/Maxim four-second/100-sample window; new calculation each 25 samples |
| Optical servicing | Approximately every 5 ms; FIFO gaps ≥100 ms invalidate and restart acquisition |
| Buzzer | Hardware 2.5 kHz PWM; nonblocking timed envelope |
| Button | 35 ms stable debounce; one event per press |
| Alarm sound | 180 ms bursts at 0, 300 and 600 ms in each second; stops at three seconds |
| Fall candidate | Immediate short local pattern, then up to four seconds to assess evidence |
| Demo event | Queued on the first sampled low-acceleration or impact threshold crossing |
| Vital advisory | Persistently abnormal valid estimates for at least ten seconds |
| Telegram poll | Nominally every two seconds when requests/delivery queue allow |

These are configured schedules, not hard real-time guarantees. I2C timeouts, computation and scheduler activity introduce jitter. `maxGap` and missed-window counters make substantial optical sampling delays visible. Network requests do not intentionally block the sensor task, but hardware testing is necessary to establish actual timing under load.

Normal fall logic uses impact followed by approximately one second of stillness plus prior low acceleration or a substantial posture change. Some real falls will not meet this heuristic; some non-falls will. Non-confirmed impacts produce an informational event instead of a definite fall claim. An eight-G MPU range can clip severe impacts.

A stationary, functioning accelerometer magnitude should be near 9.81 m/s². If yours reports 6.9–8.1 while stationary, investigate sensor calibration, mounting, power and readings before tuning thresholds to conceal the discrepancy. Motion gating intentionally rejects readings far from plausible rest.

## 6. What optical validity means

The old one-second extrema formula and its 75–100% clamp have been removed. This firmware uses the library's example algorithm at its expected 25 Hz rate, adds optical amplitude/correlation checks, rejects motion, requires consecutive consistent pulse estimates, and withholds stale results.

Those checks reject some artifacts; they do not prove that a displayed estimate is correct. The algorithm's validity flags and the added signal gates are not clinical validation. Accepted pulse estimates are limited to 35–200 BPM and oxygen estimates to 70–100%; outside those algorithm bounds data are unavailable, not clamped or diagnosed. Configured advisory thresholds are prototype values, not universal safe limits or a diagnosis.

For the demonstration, a stationary fingertip is the practical starting point. Bare-chest operation needs separately measured signal quality and comparison against an appropriate reference. No software can guarantee accuracy through clothing or a closed opaque case. The optical face needs a skin-facing opening, light shielding and stable contact. Validate again in the final enclosure: buzzer vibration, movement, contact pressure and power noise can change results. The software does not verify case dimensions, battery safety, skin temperature, acoustics or wearability.

The system continues monitoring motion and SOS even when no usable pulse signal is available. There is no automatic inference that “no contact” means “not worn” or that “no alert” means “safe.”

## 7. Updating from a phone

Initial installation is by USB. Configure a strong `OTA_PASSWORD` first. After a successful initial flash:

1. Modify your source, **compile for the same ESP32-C3 board/partition**, and use Arduino IDE **Sketch → Export Compiled Binary**. Transfer the application file ending `.ino.bin` to your phone. Do not select `.merged.bin`, `.bootloader.bin` or `.partitions.bin`.
2. Put phone and device on the same trusted Wi-Fi. Hotspot clients may be isolated by some phones; if access fails, use a shared router. Get the device IP from `/health`.
3. Send `/update`, read the maintenance notice, then `/update confirm` within 30 seconds. An active alert must first be checked and acknowledged.
4. Open the provided `http://DEVICE_IP/` address. Username `admin`; password is `OTA_PASSWORD` from `config.h`. The IP can change after reboot; check Serial Monitor if necessary.
5. Select the compiled application `.bin`, keep power stable, and upload. Successful update reboots; otherwise the five-minute window returns the device to monitoring. Digest authentication and a random form token protect the upload route. This local HTTP service is not encrypted; use only trusted Wi-Fi, never port-forward it to the internet.

Monitoring is intentionally paused during maintenance. The five-minute deadline includes upload time. There is no source-code compiler on the ESP32. A phone can upload the compiled binary; editing `.ino` on a phone still requires a compatible compiler/cloud build elsewhere. A structurally valid but faulty new application may still require USB recovery; this is not signed firmware or automatic health-check rollback.

## 8. Known limits and verification still required

- Test buzzer audibility, duration and current draw on your actual module.
- Test both chat deliveries, wrong-token behavior, Wi-Fi loss, no NTP and rate limiting.
- Check optical availability before and after every motion/alarm. Invalid periods must show unavailable.
- Check `/health` under repeated commands; sampling gaps invalidate optical windows instead of silently reusing data.
- Incoming queue holds 12 events and network storage holds 16. Overflow is counted; informational events may be displaced by alarms. The queue is RAM-only and is lost on reset/power loss/firmware update. No claim of guaranteed delivery is made.
- There is no battery-voltage measurement hardware in the supplied wiring, no cellular fallback, no GPS, and no automatic escalation to emergency services. Do not invent those values in the UI.
- Chest accuracy, false-positive/false-negative rates and long-duration reliability remain untested. This should not be the sole safety mechanism for an unattended elder.

## 9. Code layout

- `ElderCare_C3.ino`: Arduino entry points.
- `config.h`: editable credentials, pin assignment and prototype thresholds.
- `firmware.h`: sensor, buzzer, event, Telegram and maintenance implementations.
- `certs.h`: public TLS roots; contains no private keys.
- `BOT_COMMANDS.txt`: paste into BotFather `/setcommands`.
- `BUILD_RESULT.txt`: compilation result and exact library versions, if verification succeeded.

The original uploaded sketch has not been overwritten.
