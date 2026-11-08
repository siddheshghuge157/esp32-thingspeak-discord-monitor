# Notification Bot

The simpler, no-display version of the Discord monitor. An ESP32 reads its sensors, uploads to ThingSpeak every 30 seconds, and lets you check on things or change alert thresholds from a Discord channel.

Full write-up: [docs/Report3_Notification_Bot.docx](docs/Report3_Notification_Bot.docx)

## Hardware

| Part | Pin |
|---|---|
| DHT11 | 19 |
| HC-SR04 | trig 25, echo 26 |
| Potentiometer | 34 |
| LDR | 35 |
| LED | 13 |
| Buzzer | 15 |

## Setup

1. Install: ThingSpeak, DHT sensor library (plus Adafruit Unified Sensor), ArduinoJson.
2. Copy `secrets.example.h` to `secrets.h` and fill in Wi-Fi, ThingSpeak channel ID and keys, Discord bot token and channel ID.
3. Upload `notification_bot.ino`.

## Commands

| Command | Effect |
|---|---|
| `!status` | readings, thresholds, LED/buzzer state |
| `!temp <value>` | high temperature threshold (C) |
| `!dist <value>` | proximity threshold (cm) |
| `!ledon` `!ledoff` `!buzzon` `!buzzoff` | manual toggles |
| `!history` | last 5 temperature readings from ThingSpeak |
| `!read <field> <count>` | history for a field (1-8) |

## Known issues

- The ThingSpeak history request uses a plain `WiFiClient` on port 443, so `!history` and `!read` probably won't connect. The LCD monitor fixes this with `WiFiClientSecure`.
- Setup waits on NTP with no timeout, so it can hang on a bad network.
- Make sure the ThingSpeak channel ID in `secrets.h` is your real one.
