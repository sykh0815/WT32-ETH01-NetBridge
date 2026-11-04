// Minimaler MQTT-3.1.1-Client (QoS 0, Last Will, Retain) ohne externe Bibliothek.
#pragma once
#include <Arduino.h>
#include <WiFiClient.h>

class MiniMqtt {
 public:
  using Callback = void (*)(const String &topic, const String &payload);

  void setCallback(Callback cb) { callback = cb; }
  bool connected() { return client.connected() && up; }
  // Grund des letzten fehlgeschlagenen connect(): 0 = keine Antwort, 1 = kein TCP, 0x10+ = CONNACK-Code (+0x10)
  int lastError = 0;

  bool connect(const char *host, uint16_t port, const String &id, const String &user, const String &pass,
               const String &willTopic, const char *willMessage) {
    stop();
    client.setTimeout(3);
    lastError = 1;
    if (!client.connect(host, port, 3000)) return false;
    lastError = 0;
    const size_t idLen = id.length(), uLen = user.length(), pLen = pass.length(), wtLen = willTopic.length(), wmLen = strlen(willMessage);
    uint8_t flags = 0x02 | 0x04 | 0x20;  // Clean Session, Will, Will Retain
    if (uLen) flags |= 0x80;
    if (uLen && pLen) flags |= 0x40;
    const size_t remaining = 10 + 2 + idLen + 2 + wtLen + 2 + wmLen + (uLen ? 2 + uLen : 0) + (uLen && pLen ? 2 + pLen : 0);
    startPacket(0x10, remaining);
    const uint8_t head[] = {0, 4, 'M', 'Q', 'T', 'T', 4, flags, static_cast<uint8_t>(keepAlive >> 8), static_cast<uint8_t>(keepAlive & 0xFF)};
    client.write(head, sizeof(head));
    str(id);
    str(willTopic);
    str(String(willMessage));
    if (uLen) str(user);
    if (uLen && pLen) str(pass);
    lastRx = lastTx = lastPing = millis();
    const uint32_t start = millis();
    while (millis() - start < 3000) {
      if (client.available() >= 4) {
        uint8_t ack[4];
        client.read(ack, 4);
        up = ack[0] == 0x20 && ack[3] == 0;
        lastError = 0x10 + ack[3];
        if (!up) stop();
        return up;
      }
      delay(5);
    }
    stop();
    return false;
  }

  void stop() {
    up = false;
    client.stop();
  }

  bool publish(const String &topic, const String &payload, bool retain = false) {
    if (!connected()) return false;
    const size_t remaining = 2 + topic.length() + payload.length();
    startPacket(0x30 | (retain ? 1 : 0), remaining);
    str(topic);
    client.write(reinterpret_cast<const uint8_t *>(payload.c_str()), payload.length());
    lastTx = millis();
    return true;
  }

  bool subscribe(const String &topic) {
    if (!connected()) return false;
    startPacket(0x82, 2 + 2 + topic.length() + 1);
    const uint8_t pid[] = {0, 1};
    client.write(pid, 2);
    str(topic);
    client.write(static_cast<uint8_t>(0));
    lastTx = millis();
    return true;
  }

  // Eingehende Pakete verarbeiten und bei Bedarf Keep-Alive senden
  void loop() {
    if (!connected()) return;
    uint8_t guard = 8;
    while (guard-- && client.available()) readPacket();
    const uint32_t now = millis();
    // Ping unabhaengig vom Senden, damit der Broker regelmaessig antwortet (lastRx)
    if (up && now - lastPing > keepAlive * 500UL) {
      const uint8_t ping[] = {0xC0, 0};
      client.write(ping, 2);
      lastPing = lastTx = now;
    }
    if (up && now - lastRx > keepAlive * 1500UL) stop();
  }

 private:
  WiFiClient client;
  Callback callback = nullptr;
  bool up = false;
  uint16_t keepAlive = 30;
  uint32_t lastRx = 0, lastTx = 0, lastPing = 0;

  void startPacket(uint8_t type, size_t remaining) {
    uint8_t buf[5];
    uint8_t n = 0;
    buf[n++] = type;
    do {
      uint8_t b = remaining & 0x7F;
      remaining >>= 7;
      if (remaining) b |= 0x80;
      buf[n++] = b;
    } while (remaining);
    client.write(buf, n);
  }
  void str(const String &s) {
    const uint8_t len[] = {static_cast<uint8_t>(s.length() >> 8), static_cast<uint8_t>(s.length() & 0xFF)};
    client.write(len, 2);
    client.write(reinterpret_cast<const uint8_t *>(s.c_str()), s.length());
  }
  bool readByte(uint8_t &b) {
    const uint32_t start = millis();
    while (!client.available()) {
      if (!client.connected() || millis() - start > 1000) return false;
      delay(1);
    }
    b = client.read();
    return true;
  }
  void readPacket() {
    uint8_t type;
    if (!readByte(type)) return stop();
    size_t len = 0, mult = 1;
    uint8_t b;
    do {
      if (!readByte(b)) return stop();
      len += (b & 0x7F) * mult;
      mult <<= 7;
    } while (b & 0x80);
    if (len > 1024) {  // zu gross: verwerfen
      for (size_t i = 0; i < len; ++i) if (!readByte(b)) return stop();
      return;
    }
    String body;
    body.reserve(len);
    for (size_t i = 0; i < len; ++i) {
      if (!readByte(b)) return stop();
      body += static_cast<char>(b);
    }
    lastRx = millis();
    if ((type & 0xF0) == 0x30 && len >= 2) {  // PUBLISH (QoS 0)
      const size_t tl = (static_cast<uint8_t>(body[0]) << 8) | static_cast<uint8_t>(body[1]);
      if (2 + tl <= len && callback) callback(body.substring(2, 2 + tl), body.substring(2 + tl));
    }
  }
};
