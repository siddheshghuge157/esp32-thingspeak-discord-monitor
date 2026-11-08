#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "ThingSpeak.h"
#include "DHT.h"
#include "time.h"
#include <ArduinoJson.h>
#include "secrets.h"

WiFiClient thingSpeakClient;

WiFiClientSecure discordClient;
String lastMessageId = "";

float tempThreshold = 35.0;
float distThreshold = 10.0;
bool alertsEnabled = true;

const int potPin = 34;
const int ldrPin = 35;
#define DHTPIN 19
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);
const int trigPin = 25;
const int echoPin = 26;

const int ledPin = 13;
const int buzzerPin = 15;

float potValue = 0;
float ldrValue = 0;
float temperatureC = 0;
float humidity = 0;
float distanceCm = 0;

unsigned long lastThingSpeakUpdate = 0;
const unsigned long thingSpeakInterval = 30000;
unsigned long lastDiscordCheck = 0;
const unsigned long discordCheckInterval = 3000;

void sendDiscordMessage(String content) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected – can't send Discord message.");
    return;
  }

  if (!discordClient.connect("discord.com", 443)) {
    Serial.println("Connection to Discord failed (send).");
    return;
  }

  StaticJsonDocument<256> doc;
  doc["content"] = content;
  String json;
  serializeJson(doc, json);

  String url = "/api/v10/channels/" + String(CHANNEL_ID) + "/messages";

  discordClient.println("POST " + url + " HTTP/1.0");
  discordClient.println("Host: discord.com");
  discordClient.println("Authorization: Bot " + String(DISCORD_BOT_TOKEN));
  discordClient.println("Content-Type: application/json");
  discordClient.print("Content-Length: ");
  discordClient.println(json.length());
  discordClient.println();
  discordClient.println(json);

  while (discordClient.connected() && !discordClient.available()) delay(10);
  while (discordClient.available()) {
    String line = discordClient.readStringUntil('\n');
    Serial.println(line);
  }
  discordClient.stop();
}

String getLatestDiscordMessage() {
  if (WiFi.status() != WL_CONNECTED) return "";

  if (!discordClient.connect("discord.com", 443)) {
    Serial.println("Connection to Discord failed (read).");
    return "";
  }

  String url = "/api/v10/channels/" + String(CHANNEL_ID) + "/messages?limit=1";
  discordClient.println("GET " + url + " HTTP/1.0");
  discordClient.println("Host: discord.com");
  discordClient.println("Authorization: Bot " + String(DISCORD_BOT_TOKEN));
  discordClient.println("Content-Type: application/json");
  discordClient.println();

  while (discordClient.connected() && !discordClient.available()) delay(10);
  while (discordClient.available()) {
    String line = discordClient.readStringUntil('\r');
    if (line == "\n") break;
  }

  String json = "";
  while (discordClient.available()) {
    json += discordClient.readString();
  }
  discordClient.stop();

  Serial.println("Raw JSON from Discord: " + json);

  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    return "";
  }

  JsonArray messages = doc.as<JsonArray>();
  if (messages.size() == 0) return "";

  JsonObject message = messages[0];
  String author = message["author"]["username"];
  String content = message["content"];
  String msgId = message["id"];

  if (author == "ESP32") return "";

  if (msgId != lastMessageId) {
    lastMessageId = msgId;
    return content;
  }
  return "";
}

String getThingSpeakData(int fieldNum, int results) {
  if (WiFi.status() != WL_CONNECTED) return "❌ WiFi not connected.";

  if (fieldNum < 1 || fieldNum > 8) return "❌ Field must be between 1 and 8.";
  if (results < 1 || results > 8000) return "❌ Results must be between 1 and 8000.";

  WiFiClient client;
  if (!client.connect("api.thingspeak.com", 443)) {
    return "❌ Could not connect to ThingSpeak.";
  }

  String url = "/channels/" + String(myChannelNumber) + "/fields/" + String(fieldNum) + ".json";
  url += "?api_key=" + String(myReadAPIKey);
  url += "&results=" + String(results);

  client.println("GET " + url + " HTTP/1.0");
  client.println("Host: api.thingspeak.com");
  client.println("Connection: close");
  client.println();

  String response = "";
  while (client.connected() || client.available()) {
    if (client.available()) response += client.readString();
  }
  client.stop();

  int jsonStart = response.indexOf("\r\n\r\n");
  if (jsonStart == -1) return "❌ Invalid response from ThingSpeak.";
  String json = response.substring(jsonStart + 4);

  Serial.println("ThingSpeak raw JSON: " + json);

  StaticJsonDocument<4096> doc;
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    Serial.println("ThingSpeak JSON parse error: " + String(error.c_str()));
    return "❌ Could not parse ThingSpeak data.";
  }

  JsonObject root = doc.as<JsonObject>();
  JsonArray feeds = root["feeds"];
  if (!feeds || feeds.size() == 0) return "❌ No data found for field " + String(fieldNum) + ".";

  String reply = "📈 **Field " + String(fieldNum) + " – Last " + String(feeds.size()) + " readings:**\n";
  for (JsonObject feed : feeds) {
    String timeStr = feed["created_at"];
    timeStr = timeStr.substring(0, 19);
    timeStr.replace("T", " ");
    String value = feed["field" + String(fieldNum)];
    reply += "`" + timeStr + "` → " + value + "\n";
  }

  if (reply.length() > 2000) {
    reply = reply.substring(0, 1950) + "\n... (truncated)";
  }
  return reply;
}

void handleCommand(String cmd) {
  cmd.trim();
  Serial.println("Command received: " + cmd);

  if (cmd == "!status") {
    String reply = "📊 **Current Status**\n";
    reply += "Pot: " + String(potValue, 1) + "\n";
    reply += "LDR: " + String(ldrValue, 1) + "\n";
    reply += "Temp: " + String(temperatureC, 1) + " °C\n";
    reply += "Humidity: " + String(humidity, 1) + " %\n";
    reply += "Distance: " + String(distanceCm, 1) + " cm\n";
    reply += "Temp Threshold: " + String(tempThreshold, 1) + " °C\n";
    reply += "Dist Threshold: " + String(distThreshold, 1) + " cm\n";
    reply += "LED: " + String(digitalRead(ledPin) ? "ON" : "OFF") + "\n";
    reply += "Buzzer: " + String(digitalRead(buzzerPin) ? "ON" : "OFF");
    sendDiscordMessage(reply);
  }

  else if (cmd.startsWith("!temp ")) {
    float val = cmd.substring(6).toFloat();
    if (val > 0) {
      tempThreshold = val;
      sendDiscordMessage("✅ Temperature threshold set to " + String(tempThreshold, 1) + " °C");
    } else {
      sendDiscordMessage("❌ Usage: !temp 38");
    }
  }

  else if (cmd.startsWith("!dist ")) {
    float val = cmd.substring(6).toFloat();
    if (val > 0) {
      distThreshold = val;
      sendDiscordMessage("✅ Distance threshold set to " + String(distThreshold, 1) + " cm");
    } else {
      sendDiscordMessage("❌ Usage: !dist 15");
    }
  }

  else if (cmd == "!ledon") {
    digitalWrite(ledPin, HIGH);
    sendDiscordMessage("💡 LED turned ON");
  }
  else if (cmd == "!ledoff") {
    digitalWrite(ledPin, LOW);
    sendDiscordMessage("💡 LED turned OFF");
  }

  else if (cmd == "!buzzon") {
    digitalWrite(buzzerPin, HIGH);
    sendDiscordMessage("🔔 Buzzer turned ON");
  }
  else if (cmd == "!buzzoff") {
    digitalWrite(buzzerPin, LOW);
    sendDiscordMessage("🔔 Buzzer turned OFF");
  }

  else if (cmd == "!history") {
    sendDiscordMessage("⏳ Fetching last 5 temperature readings...");
    String result = getThingSpeakData(3, 5);
    sendDiscordMessage(result);
  }

  else if (cmd.startsWith("!read ")) {
    String params = cmd.substring(6);
    int spaceIdx = params.indexOf(' ');
    if (spaceIdx == -1) {
      sendDiscordMessage("❌ Usage: !read <field> <count>\nExample: !read 3 10");
      return;
    }
    int fieldNum = params.substring(0, spaceIdx).toInt();
    int count = params.substring(spaceIdx + 1).toInt();

    sendDiscordMessage("⏳ Fetching data for field " + String(fieldNum) + "...");
    String result = getThingSpeakData(fieldNum, count);
    sendDiscordMessage(result);
  }

  else {
    sendDiscordMessage("❓ Unknown command. Try:\n"
                       "`!status`, `!temp 40`, `!dist 10`, `!ledon`, `!buzzon`, "
                       "`!history`, `!read 3 10`");
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  ThingSpeak.begin(thingSpeakClient);
  dht.begin();

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  digitalWrite(buzzerPin, LOW);

  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Synchronizing time");
  time_t now = time(NULL);
  while (now < 100000) {
    delay(200);
    Serial.print(".");
    now = time(NULL);
  }
  Serial.println("\nTime synced.");

  discordClient.setInsecure();

  sendDiscordMessage("🚀 ESP32 Monitor bot is now online! Type `!status` to begin.");
  Serial.println("Startup message sent.");
}

void loop() {
  if (millis() - lastDiscordCheck > discordCheckInterval) {
    lastDiscordCheck = millis();
    String cmd = getLatestDiscordMessage();
    if (cmd.length() > 0) {
      handleCommand(cmd);
    }
  }

  if (millis() - lastThingSpeakUpdate > thingSpeakInterval) {
    lastThingSpeakUpdate = millis();

    if (WiFi.status() != WL_CONNECTED) {
      Serial.print("Reconnecting WiFi...");
      WiFi.begin(ssid, password);
      int retry = 0;
      while (WiFi.status() != WL_CONNECTED && retry++ < 10) delay(1000);
      if (WiFi.status() == WL_CONNECTED) Serial.println("Done.");
    }

    potValue = analogRead(potPin);
    ldrValue = analogRead(ldrPin);
    temperatureC = dht.readTemperature();
    humidity = dht.readHumidity();
    if (isnan(temperatureC) || isnan(humidity)) {
      temperatureC = 0;
      humidity = 0;
    }
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    long duration = pulseIn(echoPin, HIGH);
    distanceCm = duration * 0.0343 / 2;

    Serial.printf("Pot:%.1f LDR:%.1f Temp:%.1f Hum:%.1f Dist:%.1f cm\n",
                  potValue, ldrValue, temperatureC, humidity, distanceCm);

    if (alertsEnabled) {
      static bool tempAlertSent = false;
      if (temperatureC > tempThreshold && !tempAlertSent) {
        sendDiscordMessage("⚠️ ALERT! High Temperature: " + String(temperatureC, 1) + " °C");
        tempAlertSent = true;
      } else if (temperatureC <= tempThreshold) {
        tempAlertSent = false;
      }

      static bool distAlertSent = false;
      if (distanceCm < distThreshold && distanceCm > 1.0 && !distAlertSent) {
        sendDiscordMessage("🚨 ALERT! Object too close: " + String(distanceCm, 1) + " cm");
        distAlertSent = true;
      } else if (distanceCm >= distThreshold) {
        distAlertSent = false;
      }
    }

    ThingSpeak.setField(1, potValue);
    ThingSpeak.setField(2, ldrValue);
    ThingSpeak.setField(3, temperatureC);
    ThingSpeak.setField(4, humidity);
    ThingSpeak.setField(5, distanceCm);
    int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
    if (x == 200) Serial.println("ThingSpeak OK");
    else Serial.println("ThingSpeak error: " + String(x));
  }
}
