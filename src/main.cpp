#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_private/wifi.h>
#include <esp_eth.h>
#include <esp_eth_netif_glue.h>
#include <dhcpserver/dhcpserver.h>
#include <freertos/queue.h>
#include <esp_system.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>
#include "oui_table.h"

// Zwei Betriebsarten (Umschaltung im Webinterface, danach Neustart):
//  NAT:    Ethernet-LAN ist ein eigenes Netz 192.168.50.0/24 mit DHCP-Server; WLAN ist der Upstream.
//  Bridge: Ethernet-Frames werden 1:1 ins WLAN weitergereicht. Das LAN-Geraet holt sich seine
//          IP direkt vom Router. Da ein WLAN-Client nur mit seiner eigenen MAC senden darf,
//          werden die MAC-Adressen umgeschrieben (Verfahren wie Espressifs Beispiel "sta2eth").
//          Nur EIN Geraet am LAN-Port, nur IPv4.

// NAT/BRIDGE: WLAN-Client, Geraet am LAN-Port.  AP_NAT/AP_BRIDGE: LAN-Port am Router, eigenes WLAN.
enum BridgeMode : uint8_t { MODE_NAT = 0, MODE_BRIDGE = 1, MODE_AP_NAT = 2, MODE_AP_BRIDGE = 3 };

String htmlEscape(const String &text);
String jsonEscape(const String &text);
int wifiPercent();
String signalClass(int percent);
String encryptionName(wifi_auth_mode_t mode);
String pageHeader(const String &title);
String pageFooter();
void detectLanguage();
void setLanguage();
String languageSwitchHtml();
inline const char *T(const char *de, const char *en);
String signalMeterHtml();
String macToString(const uint8_t *mac);
String lanStatusJson();
String modeKey();
String wifiClientsJson();
void showStatus();
void showNetworks();
void showHome();
void connectToRouter();
void logDiagnostics();
void handleRouterConnection();
void onStaDisconnectEvent(void *argument, esp_event_base_t eventBase, int32_t eventId, void *eventData);
const char *staReasonText();
void resetReconnect();
void resumeRouterConnection();
void saveSettings();
void saveMode();
void saveApPassword();
bool isValidWifiPassword(const String &password);
void onEthernetEvent(void *argument, esp_event_base_t eventBase, int32_t eventId, void *eventData);
void onIpEvent(void *argument, esp_event_base_t eventBase, int32_t eventId, void *eventData);
void onWifiDriverEvent(void *argument, esp_event_base_t eventBase, int32_t eventId, void *eventData);
void onNetworkEvent(WiFiEvent_t event);
bool installEthernetDriver();
void startNatLan();
void startBridgeLan();
void startLanServices();
void startApNatUplink();
void startApBridge();
bool isApMode();
void onUplinkIpEvent(void *argument, esp_event_base_t eventBase, int32_t eventId, void *eventData);
void onApDriverEvent(void *argument, esp_event_base_t eventBase, int32_t eventId, void *eventData);
bool isValidSsid(const String &ssid);
bool applyApCredentials(String ssid, const String &password, const String &repeat, String &error, bool openWifi);
String openWifiWarningHtml();
void saveFirewall();
String firewallSectionHtml();
String firewallStatusJson();
void fwLoad();
void onApStaEvent(void *argument, esp_event_base_t eventBase, int32_t eventId, void *eventData);
bool webPasswordSet();
bool fwIsApStation(const uint8_t *mac);
bool fwIsPrivateOrLocal(uint32_t ip);
void useCountingTransmit();
esp_err_t onApNatUplinkInput(esp_eth_handle_t handle, uint8_t *buffer, uint32_t len, void *priv);
String webPasswordSectionHtml();

constexpr int ETH_PHY_POWER_PIN = 16;
constexpr int ETH_PHY_ADDRESS = 1;
constexpr char FIRMWARE_VERSION[] = "3.4";
constexpr char PRODUCT_NAME[] = "WT32-ETH01 NetBridge";
constexpr char BRIDGE_HOSTNAME[] = "wt32-eth01-netbridge";  // Name im Router (z. B. http://wt32-eth01-netbridge.fritz.box)
constexpr char SETUP_AP_SSID[] = "WT32-ETH01-NetBridge-Setup";
// Fruehere Standardnamen des Einrichtungs-WLANs: gespeichert = nie selbst geaendert -> neuer Standardname
const char *const OLD_SETUP_AP_SSIDS[] = {"WT32-NetBridge-Setup", "WT32-Bridge-Setup"};
// Standard-Passwort des Einrichtungs-WLANs. Leer = offenes WLAN beim ersten Start; das Webinterface
// fordert dann auffaellig dazu auf, ein Passwort festzulegen.
constexpr char SETUP_AP_PASSWORD[] = "";

WebServer webServer(80);
DNSServer dnsServer;          // Captive Portal: beantwortet im Einrichtungs-WLAN jede Namensanfrage mit 192.168.4.1
bool captivePortal = false;
Preferences preferences;
BridgeMode bridgeMode = MODE_NAT;
esp_netif_t *ethernetNetif = nullptr;
esp_eth_handle_t ethernetHandle = nullptr;
String routerSsid;
String routerPassword;
String apSsid;           // Name des eigenen WLANs (Einrichtungs-WLAN bzw. Access Point)
String setupApPassword;  // aktuelles Passwort des Einrichtungs-WLANs (NVS, sonst Standard; leer = offen)
bool apOpenChosen = false;  // offenes WLAN bewusst gewaehlt (statt "noch kein Passwort gesetzt")
volatile bool ethernetLinkUp = false;
volatile bool ethernetLanReady = false;
volatile bool wifiConnected = false;
volatile bool ethernetServicesPending = false;
uint32_t restartAtMs = 0;
uint32_t bootCounterClearAtMs = 0;    // Notfall-Reset: Zaehler fuer schnelles Aus-/Einschalten
bool recoveryTriggered = false;
volatile bool apDnsUpdatePending = false;  // AP-NAT: DNS des Routers an WLAN-Clients weitergeben
volatile int apStationCount = 0;          // AP-Bridge: verbundene WLAN-Geraete
QueueHandle_t apForwardQueue = nullptr;   // AP-Bridge: Ethernet -> WLAN (WLAN ist langsamer)
esp_netif_t *apNetif = nullptr;          // Netzwerk-Schnittstelle des Access Points
struct ForwardFrame { uint8_t *data; uint16_t len; };
bool scanPausedConnect = false;  // Verbindungsversuche waehrend eines WLAN-Scans angehalten
uint32_t scanStartedMs = 0;

// Verbindungsversuche zum Router: Jeder Versuch durchsucht die Funkkanaele. Weil Einrichtungs-WLAN und
// Router-Verbindung denselben Funkteil nutzen, ist das Einrichtungs-WLAN waehrenddessen kaum erreichbar.
// Daher: Versuche mit wachsendem Abstand statt pausenlos, und - wenn bekannt - nur auf dem Kanal des Routers.
constexpr uint32_t RECONNECT_MIN_MS = 10000;
constexpr uint32_t RECONNECT_MAX_MS = 120000;
uint32_t reconnectDelayMs = RECONNECT_MIN_MS;
uint32_t nextConnectAttemptMs = 0;   // 0 = kein Versuch geplant
uint32_t failedConnectAttempts = 0;
volatile uint8_t lastStaDisconnectReason = 0;
volatile bool staGotConnected = false;
volatile bool staLinkUp = false;     // mit dem Router verbunden (evtl. noch ohne IP)
uint8_t routerChannel = 0;           // Kanal und BSSID der letzten erfolgreichen Verbindung
uint8_t routerBssid[6] = {};

// Informationen ueber die Geraete am LAN-Port
struct LanClient {
  bool used;
  uint8_t mac[6];
  uint32_t ip;          // Netzwerk-Byte-Reihenfolge wie esp_ip4_addr_t (0 = unbekannt)
  uint32_t assignedMs;  // millis() bei Vergabe bzw. Erkennung
};
constexpr int MAX_LAN_CLIENTS = 4;
LanClient lanClients[MAX_LAN_CLIENTS] = {};
portMUX_TYPE lanClientsLock = portMUX_INITIALIZER_UNLOCKED;

// Zusatzinfos zu allen Geraeten (WLAN und LAN), gelernt aus mitgelesenen DHCP-Paketen und WLAN-Ereignissen
struct ClientInfo {
  bool used;
  uint8_t mac[6];
  char name[33];          // Geraetename aus DHCP (Option 12 bzw. 81), leer = unbekannt
  uint32_t ip;            // aus DHCP-ACK des Routers (Bridge-Modi), Netzwerk-Byte-Reihenfolge
  uint32_t connectedMs;   // Anmeldung am eigenen WLAN (0 = unbekannt)
  uint32_t leaseStartMs;  // letzte DHCP-Vergabe bzw. -Verlaengerung (0 = unbekannt)
  uint32_t leaseSeconds;  // Dauer der Vergabe (0 = die des eigenen DHCP-Servers)
  uint32_t touchedMs;     // letzte Aenderung (bei voller Liste wird der aelteste Eintrag ersetzt)
};
constexpr int MAX_CLIENT_INFOS = 16;
ClientInfo clientInfos[MAX_CLIENT_INFOS] = {};
portMUX_TYPE clientInfoLock = portMUX_INITIALIZER_UNLOCKED;

// AP-Bridge: Geraete im Heimnetz, erkannt an ihren Rundsendungen (ARP, DHCP, Multicast) am LAN-Port
struct Neighbor {
  bool used;
  uint8_t mac[6];
  uint32_t ip;      // Netzwerk-Byte-Reihenfolge, 0 = unbekannt
  char name[33];    // aus DHCP, leer = unbekannt
  uint32_t lastMs;  // zuletzt gesehen
};
constexpr int MAX_NEIGHBORS = 32;
Neighbor neighbors[MAX_NEIGHBORS] = {};
portMUX_TYPE neighborLock = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t homeIp = 0;    // eigene Adresse und Netzmaske im Heimnetz (Netzwerk-Byte-Reihenfolge)
volatile uint32_t homeMask = 0;

volatile int ethernetSpeedMbit = 0;
volatile bool ethernetFullDuplex = false;
volatile uint32_t ethernetLinkSinceMs = 0;

// Zustand des Bridge-Modus (wird aus den Empfangs-Tasks von WLAN und Ethernet benutzt)
uint8_t staMac[6] = {};
uint8_t ethMac[6] = {};   // MAC der Ethernet-Schnittstelle (AP-Bridge: Adresse des Webinterface im Heimnetz)
uint8_t lanDeviceMac[6] = {};
volatile bool lanDeviceKnown = false;
volatile bool bridgeWifiLinked = false;
volatile uint32_t framesToWifi = 0;

// Datenrate am LAN-Port: Bytezaehler in den Empfangs-/Sendewegen (ein atomares Addieren pro Frame),
// die Rate wird einmal pro Sekunde in loop() berechnet
uint32_t lanRxBytes = 0;  // vom Kabel empfangen
uint32_t lanTxBytes = 0;  // ins Kabel gesendet
uint64_t lanRxTotal = 0;
uint64_t lanTxTotal = 0;
uint32_t lanRxRate = 0;   // Bit pro Sekunde
uint32_t lanTxRate = 0;
uint32_t lanRxPeak = 0;
uint32_t lanTxPeak = 0;
inline void countLanRx(uint32_t len) { __atomic_fetch_add(&lanRxBytes, len, __ATOMIC_RELAXED); }
inline void countLanTx(uint32_t len) { __atomic_fetch_add(&lanTxBytes, len, __ATOMIC_RELAXED); }
volatile uint32_t framesToLan = 0;
volatile uint32_t framesDropped = 0;

// ---------------------------------------------------------------------------
// Hilfsfunktionen
// ---------------------------------------------------------------------------

String htmlEscape(const String &text) {
  String escaped;
  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text[i];
    if (c == '&') escaped += "&amp;";
    else if (c == '<') escaped += "&lt;";
    else if (c == '>') escaped += "&gt;";
    else if (c == '\"') escaped += "&quot;";
    else if (c == '\'') escaped += "&#39;";
    else escaped += c;
  }
  return escaped;
}

String jsonEscape(const String &text) {
  String escaped;
  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text[i];
    if (c == '\\' || c == '\"') escaped += '\\';
    if (c == '\n') escaped += "\\n";
    else if (c == '\r') escaped += "\\r";
    else escaped += c;
  }
  return escaped;
}

String macToString(const uint8_t *mac) {
  char text[18];
  snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(text);
}

int wifiPercent() {
  return wifiConnected ? constrain((WiFi.RSSI() + 90) * 100 / 60, 0, 100) : 0;
}

String signalClass(int percent) {
  if (!wifiConnected) return "off";
  if (percent < 35) return "weak";
  if (percent < 65) return "fair";
  return "good";
}

String encryptionName(wifi_auth_mode_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN: return T("Offen", "Open");
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Enterprise";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default: return T("Unbekannt", "Unknown");
  }
}

// Merkt sich MAC/IP eines LAN-Geraets fuer die Anzeige. Darf aus jedem Task aufgerufen werden.
void rememberLanClient(const uint8_t *mac, uint32_t ip) {
  portENTER_CRITICAL(&lanClientsLock);
  int slot = -1;
  for (int i = 0; i < MAX_LAN_CLIENTS; ++i) {
    if (lanClients[i].used && memcmp(lanClients[i].mac, mac, 6) == 0) { slot = i; break; }
  }
  if (slot < 0) {
    for (int i = 0; i < MAX_LAN_CLIENTS; ++i) {
      if (!lanClients[i].used) { slot = i; break; }
    }
  }
  if (slot < 0) {
    slot = 0;  // Liste voll: aeltesten Eintrag ersetzen
    for (int i = 1; i < MAX_LAN_CLIENTS; ++i) {
      if (lanClients[i].assignedMs < lanClients[slot].assignedMs) slot = i;
    }
  }
  const bool changed = !lanClients[slot].used || (ip != 0 && lanClients[slot].ip != ip);
  lanClients[slot].used = true;
  memcpy(lanClients[slot].mac, mac, 6);
  if (ip != 0) lanClients[slot].ip = ip;
  if (changed) lanClients[slot].assignedMs = millis();
  portEXIT_CRITICAL(&lanClientsLock);
}

void clearLanClients() {
  portENTER_CRITICAL(&lanClientsLock);
  for (LanClient &client : lanClients) client.used = false;
  lanDeviceKnown = false;
  portEXIT_CRITICAL(&lanClientsLock);
}

// ---------------------------------------------------------------------------
// Bridge-Modus: MAC-Adressen umschreiben
// ---------------------------------------------------------------------------

constexpr uint16_t BR_ETHERTYPE_IPV4 = 0x0800;
constexpr uint16_t BR_ETHERTYPE_ARP = 0x0806;
constexpr size_t BR_ETH_HEADER_LEN = 14;
constexpr size_t BR_DHCP_CHADDR_OFFSET = 28;
constexpr size_t BR_DHCP_YIADDR_OFFSET = 16;
constexpr size_t BR_DHCP_OPTIONS_OFFSET = 240;
constexpr uint8_t BR_DHCP_MSG_ACK = 5;

inline uint16_t readBe16(const uint8_t *p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

// Sucht eine DHCP-Option. Liefert einen Zeiger auf den Optionskopf (Code, Laenge, Daten) oder nullptr.
uint8_t *findDhcpOption(uint8_t *options, const uint8_t *end, uint8_t code) {
  while (options < end && options[0] != 255) {
    if (options[0] == 0) { ++options; continue; }
    if (options + 2 > end) break;
    uint8_t *next = options + 2 + options[1];
    if (next > end) break;
    if (options[0] == code) return options;
    options = next;
  }
  return nullptr;
}

void updateUdpChecksum(const uint8_t *ipHeader, uint8_t *udp, uint16_t udpLen) {
  udp[6] = 0;
  udp[7] = 0;
  uint32_t sum = 0;
  for (int i = 12; i < 20; i += 2) sum += readBe16(ipHeader + i);  // Quell- und Ziel-IP
  sum += 17 + udpLen;                                             // Protokoll UDP + Laenge
  for (uint16_t i = 0; i + 1 < udpLen; i += 2) sum += readBe16(udp + i);
  if (udpLen & 1) sum += static_cast<uint32_t>(udp[udpLen - 1]) << 8;
  while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
  uint16_t result = static_cast<uint16_t>(~sum);
  if (result == 0) result = 0xFFFF;
  udp[6] = result >> 8;
  udp[7] = result & 0xFF;
}

// ---------------------------------------------------------------------------
// Geraete-Infos: Name, Hersteller, Verbindungsdauer, Restlaufzeit der DHCP-Vergabe
// ---------------------------------------------------------------------------

inline uint32_t stampMs() {
  const uint32_t now = millis();
  return now ? now : 1;  // 0 bedeutet "unbekannt"
}

// Sucht den Eintrag zu einer MAC; legt ihn bei create=true an (bei voller Liste wird der aelteste ersetzt).
// Nur mit gehaltenem clientInfoLock aufrufen.
ClientInfo *clientInfoSlot(const uint8_t *mac, bool create) {
  ClientInfo *freeSlot = nullptr;
  ClientInfo *oldest = nullptr;
  for (ClientInfo &info : clientInfos) {
    if (!info.used) {
      if (freeSlot == nullptr) freeSlot = &info;
      continue;
    }
    if (memcmp(info.mac, mac, 6) == 0) return &info;
    if (oldest == nullptr || static_cast<int32_t>(info.touchedMs - oldest->touchedMs) < 0) oldest = &info;
  }
  if (!create) return nullptr;
  ClientInfo *slot = freeSlot != nullptr ? freeSlot : oldest;
  memset(slot, 0, sizeof(*slot));
  slot->used = true;
  memcpy(slot->mac, mac, 6);
  return slot;
}

// AP-Modi: Zeitpunkt der Anmeldung am eigenen WLAN merken
void noteClientConnected(const uint8_t *mac) {
  portENTER_CRITICAL(&clientInfoLock);
  ClientInfo *info = clientInfoSlot(mac, true);
  info->connectedMs = stampMs();
  info->touchedMs = millis();
  portEXIT_CRITICAL(&clientInfoLock);
}

// Private (zufaellige) MAC-Adressen, wie sie Smartphones und Laptops standardmaessig nutzen
inline bool macIsPrivate(const uint8_t *mac) { return (mac[0] & 0x02) != 0; }

// Hersteller anhand der ersten drei MAC-Bytes (Tabelle aus tools/gen_oui.py), nullptr = unbekannt
const char *macVendor(const uint8_t *mac) {
  if (macIsPrivate(mac) || (mac[0] & 0x01)) return nullptr;
  const uint32_t key = (static_cast<uint32_t>(mac[0]) << 16) | (static_cast<uint32_t>(mac[1]) << 8) | mac[2];
  uint32_t low = 0;
  uint32_t high = OUI_TABLE_SIZE;
  while (low < high) {
    const uint32_t middle = (low + high) / 2;
    const uint32_t prefix = OUI_TABLE[middle] >> 8;
    if (prefix == key) return OUI_VENDORS[OUI_TABLE[middle] & 0xFF];
    if (prefix < key) low = middle + 1;
    else high = middle;
  }
  return nullptr;
}

// Uebernimmt einen Geraetenamen: nur druckbare ASCII-Zeichen, hoechstens 32, optional bis zum ersten Punkt
void copyClientName(char *target, const uint8_t *source, size_t length, bool stopAtDot) {
  size_t used = 0;
  for (size_t i = 0; i < length && used < 32; ++i) {
    const uint8_t c = source[i];
    if (c == 0 || (stopAtDot && c == '.')) break;
    if (c >= 0x20 && c < 0x7F) target[used++] = static_cast<char>(c);
  }
  while (used > 0 && target[used - 1] == ' ') --used;
  target[used] = 0;
}

// Gehoert die Adresse zum Heimnetz? (fremde Adressen, z. B. vom Router weitergeleitete, nicht zuordnen)
bool inHomeNet(uint32_t ip) {
  if (ip == 0 || ip == 0xFFFFFFFFu) return false;
  const uint32_t mask = homeMask;
  if (mask != 0) return (ip & mask) == (homeIp & mask) && (ip & ~mask) != (~mask);  // ohne Broadcast-Adresse
  const uint32_t host = __builtin_bswap32(ip);
  return fwIsPrivateOrLocal(host) && (host >> 28) != 0xE;
}

void noteNeighbor(const uint8_t *mac, uint32_t ip, const char *name) {
  if ((mac[0] & 0x01) || memcmp(mac, ethMac, 6) == 0 || fwIsApStation(mac)) return;
  bool newName = false;
  portENTER_CRITICAL(&neighborLock);
  Neighbor *slot = nullptr;
  Neighbor *freeSlot = nullptr;
  Neighbor *oldest = nullptr;
  for (Neighbor &entry : neighbors) {
    if (!entry.used) {
      if (freeSlot == nullptr) freeSlot = &entry;
      continue;
    }
    if (memcmp(entry.mac, mac, 6) == 0) {
      slot = &entry;
      break;
    }
    if (oldest == nullptr || static_cast<int32_t>(entry.lastMs - oldest->lastMs) < 0) oldest = &entry;
  }
  if (slot == nullptr) {
    slot = freeSlot != nullptr ? freeSlot : oldest;  // Liste voll: am laengsten nicht gesehenes Geraet ersetzen
    memset(slot, 0, sizeof(*slot));
    slot->used = true;
    memcpy(slot->mac, mac, 6);
  }
  if (ip != 0) slot->ip = ip;
  if (name != nullptr && name[0] != 0 && strcmp(slot->name, name) != 0) {
    strncpy(slot->name, name, 32);
    slot->name[32] = 0;
    newName = true;
  }
  slot->lastMs = stampMs();
  portEXIT_CRITICAL(&neighborLock);
  if (newName) Serial.printf("Heimnetz: %02X:%02X:%02X:%02X:%02X:%02X heisst \"%s\"\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], name);
}

// AP-Bridge: Rundsendung aus dem Heimnetz auswerten (Absender-MAC, bei ARP und IPv4 auch die IP)
void learnNeighbor(const uint8_t *frame, uint32_t len) {
  if (len < BR_ETH_HEADER_LEN || !(frame[0] & 0x01)) return;  // nur Broadcast/Multicast
  const uint8_t *src = frame + 6;
  const uint16_t type = readBe16(frame + 12);
  uint32_t ip = 0;
  if (type == BR_ETHERTYPE_ARP) {
    if (len < BR_ETH_HEADER_LEN + 28) return;
    const uint8_t *arp = frame + BR_ETH_HEADER_LEN;
    if (readBe16(arp) != 1 || readBe16(arp + 2) != BR_ETHERTYPE_IPV4 || memcmp(arp + 8, src, 6) != 0) return;
    memcpy(&ip, arp + 14, 4);
  } else if (type == BR_ETHERTYPE_IPV4) {
    if (len < BR_ETH_HEADER_LEN + 20 || (frame[BR_ETH_HEADER_LEN] >> 4) != 4) return;
    memcpy(&ip, frame + BR_ETH_HEADER_LEN + 12, 4);
  } else if (type != 0x86DD) {
    return;  // ausser IPv6 nichts anderes auswerten
  }
  noteNeighbor(src, inHomeNet(ip) ? ip : 0, nullptr);
}

// Liest DHCP-Pakete mit, ohne sie zu veraendern:
//  - Anfrage eines Geraets (Port 68 -> 67): Geraetename (Option 12, sonst 81). In den NAT-Modi beantwortet
//    der eigene DHCP-Server jede Anfrage (REQUEST) sofort - damit beginnt die Vergabe.
//  - Bestaetigung (ACK) des Routers (Port 67 -> 68, Bridge-Modi): zugewiesene IP und Dauer (Option 51).
// Darf aus jedem Task aufgerufen werden.
// homeNetwork: Paket kommt im AP-Bridge-Modus aus dem Heimnetz (Anfragen landen dann in der Heimnetz-Liste)
void sniffDhcp(const uint8_t *frame, uint32_t len, bool homeNetwork = false) {
  if (len < BR_ETH_HEADER_LEN + 28 || readBe16(frame + 12) != BR_ETHERTYPE_IPV4) return;
  const uint8_t *ip = frame + BR_ETH_HEADER_LEN;
  if ((ip[0] >> 4) != 4 || ip[9] != 17) return;
  const uint8_t *udp = ip + (ip[0] & 0x0F) * 4;
  if (udp + 8 > frame + len) return;
  const uint16_t srcPort = readBe16(udp);
  const uint16_t dstPort = readBe16(udp + 2);
  const bool request = srcPort == 68 && dstPort == 67;
  const bool reply = srcPort == 67 && dstPort == 68;
  if (!request && !reply) return;
  const uint16_t udpLen = readBe16(udp + 4);
  if (udpLen < 8 + BR_DHCP_OPTIONS_OFFSET || udp + udpLen > frame + len) return;
  uint8_t *dhcp = const_cast<uint8_t *>(udp + 8);
  const uint8_t *end = udp + udpLen;
  if (readBe16(dhcp + 236) != 0x6382 || readBe16(dhcp + 238) != 0x5363) return;  // Magic Cookie
  if (dhcp[1] != 1 || dhcp[2] != 6) return;  // nur Ethernet-Adressen
  const uint8_t *mac = dhcp + BR_DHCP_CHADDR_OFFSET;
  if (mac[0] & 0x01) return;
  uint8_t *options = dhcp + BR_DHCP_OPTIONS_OFFSET;
  const uint8_t *type = findDhcpOption(options, end, 53);
  const uint8_t messageType = (type != nullptr && type[1] == 1) ? type[2] : 0;

  if (request) {
    if (messageType != 1 && messageType != 3 && messageType != 8) return;  // DISCOVER, REQUEST, INFORM
    char name[33] = "";
    const uint8_t *option = findDhcpOption(options, end, 12);
    if (option != nullptr) {
      copyClientName(name, option + 2, option[1], false);
    } else if ((option = findDhcpOption(options, end, 81)) != nullptr && option[1] > 3) {
      // Client FQDN: Flags, 2 Bytes RCODE, dann der Name - als Text oder (Flag E) DNS-kodiert
      if (option[2] & 0x04) {
        const uint8_t labelLen = option[5];
        if (labelLen > 0 && 4 + labelLen <= option[1]) copyClientName(name, option + 6, labelLen, false);
      } else {
        copyClientName(name, option + 5, option[1] - 3, true);
      }
    }
    if (homeNetwork) {
      uint32_t wanted = 0;
      const uint8_t *requested = findDhcpOption(options, end, 50);
      if (requested != nullptr && requested[1] == 4) memcpy(&wanted, requested + 2, 4);
      else memcpy(&wanted, dhcp + 12, 4);  // ciaddr (Verlaengerung)
      noteNeighbor(mac, inHomeNet(wanted) ? wanted : 0, name);
      return;
    }
    const bool ownServer = bridgeMode == MODE_NAT || bridgeMode == MODE_AP_NAT;
    bool newName = false;
    portENTER_CRITICAL(&clientInfoLock);
    ClientInfo *info = clientInfoSlot(mac, true);
    if (name[0] != 0 && strcmp(info->name, name) != 0) {
      strcpy(info->name, name);
      newName = true;
    }
    if (ownServer && messageType == 3) {
      info->leaseStartMs = stampMs();
      info->leaseSeconds = 0;  // Dauer des eigenen DHCP-Servers
    }
    info->touchedMs = millis();
    portEXIT_CRITICAL(&clientInfoLock);
    if (newName) Serial.printf("DHCP: %02X:%02X:%02X:%02X:%02X:%02X heisst \"%s\"\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], name);
  } else if (messageType == BR_DHCP_MSG_ACK) {
    uint32_t assigned;
    memcpy(&assigned, dhcp + BR_DHCP_YIADDR_OFFSET, 4);
    const uint8_t *lease = findDhcpOption(options, end, 51);
    portENTER_CRITICAL(&clientInfoLock);
    ClientInfo *info = clientInfoSlot(mac, false);  // nur Geraete, deren Anfrage wir gesehen haben
    if (info != nullptr) {
      if (assigned != 0) info->ip = assigned;
      if (lease != nullptr && lease[1] == 4) {
        info->leaseSeconds = (static_cast<uint32_t>(lease[2]) << 24) | (static_cast<uint32_t>(lease[3]) << 16) | (static_cast<uint32_t>(lease[4]) << 8) | lease[5];
        info->leaseStartMs = stampMs();
      }
      info->touchedMs = millis();
    }
    portEXIT_CRITICAL(&clientInfoLock);
    if (homeNetwork && info == nullptr && assigned != 0) {
      // Bestaetigung fuer ein Geraet im Heimnetz (nur sichtbar, wenn der Router sie an alle schickt)
      portENTER_CRITICAL(&neighborLock);
      for (Neighbor &entry : neighbors) {
        if (entry.used && memcmp(entry.mac, mac, 6) == 0) entry.ip = assigned;
      }
      portEXIT_CRITICAL(&neighborLock);
    }
  }
}

// Dauer einer Vergabe des eigenen DHCP-Servers in Sekunden (0 = unbekannt)
uint32_t dhcpServerLeaseSeconds(esp_netif_t *netif) {
  uint32_t minutes = 0;
  if (netif == nullptr || esp_netif_dhcps_option(netif, ESP_NETIF_OP_GET, ESP_NETIF_IP_ADDRESS_LEASE_TIME, &minutes, sizeof(minutes)) != ESP_OK) return 0;
  return minutes * 60;
}

// JSON-Felder mit den Zusatzinfos eines Geraets (beginnt mit Komma). Kopiert den Eintrag nach *copy, falls gewuenscht.
String clientInfoJson(const uint8_t *mac, uint32_t serverLeaseSeconds, uint32_t now, ClientInfo *copy = nullptr) {
  ClientInfo info{};
  portENTER_CRITICAL(&clientInfoLock);
  ClientInfo *slot = clientInfoSlot(mac, false);
  if (slot != nullptr) info = *slot;
  portEXIT_CRITICAL(&clientInfoLock);
  if (copy != nullptr) *copy = info;

  String json;
  if (info.name[0] != 0) json += ",\"name\":\"" + jsonEscape(String(info.name)) + "\"";
  const char *vendor = macVendor(mac);
  if (vendor != nullptr) json += ",\"vendor\":\"" + String(vendor) + "\"";
  if (macIsPrivate(mac)) json += ",\"private\":true";
  if (info.connectedMs != 0) json += ",\"since\":" + String((now - info.connectedMs) / 1000);
  const uint32_t total = info.leaseSeconds != 0 ? info.leaseSeconds : serverLeaseSeconds;
  if (info.leaseStartMs != 0 && total != 0 && total != 0xFFFFFFFF) {
    const uint32_t elapsed = (now - info.leaseStartMs) / 1000;
    json += ",\"leaseLeft\":" + String(elapsed >= total ? 0 : total - elapsed);
  }
  return json;
}

// Schreibt die Client-MAC (chaddr und Option 61) in DHCP-Paketen um.
// Viele Router schicken DHCP-Antworten per Unicast an chaddr - das muss die WLAN-MAC der Bridge sein.
void rewriteDhcp(bool fromLan, uint8_t *frame, uint16_t len) {
  uint8_t *ip = frame + BR_ETH_HEADER_LEN;
  if (len < BR_ETH_HEADER_LEN + 20 || (ip[0] >> 4) != 4 || ip[9] != 17) return;
  const size_t ipHeaderLen = (ip[0] & 0x0F) * 4;
  uint8_t *udp = ip + ipHeaderLen;
  if (udp + 8 > frame + len) return;
  const uint16_t srcPort = readBe16(udp);
  const uint16_t dstPort = readBe16(udp + 2);
  const bool isDhcp = fromLan ? (srcPort == 68 && dstPort == 67) : (srcPort == 67 && dstPort == 68);
  if (!isDhcp) return;
  const uint16_t udpLen = readBe16(udp + 4);
  if (udpLen < 8 + BR_DHCP_OPTIONS_OFFSET || udp + udpLen > frame + len) return;

  uint8_t *dhcp = udp + 8;
  const uint8_t *end = udp + udpLen;
  if (readBe16(dhcp + 236) != 0x6382 || readBe16(dhcp + 238) != 0x5363) return;  // Magic Cookie

  const uint8_t *oldMac = fromLan ? lanDeviceMac : staMac;
  const uint8_t *newMac = fromLan ? staMac : lanDeviceMac;
  bool changed = false;
  if (memcmp(dhcp + BR_DHCP_CHADDR_OFFSET, oldMac, 6) == 0) {
    memcpy(dhcp + BR_DHCP_CHADDR_OFFSET, newMac, 6);
    changed = true;
  }
  uint8_t *options = dhcp + BR_DHCP_OPTIONS_OFFSET;
  uint8_t *clientId = findDhcpOption(options, end, 61);
  if (clientId != nullptr && clientId[1] == 7 && clientId[2] == 1 && memcmp(clientId + 3, oldMac, 6) == 0) {
    memcpy(clientId + 3, newMac, 6);
    changed = true;
  }
  if (!fromLan) {
    uint8_t *type = findDhcpOption(options, end, 53);
    if (type != nullptr && type[1] == 1 && type[2] == BR_DHCP_MSG_ACK) {
      uint32_t assigned;
      memcpy(&assigned, dhcp + BR_DHCP_YIADDR_OFFSET, 4);
      if (assigned != 0) {
        rememberLanClient(lanDeviceMac, assigned);
        Serial.printf("Router hat dem LAN-Geraet " IPSTR " zugewiesen\n", IP2STR(reinterpret_cast<esp_ip4_addr_t *>(&assigned)));
      }
    }
  }
  if (changed && (udp[6] != 0 || udp[7] != 0)) updateUdpChecksum(ip, udp, udpLen);
}

// Liefert false, wenn der Frame verworfen werden soll.
bool rewriteFrame(bool fromLan, uint8_t *frame, uint16_t len) {
  if (len < BR_ETH_HEADER_LEN) return false;
  uint8_t *dst = frame;
  uint8_t *src = frame + 6;
  const uint16_t type = readBe16(frame + 12);
  if (type != BR_ETHERTYPE_IPV4 && type != BR_ETHERTYPE_ARP) return false;  // nur IPv4 und ARP

  if (fromLan) {
    if (src[0] & 0x01) return false;  // ungueltige Quelladresse
    if (!lanDeviceKnown) {
      portENTER_CRITICAL(&lanClientsLock);
      memcpy(lanDeviceMac, src, 6);
      lanDeviceKnown = true;
      portEXIT_CRITICAL(&lanClientsLock);
      rememberLanClient(src, 0);
    } else if (memcmp(src, lanDeviceMac, 6) != 0) {
      return false;  // zweites Geraet (z. B. hinter einem Switch) - im Bridge-Modus nicht moeglich
    }
  } else if (!lanDeviceKnown) {
    return true;  // Geraet noch unbekannt: Broadcasts trotzdem durchreichen
  }

  if (type == BR_ETHERTYPE_ARP && len >= BR_ETH_HEADER_LEN + 28) {
    uint8_t *arp = frame + BR_ETH_HEADER_LEN;
    uint8_t *senderMac = arp + 8;
    uint8_t *targetMac = arp + 18;
    if (fromLan) {
      uint32_t senderIp;
      memcpy(&senderIp, arp + 14, 4);
      if (senderIp != 0) rememberLanClient(lanDeviceMac, senderIp);  // erkennt auch feste IPs
      if (memcmp(senderMac, lanDeviceMac, 6) == 0) memcpy(senderMac, staMac, 6);
    } else if (memcmp(targetMac, staMac, 6) == 0) {
      memcpy(targetMac, lanDeviceMac, 6);
    }
  } else if (type == BR_ETHERTYPE_IPV4) {
    rewriteDhcp(fromLan, frame, len);
  }

  if (fromLan) {
    memcpy(src, staMac, 6);
  } else if (memcmp(dst, staMac, 6) == 0) {
    memcpy(dst, lanDeviceMac, 6);
  }
  return true;
}

// ---------------------------------------------------------------------------
// Firewall (zustandslos, nur IPv4)
//
// Geprueft wird der Verkehr der angeschlossenen Geraete: Geraet am LAN-Port (NAT, Bridge) bzw.
// WLAN-Geraete (Access-Point-Modi). Ausgehend gelten Schalter + Regelliste, eingehend (nur in den
// Bridge-Modi, wo es kein NAT gibt) der Schalter "Nur Internet, kein Heimnetz".
// ---------------------------------------------------------------------------

enum FwProto : uint8_t { FW_ANY = 0, FW_TCP = 1, FW_UDP = 2, FW_ICMP = 3 };
struct FwRule {
  uint8_t enabled;
  uint8_t block;      // 1 = sperren, 0 = erlauben
  uint8_t proto;      // FwProto
  uint8_t prefix;     // 0..32
  uint32_t net;       // Host-Byte-Reihenfolge
  uint16_t portFrom;  // 0 = alle Ports
  uint16_t portTo;
};
constexpr int FW_MAX_RULES = 16;
constexpr int FW_MAX_MACS = 16;
constexpr int FW_MAX_STATIONS = 10;

FwRule fwRules[FW_MAX_RULES] = {};
int fwRuleCount = 0;
volatile uint32_t fwHits[FW_MAX_RULES] = {};
volatile uint32_t fwBlocked = 0;
bool fwEnabled = false;
bool fwNoHome = false;
bool fwNoAdmin = false;
bool fwIsolate = false;
bool fwDefaultBlock = false;
bool fwMacFilter = false;
uint8_t fwMacs[FW_MAX_MACS][6] = {};
int fwMacCount = 0;
volatile uint32_t fwRouterIp = 0;   // Router/Gateway im Heimnetz (Host-Byte-Reihenfolge)
volatile uint32_t fwDnsIp = 0;      // DNS-Server im Heimnetz
volatile uint32_t fwOwnIpExtra = 0; // weitere eigene IP der Bridge (Router-Netz bzw. Uplink)
uint8_t apStationMacs[FW_MAX_STATIONS][6] = {};
bool apStationUsed[FW_MAX_STATIONS] = {};
portMUX_TYPE fwLock = portMUX_INITIALIZER_UNLOCKED;

constexpr uint32_t IP_NAT_LAN = 0xC0A83201;  // 192.168.50.1
constexpr uint32_t IP_AP = 0xC0A80401;       // 192.168.4.1

inline uint32_t toHostOrder(uint32_t networkOrder) { return __builtin_bswap32(networkOrder); }
inline uint32_t prefixMask(uint8_t prefix) { return prefix == 0 ? 0 : (0xFFFFFFFFu << (32 - prefix)); }

bool fwIsPrivateOrLocal(uint32_t ip) {
  return (ip >> 24) == 10 || (ip >> 20) == 0xAC1 || (ip >> 16) == 0xC0A8 || (ip >> 16) == 0xA9FE ||
         (ip >> 28) == 0xE || ip == 0xFFFFFFFFu || (ip >> 24) == 127 || ip == 0;
}

bool fwIsOwnIp(uint32_t ip) {
  return ip == IP_NAT_LAN || ip == IP_AP || (fwOwnIpExtra != 0 && ip == fwOwnIpExtra);
}

bool fwDrop() {
  fwBlocked = fwBlocked + 1;
  return false;
}

bool fwMacListed(const uint8_t *mac) {
  for (int i = 0; i < fwMacCount; ++i) {
    if (memcmp(fwMacs[i], mac, 6) == 0) return true;
  }
  return false;
}

bool fwIsApStation(const uint8_t *mac) {
  for (int i = 0; i < FW_MAX_STATIONS; ++i) {
    if (apStationUsed[i] && memcmp(apStationMacs[i], mac, 6) == 0) return true;
  }
  return false;
}

// IPv4-Paket von einem angeschlossenen Geraet. true = durchlassen.
bool fwCheckOutbound(const uint8_t *ip, size_t len) {
  if (len < 20 || (ip[0] >> 4) != 4) return fwDrop();
  const size_t ihl = (ip[0] & 0x0F) * 4;
  if (ihl < 20 || len < ihl) return fwDrop();
  const uint8_t protocol = ip[9];
  uint32_t dstNet;
  memcpy(&dstNet, ip + 16, 4);
  const uint32_t dst = toHostOrder(dstNet);
  const bool firstFragment = ((((ip[6] & 0x1F) << 8) | ip[7]) == 0);
  uint16_t sport = 0, dport = 0;
  if ((protocol == 6 || protocol == 17) && firstFragment && len >= ihl + 4) {
    sport = readBe16(ip + ihl);
    dport = readBe16(ip + ihl + 2);
  }
  if (protocol == 17 && sport == 68 && dport == 67) return true;  // DHCP immer erlauben

  if (fwIsOwnIp(dst)) {
    if (fwNoAdmin && protocol == 6 && dport == 80) return fwDrop();
    return true;  // DHCP, DNS-Weiterleitung, Ping zur Bridge selbst
  }
  if (fwIsolate && isApMode() && (dst >> 8) == (IP_AP >> 8)) return fwDrop();  // anderes WLAN-Geraet
  if (fwNoHome && fwIsPrivateOrLocal(dst)) {
    const bool infra = (dst != 0 && (dst == fwRouterIp || dst == fwDnsIp)) && (protocol == 1 || dport == 53);
    if (!infra) return fwDrop();
  }

  int verdict = -1;
  portENTER_CRITICAL(&fwLock);
  for (int i = 0; i < fwRuleCount; ++i) {
    const FwRule &rule = fwRules[i];
    if (!rule.enabled) continue;
    if (rule.proto == FW_TCP && protocol != 6) continue;
    if (rule.proto == FW_UDP && protocol != 17) continue;
    if (rule.proto == FW_ICMP && protocol != 1) continue;
    const uint32_t mask = prefixMask(rule.prefix);
    if ((dst & mask) != (rule.net & mask)) continue;
    if (rule.portFrom != 0) {
      if (protocol != 6 && protocol != 17) continue;
      if (dport < rule.portFrom || dport > rule.portTo) continue;
    }
    fwHits[i] = fwHits[i] + 1;
    verdict = rule.block;
    break;
  }
  portEXIT_CRITICAL(&fwLock);
  if (verdict < 0) verdict = fwDefaultBlock ? 1 : 0;
  return verdict ? fwDrop() : true;
}

// IPv4-Paket zum angeschlossenen Geraet (nur Bridge-Modi). true = durchlassen.
bool fwCheckInbound(const uint8_t *ip, size_t len) {
  if (!fwNoHome) return true;
  if (len < 20 || (ip[0] >> 4) != 4) return true;
  uint32_t srcNet;
  memcpy(&srcNet, ip + 12, 4);
  const uint32_t src = toHostOrder(srcNet);
  if (src == 0 || src == fwRouterIp || src == fwDnsIp) return true;
  if (fwIsPrivateOrLocal(src)) return fwDrop();
  return true;
}

// Ethernet-Frame pruefen. outbound = vom angeschlossenen Geraet. true = durchlassen.
bool fwCheckFrame(const uint8_t *frame, size_t len, bool outbound) {
  if (!fwEnabled || len < BR_ETH_HEADER_LEN) return true;
  const uint16_t type = readBe16(frame + 12);
  if (type == 0x86DD) return fwDrop();  // IPv6 bei aktiver Firewall sperren (Regeln gelten nur fuer IPv4)
  if (type != BR_ETHERTYPE_IPV4) return true;  // ARP usw.
  return outbound ? fwCheckOutbound(frame + BR_ETH_HEADER_LEN, len - BR_ETH_HEADER_LEN)
                  : fwCheckInbound(frame + BR_ETH_HEADER_LEN, len - BR_ETH_HEADER_LEN);
}

// Frame eines WLAN-Geraets im Access-Point-Modus: Isolation + Firewall
bool fwCheckApFrame(const uint8_t *frame, size_t len) {
  if (!fwEnabled) return true;
  if (fwIsolate && len >= 6 && !(frame[0] & 0x01) && fwIsApStation(frame)) return fwDrop();
  return fwCheckFrame(frame, len, true);
}

// Router und DNS aus einer DHCP-Antwort (ACK) lernen - fuer "Nur Internet, kein Heimnetz" in den Bridge-Modi
void fwLearnDhcp(uint8_t *frame, uint16_t len) {
  if (len < BR_ETH_HEADER_LEN + 20 || readBe16(frame + 12) != BR_ETHERTYPE_IPV4) return;
  uint8_t *ip = frame + BR_ETH_HEADER_LEN;
  if ((ip[0] >> 4) != 4 || ip[9] != 17) return;
  uint8_t *udp = ip + (ip[0] & 0x0F) * 4;
  if (udp + 8 > frame + len || readBe16(udp) != 67 || readBe16(udp + 2) != 68) return;
  const uint16_t udpLen = readBe16(udp + 4);
  if (udpLen < 8 + BR_DHCP_OPTIONS_OFFSET || udp + udpLen > frame + len) return;
  uint8_t *dhcp = udp + 8;
  const uint8_t *end = udp + udpLen;
  if (readBe16(dhcp + 236) != 0x6382 || readBe16(dhcp + 238) != 0x5363) return;
  uint8_t *type = findDhcpOption(dhcp + BR_DHCP_OPTIONS_OFFSET, end, 53);
  if (type == nullptr || type[1] != 1 || type[2] != BR_DHCP_MSG_ACK) return;
  uint32_t value;
  uint8_t *router = findDhcpOption(dhcp + BR_DHCP_OPTIONS_OFFSET, end, 3);
  if (router != nullptr && router[1] >= 4) { memcpy(&value, router + 2, 4); fwRouterIp = toHostOrder(value); }
  uint8_t *dns = findDhcpOption(dhcp + BR_DHCP_OPTIONS_OFFSET, end, 6);
  if (dns != nullptr && dns[1] >= 4) { memcpy(&value, dns + 2, 4); fwDnsIp = toHostOrder(value); }
}

// --- Einstellungen laden/speichern -----------------------------------------

void fwLoad() {
  fwEnabled = preferences.getBool("fw_on", false);
  fwNoHome = preferences.getBool("fw_nohome", false);
  fwNoAdmin = preferences.getBool("fw_noadmin", false);
  fwIsolate = preferences.getBool("fw_isolate", false);
  fwDefaultBlock = preferences.getBool("fw_defblk", false);
  fwMacFilter = preferences.getBool("fw_macon", false);
  fwRuleCount = 0;
  const size_t ruleBytes = preferences.isKey("fw_rules") ? preferences.getBytesLength("fw_rules") : 0;  // isKey: keine Fehlermeldung, solange nichts gespeichert ist
  if (ruleBytes > 0 && ruleBytes % sizeof(FwRule) == 0 && ruleBytes <= sizeof(fwRules)) {
    preferences.getBytes("fw_rules", fwRules, ruleBytes);
    fwRuleCount = ruleBytes / sizeof(FwRule);
  }
  fwMacCount = 0;
  const size_t macBytes = preferences.isKey("fw_macs") ? preferences.getBytesLength("fw_macs") : 0;
  if (macBytes > 0 && macBytes % 6 == 0 && macBytes <= sizeof(fwMacs)) {
    preferences.getBytes("fw_macs", fwMacs, macBytes);
    fwMacCount = macBytes / 6;
  }
  if (fwEnabled) Serial.printf("Firewall aktiv: %d Regeln, %d MAC-Adressen\n", fwRuleCount, fwMacCount);
}

void fwSave() {
  preferences.putBool("fw_on", fwEnabled);
  preferences.putBool("fw_nohome", fwNoHome);
  preferences.putBool("fw_noadmin", fwNoAdmin);
  preferences.putBool("fw_isolate", fwIsolate);
  preferences.putBool("fw_defblk", fwDefaultBlock);
  preferences.putBool("fw_macon", fwMacFilter);
  if (fwRuleCount > 0) preferences.putBytes("fw_rules", fwRules, fwRuleCount * sizeof(FwRule));
  else preferences.remove("fw_rules");
  if (fwMacCount > 0) preferences.putBytes("fw_macs", fwMacs, fwMacCount * 6);
  else preferences.remove("fw_macs");
}

// "192.168.1.0/24", "192.168.1.10", "*" -> Netz + Praefix
bool fwParseTarget(String text, uint32_t &net, uint8_t &prefix) {
  text.trim();
  if (text == "*" || text == "any" || text == "0.0.0.0/0") { net = 0; prefix = 0; return true; }
  int slash = text.indexOf('/');
  String address = slash >= 0 ? text.substring(0, slash) : text;
  int bits = 32;
  if (slash >= 0) {
    const String bitsText = text.substring(slash + 1);
    if (bitsText.isEmpty()) return false;
    for (size_t i = 0; i < bitsText.length(); ++i) if (bitsText[i] < '0' || bitsText[i] > '9') return false;
    bits = bitsText.toInt();
    if (bits < 0 || bits > 32) return false;
  }
  uint32_t value = 0;
  int parts = 0;
  int current = -1;
  for (size_t i = 0; i <= address.length(); ++i) {
    const char c = i < address.length() ? address[i] : '.';
    if (c >= '0' && c <= '9') {
      current = (current < 0 ? 0 : current) * 10 + (c - '0');
      if (current > 255) return false;
    } else if (c == '.') {
      if (current < 0 || parts >= 4) return false;
      value = (value << 8) | static_cast<uint32_t>(current);
      ++parts;
      current = -1;
    } else {
      return false;
    }
  }
  if (parts != 4) return false;
  prefix = static_cast<uint8_t>(bits);
  net = value & prefixMask(prefix);
  return true;
}

// "" -> alle, "443", "1000-2000"
bool fwParsePorts(String text, uint16_t &from, uint16_t &to) {
  text.trim();
  if (text.isEmpty()) { from = 0; to = 0; return true; }
  const int dash = text.indexOf('-');
  const String first = dash >= 0 ? text.substring(0, dash) : text;
  const String second = dash >= 0 ? text.substring(dash + 1) : text;
  auto parse = [](const String &part, long &out) {
    if (part.isEmpty() || part.length() > 5) return false;
    for (size_t i = 0; i < part.length(); ++i) if (part[i] < '0' || part[i] > '9') return false;
    out = part.toInt();
    return out >= 1 && out <= 65535;
  };
  long a = 0, b = 0;
  if (!parse(first, a) || !parse(second, b) || b < a) return false;
  from = static_cast<uint16_t>(a);
  to = static_cast<uint16_t>(b);
  return true;
}

bool fwParseMac(String text, uint8_t *mac) {
  text.trim();
  text.replace("-", ":");
  if (text.length() != 17) return false;
  for (int i = 0; i < 6; ++i) {
    char hex[3] = {text[i * 3], text[i * 3 + 1], 0};
    if (i < 5 && text[i * 3 + 2] != ':') return false;
    if (!isxdigit(static_cast<unsigned char>(hex[0])) || !isxdigit(static_cast<unsigned char>(hex[1]))) return false;
    mac[i] = static_cast<uint8_t>(strtoul(hex, nullptr, 16));
  }
  return true;
}

String fwTargetText(const FwRule &rule) {
  if (rule.prefix == 0) return "*";
  String text = String(rule.net >> 24) + "." + String((rule.net >> 16) & 0xFF) + "." + String((rule.net >> 8) & 0xFF) + "." + String(rule.net & 0xFF);
  if (rule.prefix != 32) text += "/" + String(rule.prefix);
  return text;
}

String fwPortText(const FwRule &rule) {
  if (rule.portFrom == 0) return "";
  if (rule.portFrom == rule.portTo) return String(rule.portFrom);
  return String(rule.portFrom) + "-" + String(rule.portTo);
}

// Ethernet -> WLAN (laeuft im Empfangs-Task des Ethernet-Treibers)
esp_err_t onLanFrame(esp_eth_handle_t handle, uint8_t *buffer, uint32_t len, void *priv) {
  countLanRx(len);
  sniffDhcp(buffer, len);  // vor dem Umschreiben: chaddr ist noch die MAC des LAN-Geraets
  if (bridgeWifiLinked && len <= 1600 && fwCheckFrame(buffer, len, true) && rewriteFrame(true, buffer, static_cast<uint16_t>(len))) {
    if (esp_wifi_internal_tx(WIFI_IF_STA, buffer, static_cast<uint16_t>(len)) == ESP_OK) framesToWifi = framesToWifi + 1;
    else framesDropped = framesDropped + 1;
  } else {
    framesDropped = framesDropped + 1;
  }
  free(buffer);
  return ESP_OK;
}

// WLAN -> Ethernet (laeuft im WLAN-Task)
esp_err_t onWifiFrame(void *buffer, uint16_t len, void *eb) {
  fwLearnDhcp(static_cast<uint8_t *>(buffer), len);
  if (ethernetLinkUp && fwCheckFrame(static_cast<uint8_t *>(buffer), len, false) && rewriteFrame(false, static_cast<uint8_t *>(buffer), len)) {
    sniffDhcp(static_cast<uint8_t *>(buffer), len);  // nach dem Umschreiben: chaddr ist die MAC des LAN-Geraets
    if (esp_eth_transmit(ethernetHandle, buffer, len) == ESP_OK) {
      framesToLan = framesToLan + 1;
      countLanTx(len);
    } else {
      framesDropped = framesDropped + 1;
    }
  }
  esp_wifi_internal_free_rx_buffer(eb);
  return ESP_OK;
}

// ---------------------------------------------------------------------------
// Sprache des Webinterface (Deutsch/Englisch)
// ---------------------------------------------------------------------------

bool uiEnglish = false;  // wird zu Beginn jeder Anfrage per detectLanguage() gesetzt

// Waehlt den Text in der aktuellen Sprache
inline const char *T(const char *de, const char *en) { return uiEnglish ? en : de; }

// Sprache aus Cookie "lang" (vom Umschalter gesetzt), sonst aus dem Browser (Accept-Language)
void detectLanguage() {
  const String cookie = webServer.header("Cookie");
  if (cookie.indexOf("lang=en") >= 0) { uiEnglish = true; return; }
  if (cookie.indexOf("lang=de") >= 0) { uiEnglish = false; return; }
  String accept = webServer.header("Accept-Language");
  accept.toLowerCase();
  uiEnglish = !accept.startsWith("de");
}

// /lang?l=de oder /lang?l=en: Sprache fuer ein Jahr im Browser merken und zur Startseite
void setLanguage() {
  const bool english = webServer.arg("l") == "en";
  webServer.sendHeader("Set-Cookie", english ? "lang=en; Path=/; Max-Age=31536000" : "lang=de; Path=/; Max-Age=31536000");
  webServer.sendHeader("Location", "/");
  webServer.send(302, "text/plain", "");
}

String languageSwitchHtml() {
  const char *flagDe = "<svg viewBox='0 0 5 3' aria-hidden='true'><rect width='5' height='1' fill='#000'/><rect y='1' width='5' height='1' fill='#dd0000'/><rect y='2' width='5' height='1' fill='#ffce00'/></svg>";
  const char *flagEn = "<svg viewBox='0 0 60 30' aria-hidden='true'><clipPath id='uk1'><path d='M0 0v30h60V0z'/></clipPath><clipPath id='uk2'><path d='M30 15h30v15zv15H0zH0V0zV0h30z'/></clipPath>"
    "<g clip-path='url(#uk1)'><path d='M0 0v30h60V0z' fill='#012169'/><path d='M0 0l60 30m0-30L0 30' stroke='#fff' stroke-width='6'/>"
    "<path d='M0 0l60 30m0-30L0 30' clip-path='url(#uk2)' stroke='#c8102e' stroke-width='4'/><path d='M30 0v30M0 15h60' stroke='#fff' stroke-width='10'/>"
    "<path d='M30 0v30M0 15h60' stroke='#c8102e' stroke-width='6'/></g></svg>";
  return String("<nav class='lang' aria-label='Sprache / Language'>") +
    "<a href='/lang?l=de' hreflang='de' class='" + (uiEnglish ? "" : "on") + "' title='Deutsch'>" + flagDe + "DE</a>" +
    "<a href='/lang?l=en' hreflang='en' class='" + (uiEnglish ? "on" : "") + "' title='English'>" + flagEn + "EN</a></nav>";
}

// ---------------------------------------------------------------------------
// Webinterface
// ---------------------------------------------------------------------------

String pageHeader(const String &title) {
  return String("<!doctype html><html lang='") + (uiEnglish ? "en" : "de") + "'><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>" + title + "</title><style>body{font-family:Arial,sans-serif;max-width:700px;margin:30px auto;padding:0 18px;background:#f2f6fa;color:#17212b}.card{background:#fff;border-radius:16px;padding:24px;box-shadow:0 4px 18px #0002}h1{margin-top:0;color:#1263a6}h2{font-size:18px;margin:26px 0 4px}.status{padding:12px 14px;margin:12px 0;border-radius:10px;background:#edf5fd}.ok{color:#08783d}.wait{color:#875b00}.bad{color:#a32020}.meter{display:flex;align-items:flex-end;gap:4px;height:38px;margin:10px 0 3px}.bar{width:13px;border-radius:3px 3px 0 0;background:#d3dae1}.bar.on.good{background:#1a9b59}.bar.on.fair{background:#dd9a17}.bar.on.weak{background:#ce3e3e}label{display:block;font-weight:bold;margin-top:16px}input{box-sizing:border-box;width:100%;padding:12px;margin-top:6px;border:1px solid #aac;border-radius:8px;font-size:16px}label.mode{display:flex;gap:14px;align-items:flex-start;font-weight:normal;margin-top:12px;padding:14px;border:2px solid #cbd8e3;border-radius:12px;cursor:pointer;background:#fff}label.mode:has(input:checked){border-color:#1263a6;background:#f3f8fd}label.mode input{width:auto;margin:4px 0 0}.mode svg{flex:none;width:46px;height:46px;color:#1263a6}.mode b{display:block;font-size:17px;margin-bottom:4px}.mode p{margin:6px 0 0;color:#4b5865;font-size:14px;line-height:1.4}.badge{display:inline-block;margin-top:8px;padding:3px 10px;border-radius:99px;font-size:13px;font-weight:bold}.badge.slow{background:#fdf1dc;color:#875b00}.badge.fast{background:#e3f4ea;color:#08783d}.alert{display:flex;gap:14px;align-items:flex-start;background:#c62828;color:#fff;padding:16px 18px;border-radius:12px;margin:0 0 18px;line-height:1.45;box-shadow:0 0 0 4px #f8d4d4;animation:pulse 2s ease-in-out infinite}.alert svg{flex:none;width:34px;height:34px}.alert a{display:inline-block;margin-top:8px;color:#fff;font-weight:bold;text-decoration:underline}@keyframes pulse{50%{box-shadow:0 0 0 8px #f8d4d4}}.group{margin:18px 0 0;font-size:13px;font-weight:bold;color:#4b5865;text-transform:uppercase;letter-spacing:.03em}.badge.danger{background:#c62828;color:#fff}.dangerbox{display:flex;gap:12px;margin-top:10px;padding:14px 16px;border:2px solid #c62828;border-radius:12px;background:#fdecec;color:#7a1414;font-size:14px;line-height:1.45}.dangerbox svg{flex:none;width:30px;height:30px;color:#c62828}form:has(input[name=mode]) .dangerbox{display:none}form:has(input[value=apbridge]:checked) .dangerbox{display:flex}.fwbox{border:1px solid #cbd8e3;border-radius:12px;padding:6px 16px 16px;margin-top:10px}label.chk{display:flex;gap:10px;align-items:flex-start;font-weight:normal;margin-top:12px}label.chk input{width:auto;margin:3px 0 0}label.chk small{display:block;margin-top:2px}.fwrule{display:grid;grid-template-columns:auto 1fr 1fr 2fr 1fr 3em;gap:6px;align-items:center;margin-top:6px}.fwrule input,.fwrule select,select,textarea{box-sizing:border-box;width:100%;margin:0;padding:8px;border:1px solid #aac;border-radius:8px;font-size:14px;background:#fff}.fwrule input[type=checkbox]{width:auto}.hits{font-size:12px;color:#4b5865;text-align:right}textarea{min-height:90px;font-family:monospace}button.small{margin:6px 6px 0 0;padding:6px 10px;font-size:13px}@media(max-width:600px){.fwrule{grid-template-columns:auto 1fr 1fr}.fwrule input[name$=_dst]{grid-column:span 2}}.apfields{display:none;margin-top:12px;padding:6px 16px 14px;border:2px solid #1263a6;border-radius:12px;background:#f3f8fd}form:has(input[value=apnat]:checked) .apfields,form:has(input[value=apbridge]:checked) .apfields{display:block}.openwarn{display:none;gap:12px;margin-top:10px;padding:12px 14px;border:2px solid #c62828;border-radius:12px;background:#fdecec;color:#7a1414;font-size:14px;line-height:1.45}.openwarn svg{flex:none;width:28px;height:28px;color:#c62828}form:has(input[name=m_open]:checked) .openwarn,form:has(input[name=ap_open]:checked) .openwarn{display:flex}form:has(input[name=m_open]:checked) .pwfields,form:has(input[name=ap_open]:checked) .pwfields{display:none}.alert.static{animation:none}.tw{overflow-x:auto}table.nb{width:100%;border-collapse:collapse;font-size:14px;margin-top:8px}table.nb th{text-align:left;font-size:12px;color:#4b5865;padding:4px}table.nb td{padding:6px 4px;border-top:1px solid #d6e2ee;vertical-align:top;white-space:nowrap}table.nb td small{display:block;color:#4b5865;font-size:12px}table.nb .mono{font-family:monospace;font-size:12px}table.nb tr.old{opacity:.55}.spark{display:block;width:100%;height:auto;margin:6px 0 2px;background:#fff;border:1px solid #d6e2ee;border-radius:8px}.spark .gl{stroke:#e3eaf1;stroke-width:1}.spark .vl{stroke:#e3eaf1;stroke-dasharray:4 4}.spark .yl,.spark .xl{font:13px Arial,sans-serif;fill:#6b7885}.spark .yl{text-anchor:end}.spark polyline{fill:none;stroke-width:2;stroke-linejoin:round}.spark .dn{stroke:#1263a6}.spark .up{stroke:#dd9a17}.spark .adn{fill:#1263a6;fill-opacity:.12}.spark .aup{fill:#dd9a17;fill-opacity:.15}.leg{display:flex;flex-wrap:wrap;gap:4px 14px;font-size:12px;color:#4b5865;margin-bottom:4px}.leg .sw{display:inline-block;width:12px;height:3px;border-radius:2px;margin:0 5px 3px 0;vertical-align:middle}.leg .sw.dn{background:#1263a6}.leg .sw.up{background:#dd9a17}span.dn{color:#1263a6}span.up{color:#a56d00}form:has(input[name=web_off]:checked) .pwfields{display:none}.card>.dangerbox{margin:0 0 16px}.dangerbox a{color:#7a1414;font-weight:bold}h1 .sub{display:block;font-size:15px;font-weight:normal;color:#4b5865;margin-top:2px}label.ack{display:flex;gap:10px;align-items:flex-start;margin-top:10px;font-weight:bold;color:#7a1414}label.ack input{width:auto;margin:3px 0 0}.apbox.open{border:2px solid #c62828;background:#fdecec;border-radius:12px;padding:4px 16px 16px}.lang{display:flex;justify-content:flex-end;gap:6px;margin:-8px -8px 8px 0}.lang a{display:flex;align-items:center;gap:6px;padding:5px 9px;border:1px solid #cbd8e3;border-radius:8px;text-decoration:none;color:#4b5865;font-size:13px;font-weight:bold}.lang a.on{border-color:#1263a6;background:#eaf3fc;color:#1263a6}.lang svg{width:24px;height:15px;border-radius:2px;box-shadow:0 0 0 1px #0003}button{margin-top:22px;background:#1263a6;color:#fff;border:0;border-radius:8px;padding:12px 18px;font-size:16px;cursor:pointer}.secondary{margin-top:12px;background:#587080}.network{display:block;width:100%;text-align:left;margin-top:8px;padding:11px;border:1px solid #cbd8e3;border-radius:8px;background:#f8fbfe;color:#17212b}.network b{display:block}.network small,small{color:#4b5865}table.info{width:100%;border-collapse:collapse;margin-top:8px}table.info td{padding:5px 4px;border-top:1px solid #d6e2ee;vertical-align:top}table.info td:first-child{color:#4b5865;width:45%}.cl{padding:9px 0;border-top:1px solid #d6e2ee}.cl:first-child{border-top:0}.cl div{display:flex;justify-content:space-between;gap:10px}.cl div span{color:#4b5865}.cl small{display:block;margin-top:3px}.mini{display:inline-flex;align-items:flex-end;gap:2px;height:12px;vertical-align:-1px}.mini .bar{width:4px;border-radius:1px}</style></head><body><div class='card'>";
}

String pageFooter() { return "</div></body></html>"; }

String signalMeterHtml() {
  return String("<div class='status'><b>") + T("WLAN-Empfang", "WiFi signal") + "</b><div class='meter' id='meter'><i class='bar' style='height:20%'></i><i class='bar' style='height:40%'></i><i class='bar' style='height:65%'></i><i class='bar' style='height:100%'></i></div><span id='signalText'>" + T("Wird geladen...", "Loading...") + "</span></div>";
}

String lanStatusJson() {
  const uint32_t now = millis();
  String json = "{\"speed\":" + String(ethernetLinkUp ? ethernetSpeedMbit : 0);
  json += ",\"fullDuplex\":" + String(ethernetFullDuplex ? "true" : "false");
  json += ",\"linkSeconds\":" + String(ethernetLinkUp ? (now - ethernetLinkSinceMs) / 1000 : 0);
  json += ",\"rxRate\":" + String(lanRxRate) + ",\"txRate\":" + String(lanTxRate) + ",\"rxPeak\":" + String(lanRxPeak) + ",\"txPeak\":" + String(lanTxPeak);
  json += ",\"rxTotal\":" + String(static_cast<unsigned long>(lanRxTotal / 1024)) + ",\"txTotal\":" + String(static_cast<unsigned long>(lanTxTotal / 1024));  // KiB

  uint8_t bridgeMac[6] = {};
  if (ethernetHandle != nullptr && esp_eth_ioctl(ethernetHandle, ETH_CMD_G_MAC_ADDR, bridgeMac) == ESP_OK) {
    json += ",\"bridgeMac\":\"" + macToString(bridgeMac) + "\"";
  }
  if (bridgeMode == MODE_BRIDGE) {
    json += ",\"staMac\":\"" + macToString(staMac) + "\"";
    json += ",\"toWifi\":" + String(framesToWifi) + ",\"toLan\":" + String(framesToLan) + ",\"dropped\":" + String(framesDropped);
  }
  uint32_t leaseMinutes = 0;
  // Nur im NAT-Modus hat die Ethernet-Schnittstelle einen DHCP-Server (im AP-NAT-Modus ist sie DHCP-Client)
  if (bridgeMode == MODE_NAT && ethernetNetif != nullptr && esp_netif_dhcps_option(ethernetNetif, ESP_NETIF_OP_GET, ESP_NETIF_IP_ADDRESS_LEASE_TIME, &leaseMinutes, sizeof(leaseMinutes)) == ESP_OK) {
    json += ",\"leaseMinutes\":" + String(leaseMinutes);
  }

  LanClient copy[MAX_LAN_CLIENTS];
  portENTER_CRITICAL(&lanClientsLock);
  memcpy(copy, lanClients, sizeof(copy));
  portEXIT_CRITICAL(&lanClientsLock);

  const uint32_t serverLease = bridgeMode == MODE_NAT ? leaseMinutes * 60 : 0;
  json += ",\"clients\":[";
  bool first = true;
  for (const LanClient &client : copy) {
    if (!client.used) continue;
    if (!first) json += ',';
    first = false;
    json += "{\"ip\":\"" + (client.ip ? IPAddress(client.ip).toString() : String("")) + "\",\"mac\":\"" + macToString(client.mac) + "\",\"seconds\":" + String((now - client.assignedMs) / 1000) + clientInfoJson(client.mac, serverLease, now) + "}";
  }
  json += "]}";
  return json;
}

String modeKey() {
  switch (bridgeMode) {
    case MODE_BRIDGE: return "bridge";
    case MODE_AP_NAT: return "apnat";
    case MODE_AP_BRIDGE: return "apbridge";
    default: return "nat";
  }
}

// AP-Bridge: Geraete im Heimnetz (zuletzt gesehen innerhalb von 24 h)
String neighborsJson() {
  Neighbor copy[MAX_NEIGHBORS];
  portENTER_CRITICAL(&neighborLock);
  memcpy(copy, neighbors, sizeof(copy));
  portEXIT_CRITICAL(&neighborLock);
  const uint32_t now = millis();
  String json = "[";
  bool first = true;
  for (const Neighbor &entry : copy) {
    if (!entry.used) continue;
    const uint32_t ago = (now - entry.lastMs) / 1000;
    if (ago > 86400) continue;
    if (!first) json += ',';
    first = false;
    json += "{\"mac\":\"" + macToString(entry.mac) + "\",\"ip\":\"" + (entry.ip ? IPAddress(entry.ip).toString() : String("")) + "\",\"seen\":" + String(ago);
    if (entry.name[0] != 0) json += ",\"name\":\"" + jsonEscape(String(entry.name)) + "\"";
    const char *vendor = macVendor(entry.mac);
    if (vendor != nullptr) json += ",\"vendor\":\"" + String(vendor) + "\"";
    if (macIsPrivate(entry.mac)) json += ",\"private\":true";
    json += "}";
  }
  return json + "]";
}

// AP-Modi: verbundene WLAN-Geraete mit Signal, IP, Name, Hersteller, Verbindungsdauer und Restlaufzeit der Vergabe
String wifiClientsJson() {
  wifi_sta_list_t list{};
  if (esp_wifi_ap_get_sta_list(&list) != ESP_OK) return "[]";
  esp_netif_pair_mac_ip_t pairs[ESP_WIFI_MAX_CONN_NUM] = {};
  const int count = list.num < ESP_WIFI_MAX_CONN_NUM ? list.num : ESP_WIFI_MAX_CONN_NUM;
  for (int i = 0; i < count; ++i) memcpy(pairs[i].mac, list.sta[i].mac, 6);
  const bool haveIps = bridgeMode == MODE_AP_NAT && count > 0 && esp_netif_dhcps_get_clients_by_mac(WiFi.AP.netif(), count, pairs) == ESP_OK;
  const uint32_t serverLease = bridgeMode == MODE_AP_NAT ? dhcpServerLeaseSeconds(WiFi.AP.netif()) : 0;
  const uint32_t now = millis();
  String json = "[";
  for (int i = 0; i < count; ++i) {
    const wifi_sta_info_t &sta = list.sta[i];
    ClientInfo info{};
    const String extra = clientInfoJson(sta.mac, serverLease, now, &info);
    uint32_t ip = haveIps ? pairs[i].ip.addr : 0;
    if (ip == 0) ip = info.ip;  // AP-Bridge: vom Router vergebene Adresse
    const char *phy = sta.phy_11n ? "802.11n" : sta.phy_11g ? "802.11g" : sta.phy_11b ? "802.11b" : "";
    if (i) json += ',';
    json += "{\"mac\":\"" + macToString(sta.mac) + "\",\"rssi\":" + String(sta.rssi) + ",\"ip\":\"" + (ip ? IPAddress(ip).toString() : String("")) + "\",\"phy\":\"" + phy + "\"" + extra + "}";
  }
  return json + "]";
}

void showStatus() {
  detectLanguage();  // fuer den Text zum Verbindungsfehler
  const bool hasRouterIp = bridgeMode == MODE_NAT && wifiConnected;
  String json = "{\"version\":\"" + String(FIRMWARE_VERSION) + "\",\"mode\":\"" + modeKey() + "\",\"wifi\":" + String(wifiConnected ? "true" : "false") + ",\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\",\"ip\":\"" + (hasRouterIp ? WiFi.localIP().toString() : String("")) + "\",\"rssi\":" + String(wifiConnected ? WiFi.RSSI() : 0) + ",\"percent\":" + String(wifiPercent()) + ",\"quality\":\"" + signalClass(wifiPercent()) + "\",\"ethLink\":" + String(ethernetLinkUp ? "true" : "false") + ",\"dhcp\":" + String(ethernetLanReady ? "true" : "false") + ",\"apOpen\":" + String(setupApPassword.isEmpty() ? "true" : "false");
  if (!isApMode() && !wifiConnected && !routerSsid.isEmpty()) {
    const int32_t wait = nextConnectAttemptMs ? static_cast<int32_t>(nextConnectAttemptMs - millis()) : 0;
    json += ",\"staReason\":\"" + jsonEscape(staReasonText()) + "\",\"nextTry\":" + String(wait > 0 ? wait / 1000 : 0);
  }
  if (isApMode()) {
    esp_netif_ip_info_t ip{};
    const bool haveIp = ethernetNetif != nullptr && esp_netif_get_ip_info(ethernetNetif, &ip) == ESP_OK && ip.ip.addr != 0;
    json += ",\"uplink\":{\"ip\":\"" + (haveIp ? IPAddress(ip.ip.addr).toString() : String("")) + "\",\"gw\":\"" + (haveIp ? IPAddress(ip.gw.addr).toString() : String("")) + "\"}";
    json += ",\"wifiClients\":" + wifiClientsJson();
    if (bridgeMode == MODE_AP_BRIDGE) json += ",\"neighbors\":" + neighborsJson();
  }
  json += ",\"fw\":" + firewallStatusJson();
  json += ",\"lan\":" + lanStatusJson() + "}";
  webServer.send(200, "application/json", json);
}

// Setzt unterbrochene Verbindungsversuche zum Router nach einem WLAN-Scan fort.
void resumeRouterConnection() {
  if (!scanPausedConnect) return;
  scanPausedConnect = false;
  resetReconnect();
  connectToRouter();
}

// Asynchroner WLAN-Scan: /networks?start=1 startet, danach fragt die Seite /networks ab,
// bis das Ergebnis da ist. So blockiert der Scan den Webserver nicht.
void showNetworks() {
  detectLanguage();  // fuer die Namen der Verschluesselung
  int state = WiFi.scanComplete();
  if (webServer.hasArg("start") && state != WIFI_SCAN_RUNNING) {
    WiFi.scanDelete();
    if (!wifiConnected && !routerSsid.isEmpty()) {
      // Solange der ESP32 versucht, sich mit dem Router zu verbinden, lehnt der WLAN-Treiber
      // einen Scan ab ("STA is connecting"). Daher die Versuche fuer die Dauer des Scans anhalten.
      WiFi.setAutoReconnect(false);
      WiFi.disconnect(false, false);
      scanPausedConnect = true;
      delay(200);
    }
    scanStartedMs = millis();
    state = WiFi.scanNetworks(true, true);
    if (state == WIFI_SCAN_FAILED) {
      delay(500);
      state = WiFi.scanNetworks(true, true);
    }
    if (state == WIFI_SCAN_FAILED) {
      Serial.println("WLAN-Scan konnte nicht gestartet werden");
      resumeRouterConnection();
      webServer.send(200, "application/json", "{\"state\":\"failed\"}");
      return;
    }
    webServer.send(200, "application/json", "{\"state\":\"running\"}");
    return;
  }

  if (state == WIFI_SCAN_RUNNING) {
    if (millis() - scanStartedMs < 15000) {
      webServer.send(200, "application/json", "{\"state\":\"running\"}");
      return;
    }
    WiFi.scanDelete();
    state = WIFI_SCAN_FAILED;
  }
  if (state < 0) {
    resumeRouterConnection();
    webServer.send(200, "application/json", "{\"state\":\"failed\"}");
    return;
  }

  String json = "{\"state\":\"done\",\"networks\":[";
  bool first = true;
  for (int i = 0; i < state; ++i) {
    const String ssid = WiFi.SSID(i);
    bool duplicate = false;  // gleiche SSID mehrfach (Mesh/Repeater): nur den staerksten Eintrag zeigen
    for (int j = 0; j < i && !ssid.isEmpty(); ++j) {
      if (WiFi.SSID(j) == ssid && WiFi.RSSI(j) >= WiFi.RSSI(i)) { duplicate = true; break; }
    }
    if (duplicate) continue;
    if (!first) json += ',';
    first = false;
    const int rssi = WiFi.RSSI(i);
    const int percent = constrain((rssi + 90) * 100 / 60, 0, 100);
    const wifi_auth_mode_t encryption = WiFi.encryptionType(i);
    json += "{\"ssid\":\"" + jsonEscape(ssid) + "\",\"rssi\":" + String(rssi) + ",\"percent\":" + String(percent) + ",\"encryption\":\"" + jsonEscape(encryptionName(encryption)) + "\",\"secured\":" + String(encryption == WIFI_AUTH_OPEN ? "false" : "true") + "}";
  }
  json += "]}";
  WiFi.scanDelete();
  resumeRouterConnection();
  webServer.send(200, "application/json", json);
}

// Texte fuer das JavaScript der Startseite
const char *const JS_TEXT_DE = "var L={notConnected:'Nicht mit dem Router verbunden',noCable:'Kein Kabel erkannt. Stecke das Kabel am Endger\\u00e4t und an der Bridge fest ein.',"
  "link:'Verbindung',full:'Vollduplex',half:'Halbduplex',since:'Kabel steckt seit',device:'Ger\\u00e4t',notDetected:'noch nicht erkannt (sendet noch nichts)',"
  "noLease:'noch keine per DHCP vergeben',ip:'IP-Adresse',unknown:'noch unbekannt',mac:'MAC-Adresse',seen:'Erkannt vor',assigned:'Adresse vergeben vor',ago:'',"
  "addr:'Adressvergabe',byRouter:'direkt durch den Router',appears:'Ger\\u00e4t erscheint im Router als MAC',toWifi:'Frames LAN \\u2192 WLAN',toLan:'Frames WLAN \\u2192 LAN',"
  "dropped:'Verworfen',gw:'Gateway / DNS',lease:'Lease-Dauer',bridgeMac:'MAC der Bridge (LAN)',searching:'Suche nach WLANs ...',"
  "scanFail:'Die WLAN-Suche ist fehlgeschlagen. Bitte erneut versuchen.',none:'Keine WLANs gefunden.',hidden:'(verstecktes WLAN)',secured:' (gesichert)',"
  "selected:'Ausgew\\u00e4hlt: ',mismatch:'Die beiden Eingaben stimmen nicht \\u00fcberein.',confirmPw:'Passwort \\u00e4ndern? Die Bridge startet danach neu.',noUplink:'Kein Kabel erkannt. Verbinde den LAN-Port mit deinem Router.',homeIp:'IP der Bridge im Heimnetz',gateway:'Gateway',waitingIp:'wartet auf Adresse vom Router',noClients:'Noch keine WLAN-Ger\\u00e4te verbunden.',ackNeeded:'Bitte best\\u00e4tige den Hinweis zum Webinterface.',add:'zur Liste',pwNeeded:'Bitte ein WLAN-Passwort f\\u00fcr den Access Point festlegen (mindestens 8 Zeichen).',nextTry:'n\\u00e4chster Versuch in',"
  "name:'Ger\\u00e4tename',vendor:'Hersteller',noVendor:'Hersteller unbekannt',privMac:'Private MAC',privHint:'Zuf\\u00e4llige Adresse zum Schutz der Privatsph\\u00e4re \\u2013 der Hersteller ist daran nicht erkennbar.',"
  "connFor:'verbunden seit',leaseShort:'Adresse g\\u00fcltig noch',leaseLeft:'Adresse g\\u00fcltig noch',expired:'abgelaufen',webPwShort:'Das Passwort muss mindestens 8 Zeichen lang sein.',rate:'Datenrate',peak:'H\\u00f6chstwert',total:'Daten seit Start',last2:'letzte 2 Minuten',now:'jetzt',noNeighbors:'Noch keine Ger\\u00e4te erkannt.',router:'Router',seenCol:'Zuletzt',justNow:'gerade eben',agoA:'vor ',agoB:'',dec:','};";
const char *const JS_TEXT_EN = "var L={notConnected:'Not connected to the router',noCable:'No cable detected. Plug the cable firmly into the device and the bridge.',"
  "link:'Link',full:'full duplex',half:'half duplex',since:'Cable connected for',device:'Device',notDetected:'not detected yet (not sending anything)',"
  "noLease:'none assigned via DHCP yet',ip:'IP address',unknown:'not known yet',mac:'MAC address',seen:'Detected',assigned:'Address assigned',ago:' ago',"
  "addr:'Address assignment',byRouter:'directly by the router',appears:'Device appears in the router with MAC',toWifi:'Frames LAN \\u2192 WiFi',toLan:'Frames WiFi \\u2192 LAN',"
  "dropped:'Dropped',gw:'Gateway / DNS',lease:'Lease time',bridgeMac:'Bridge MAC (LAN)',searching:'Searching for WiFi networks ...',"
  "scanFail:'The WiFi scan failed. Please try again.',none:'No WiFi networks found.',hidden:'(hidden network)',secured:' (secured)',"
  "selected:'Selected: ',mismatch:'The two entries do not match.',confirmPw:'Change the password? The bridge will restart afterwards.',noUplink:'No cable detected. Connect the LAN port to your router.',homeIp:'Bridge IP in the home network',gateway:'Gateway',waitingIp:'waiting for an address from the router',noClients:'No WiFi devices connected yet.',ackNeeded:'Please confirm the note about the web interface.',add:'add to list',pwNeeded:'Please set a WiFi password for the access point (at least 8 characters).',nextTry:'next attempt in',"
  "name:'Device name',vendor:'Manufacturer',noVendor:'unknown manufacturer',privMac:'Private MAC',privHint:'Randomized address for privacy \\u2013 the manufacturer cannot be derived from it.',"
  "connFor:'connected for',leaseShort:'lease left',leaseLeft:'Lease remaining',expired:'expired',webPwShort:'The password must be at least 8 characters long.',rate:'Data rate',peak:'Peak',total:'Data since start',last2:'last 2 minutes',now:'now',noNeighbors:'No devices detected yet.',router:'Router',seenCol:'Last seen',justNow:'just now',agoA:'',agoB:' ago',dec:'.'};";

// Alle 10 s eine Diagnosezeile im seriellen Log (hilft bei Verbindungsproblemen)
void logDiagnostics() {
  static uint32_t last = 0;
  if (millis() - last < 10000) return;
  last = millis();
  uint8_t channel = 0;
  wifi_second_chan_t second;
  esp_wifi_get_channel(&channel, &second);
  const char *modeNames[] = {"NAT", "Bridge", "AP-NAT", "AP-Bridge"};
  String router = "-";
  if (!isApMode()) {
    if (wifiConnected) router = "verbunden";
    else if (routerSsid.isEmpty()) router = "nicht eingerichtet";
    else {
      const int32_t wait = nextConnectAttemptMs ? static_cast<int32_t>(nextConnectAttemptMs - millis()) / 1000 : 0;
      router = "getrennt (Grund " + String(lastStaDisconnectReason) + "), Versuche " + String(failedConnectAttempts) + ", naechster in " + String(wait > 0 ? wait : 0) + " s";
    }
  }
  Serial.printf("Diag: %lus | %s | Router: %s | WLAN-Geraete: %d | Kanal %u | Heap frei %u, min %u, Block %u\n",
                static_cast<unsigned long>(millis() / 1000), modeNames[bridgeMode], router.c_str(), WiFi.softAPgetStationNum(), channel,
                static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
}

// Captive Portal: Anfragen von Geraeten im Einrichtungs-WLAN an fremde Adressen (z. B. die
// Verbindungstests von Android, iOS, Windows) auf die Einrichtungsseite umleiten. Dadurch oeffnet
// das Handy/Notebook die Seite nach dem Verbinden automatisch.
bool captiveRedirect() {
  if (!captivePortal) return false;
  const uint32_t remote = static_cast<uint32_t>(webServer.client().remoteIP());
  if ((remote & 0x00FFFFFFu) != 0x0004A8C0u) return false;  // nur Geraete aus 192.168.4.x
  const String host = webServer.hostHeader();
  if (host == "192.168.4.1" || host.startsWith("192.168.4.1:")) return false;
  webServer.sendHeader("Location", "http://192.168.4.1/");
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(302, "text/plain", "");
  return true;
}

void showHome() {
  if (captiveRedirect()) return;
  detectLanguage();
  const bool bridge = bridgeMode == MODE_BRIDGE;
  const bool apNat = bridgeMode == MODE_AP_NAT;
  const bool apMode = isApMode();

  String routerState;
  if (apMode) {
    routerState = "";
  } else if (!wifiConnected) {
    routerState = String("<p class='wait'>") + T("Noch nicht mit dem Router verbunden. Speichere die Zugangsdaten unten; die Bridge versucht die Verbindung automatisch.", "Not connected to the router yet. Save the credentials below; the bridge will connect automatically.") + "</p>";
  } else if (bridge) {
    routerState = String("<p class='ok'>") + T("Mit Router verbunden: ", "Connected to router: ") + "<b>" + htmlEscape(WiFi.SSID()) + "</b></p>";
  } else {
    routerState = String("<p class='ok'>") + T("Mit Router verbunden: ", "Connected to router: ") + "<b>" + htmlEscape(WiFi.SSID()) + "</b><br>" + T("Router-IP der Bridge: ", "Bridge IP in the router network: ") + WiFi.localIP().toString() + "</p>";
  }

  String ethernetState;
  if (apNat) {
    ethernetState = String("<p class='ok'>") + T("Access Point mit NAT: Das WLAN ", "Access point with NAT: the WiFi ") + "<b>" + htmlEscape(apSsid) + "</b> " +
      T("ist aktiv. WLAN-Ger&auml;te bekommen Adressen im Netz <b>192.168.4.x</b>; ins Heimnetz geht es &uuml;ber das LAN-Kabel. Dieses Webinterface ist auch &uuml;ber die IP der Bridge im Heimnetz erreichbar.",
        "is active. WiFi devices get addresses in the <b>192.168.4.x</b> network; the connection to the home network runs over the LAN cable. This web interface is also reachable via the bridge IP in the home network.") + "</p>";
  } else if (bridgeMode == MODE_AP_BRIDGE) {
    ethernetState = String("<p class='ok'>") + T("Access Point als Bridge aktiv: Das WLAN ", "Access point bridge active: the WiFi ") + "<b>" + htmlEscape(apSsid) + "</b> " +
      T("geh&ouml;rt direkt zu deinem Heimnetz. Dieses Webinterface erreichst du &uuml;ber die IP der Bridge (siehe &bdquo;Uplink&ldquo;) oder ", "is part of your home network. You reach this web interface via the bridge IP (see \"Uplink\") or ") +
      "<b>http://" + BRIDGE_HOSTNAME + ".fritz.box</b>.</p>";
  } else if (bridge) {
    ethernetState = ethernetLanReady
      ? String("<p class='ok'>") + T("Bridge-Modus aktiv. Das LAN-Ger&auml;t bekommt seine Adresse direkt vom Router.", "Bridge mode active. The LAN device gets its address directly from the router.") + "</p>"
      : String("<p class='bad'>") + T("Die Ethernet-Bridge ist noch nicht bereit.", "The Ethernet bridge is not ready yet.") + "</p>";
  } else {
    ethernetState = ethernetLanReady
      ? String("<p class='ok'>") + T("NAT-Modus: Ethernet-DHCP ist aktiv. Das angeschlossene Ger&auml;t bekommt automatisch eine Adresse im Netz <b>192.168.50.x</b>; Gateway ist <b>192.168.50.1</b>.", "NAT mode: Ethernet DHCP is active. The connected device automatically gets an address in the <b>192.168.50.x</b> network; the gateway is <b>192.168.50.1</b>.") + "</p>"
      : String("<p class='bad'>") + T("Der Ethernet-DHCP-Dienst ist noch nicht bereit.", "The Ethernet DHCP service is not ready yet.") + "</p>";
  }
  String linkState = String("<div class='status'><b>") + (apMode ? T("Uplink zum Router (LAN-Port)", "Uplink to the router (LAN port)") : T("Ger&auml;t am LAN-Port", "Device on the LAN port")) +
    "</b><div id='lanInfo'>" + T("Wird geladen...", "Loading...") + "</div></div>";
  if (apMode) {
    linkState += String("<div class='status'><b>") + T("WLAN-Ger&auml;te", "WiFi devices") + "</b><div id='apClients'>" + T("Wird geladen...", "Loading...") + "</div></div>";
    if (bridgeMode == MODE_AP_BRIDGE) {
      linkState += String("<div class='status'><b>") + T("Ger&auml;te im Heimnetz", "Devices in the home network") + " <span id='nbCount'></span></b><div id='neighbors'>" + T("Wird geladen...", "Loading...") + "</div><p><small>" +
        T("Erkannt an ihren Rundsendungen am LAN-Port: Ger&auml;te erscheinen, sobald sie im Netz etwas senden, Namen bei der n&auml;chsten Adressvergabe. Vollst&auml;ndig ist die Liste nur im Router.",
          "Detected from their broadcasts on the LAN port: devices appear as soon as they send something on the network, names with their next address assignment. Only the router has the complete list.") + "</small></p></div>";
    }
  }

  const String script = String("<script>") + (uiEnglish ? JS_TEXT_EN : JS_TEXT_DE) +
    "function status(){fetch('/status').then(r=>{if(r.status==401){location.reload();throw 0;}return r.json();}).then(s=>{if(document.getElementById('meter')){let bars=document.querySelectorAll('#meter .bar'),n=s.wifi?Math.ceil(s.percent/25):0;bars.forEach((b,i)=>b.className='bar '+(i<n?'on '+s.quality:''));document.getElementById('signalText').textContent=s.wifi?s.rssi+' dBm - '+s.percent+' %':L.notConnected+(s.staReason?': '+s.staReason:'')+(s.nextTry?' \\u2013 '+L.nextTry+' '+s.nextTry+' s':'');}showLan(s);showClients(s);showNeighbors(s);showFw(s);});}function showFw(s){if(!s.fw)return;s.fw.hits.forEach((h,i)=>{let e=document.getElementById('hit'+i);if(e)e.textContent=h;});let b=document.getElementById('fwBlocked');if(b)b.textContent=s.fw.blocked;let p=document.getElementById('fwMacPick');if(p&&s.wifiClients){p.textContent='';s.wifiClients.forEach(w=>{let k=document.createElement('button');k.type='button';k.className='secondary small';k.textContent='+ '+(w.name?w.name+' \\u2013 ':'')+w.mac+(w.ip?' ('+w.ip+')':'');k.onclick=()=>{let t=document.querySelector('[name=fw_macs]');if(t.value.indexOf(w.mac)<0)t.value=(t.value.trim()?t.value.trim()+'\\n':'')+w.mac;};p.appendChild(k);});}}function showClients(s){let c=document.getElementById('apClients');if(!c||!s.wifiClients)return;if(!s.wifiClients.length){c.textContent=L.noClients;return;}let h='';s.wifiClients.forEach(w=>{let t=w.name||w.ip||w.mac,d=[t!=w.mac?w.mac:'',maker(w)].filter(x=>x).join(' \\u00b7 '),x=bars(w.rssi)+' '+w.rssi+' dBm'+(w.phy?' \\u00b7 '+w.phy:'')+(w.since!=null?' \\u00b7 '+L.connFor+' '+short(w.since):'')+(w.leaseLeft!=null?' \\u00b7 '+L.leaseShort+' '+(w.leaseLeft?short(w.leaseLeft):L.expired):'');h+='<div class=\\'cl\\'><div><b>'+esc(t)+'</b>'+(w.name&&w.ip?'<span>'+w.ip+'</span>':'')+'</div><small>'+d+'</small><small>'+x+'</small></div>';});c.innerHTML=h;}function esc(t){return String(t).replace(/[&<>\"']/g,c=>'&#'+c.charCodeAt(0)+';');}function short(t){let h=Math.floor(t/3600),m=Math.floor(t%3600/60);return h?h+' h '+m+' min':m?m+' min':t+' s';}function maker(w){return w.vendor?esc(w.vendor):w.private?'<span title=\\''+L.privHint+'\\'>'+L.privMac+'</span>':L.noVendor;}function bars(r){let p=Math.max(0,Math.min(100,(r+90)*100/60)),n=Math.ceil(p/25),q=p<35?'weak':p<65?'fair':'good',h='<span class=\\'mini\\'>';for(let i=0;i<4;i++)h+='<i class=\\'bar'+(i<n?' on '+q:'')+'\\' style=\\'height:'+(i+1)*25+'%\\'></i>';return h+'</span>';}"
    "function dur(t){let h=Math.floor(t/3600),m=Math.floor(t%3600/60),x=t%60;return (h?h+' h ':'')+(h||m?m+' min ':'')+x+' s';}"
    "var RH=[];function num(x,d){return x.toFixed(d).replace('.',L.dec);}"
    "function rate(b){return b>=1e6?num(b/1e6,1)+' Mbit/s':b>=1e3?num(b/1e3,0)+' kbit/s':b+' bit/s';}"
    "function size(k){return k<1024?k+' KB':k<1048576?num(k/1024,1)+' MB':num(k/1048576,2)+' GB';}"
    "function axis(v,s){return v==0?'0':v>=1e6?num(v/1e6,s<1e6?1:0)+' Mbit/s':num(v/1e3,0)+' kbit/s';}"
    "function spark(){let n=RH.length,m=1e5;RH.forEach(r=>{m=Math.max(m,r[0],r[1]);});"
    "let s=Math.pow(10,Math.floor(Math.log10(m/4))),f=m/4/s;s*=f<=1?1:f<=2?2:f<=5?5:10;let top=s*Math.ceil(m/s);"
    "let X0=80,X1=592,Y0=12,Y1=150,X=i=>(X0+(60-n+i)*(X1-X0)/59).toFixed(1),Y=v=>(Y1-v/top*(Y1-Y0)).toFixed(1),g='';"
    "for(let v=0;v<=top;v+=s){g+='<line class=\\'gl\\' x1=\\''+X0+'\\' x2=\\''+X1+'\\' y1=\\''+Y(v)+'\\' y2=\\''+Y(v)+'\\'/><text class=\\'yl\\' x=\\''+(X0-6)+'\\' y=\\''+(+Y(v)+4)+'\\'>'+axis(v,s)+'</text>';}"
    "let xm=(X0+X1)/2;g+='<line class=\\'vl\\' x1=\\''+xm+'\\' x2=\\''+xm+'\\' y1=\\''+Y0+'\\' y2=\\''+Y1+'\\'/>';"
    "g+='<text class=\\'xl\\' x=\\''+X0+'\\' y=\\'168\\'>\\u2212 2 min</text><text class=\\'xl\\' x=\\''+xm+'\\' y=\\'168\\' text-anchor=\\'middle\\'>\\u2212 1 min</text><text class=\\'xl\\' x=\\''+X1+'\\' y=\\'168\\' text-anchor=\\'end\\'>'+L.now+'</text>';"
    "let pl=j=>RH.map((r,i)=>X(i)+','+Y(r[j])).join(' '),ar=j=>n?X(0)+','+Y1+' '+pl(j)+' '+X(n-1)+','+Y1:'';"
    "return '<svg class=\\'spark\\' viewBox=\\'0 0 600 176\\' role=\\'img\\' aria-label=\\''+L.rate+'\\'>'+g+'<polygon class=\\'adn\\' points=\\''+ar(0)+'\\'/><polygon class=\\'aup\\' points=\\''+ar(1)+'\\'/><polyline class=\\'dn\\' points=\\''+pl(0)+'\\'/><polyline class=\\'up\\' points=\\''+pl(1)+'\\'/></svg>'"
    "+'<div class=\\'leg\\'><span><i class=\\'sw dn\\'></i>Download</span><span><i class=\\'sw up\\'></i>Upload</span><span>'+L.last2+'</span></div>';}"
    "function ipNum(a){return a?a.split('.').reduce((x,y)=>x*256+ +y,0):1e10;}"
    "function ago(t){return t<30?L.justNow:L.agoA+short(t)+L.agoB;}"
    "function showNeighbors(s){let c=document.getElementById('neighbors');if(!c||!s.neighbors)return;let l=s.neighbors.slice().sort((a,b)=>ipNum(a.ip)-ipNum(b.ip));"
    "document.getElementById('nbCount').textContent=l.length?'('+l.length+')':'';if(!l.length){c.textContent=L.noNeighbors;return;}"
    "let h='<div class=\\'tw\\'><table class=\\'nb\\'><tr><th>'+L.ip+'</th><th>'+L.device+'</th><th>'+L.mac+'</th><th>'+L.seenCol+'</th></tr>';"
    "l.forEach(n=>{let r=n.ip&&s.uplink&&n.ip==s.uplink.gw,nm=n.name?esc(n.name):r?L.router:'\\u2013';"
    "h+='<tr'+(n.seen>600?' class=\\'old\\'':'')+'><td>'+(n.ip||'\\u2013')+'</td><td><b>'+nm+'</b><small>'+maker(n)+'</small></td><td class=\\'mono\\'>'+n.mac+'</td><td>'+ago(n.seen)+'</td></tr>';});"
    "c.innerHTML=h+'</table></div>';}"
    "function row(k,v){return '<tr><td>'+k+'</td><td><b>'+v+'</b></td></tr>';}"
    "function showLan(s){let l=s.lan,br=s.mode=='bridge',ap=!!s.uplink,box=document.getElementById('lanInfo'),h='';"
    "if(!s.ethLink){box.innerHTML='<p class=\\'wait\\'>'+(ap?L.noUplink:L.noCable)+'</p>';return;}"
    "h+=row(L.link,l.speed+' Mbit/s, '+(l.fullDuplex?L.full:L.half));h+=row(L.since,dur(l.linkSeconds));let dn=ap?l.rxRate:l.txRate,up=ap?l.txRate:l.rxRate;RH.push([dn,up]);if(RH.length>60)RH.shift();h+=row(L.rate,'<span class=dn>\\u2193 '+rate(dn)+'</span> \\u00b7 <span class=up>\\u2191 '+rate(up)+'</span>');h+='<tr><td colspan=2>'+spark()+'</td></tr>';h+=row(L.peak,'\\u2193 '+rate(ap?l.rxPeak:l.txPeak)+' \\u00b7 \\u2191 '+rate(ap?l.txPeak:l.rxPeak));h+=row(L.total,'\\u2193 '+size(ap?l.rxTotal:l.txTotal)+' \\u00b7 \\u2191 '+size(ap?l.txTotal:l.rxTotal));if(ap){h+=row(L.homeIp,s.uplink.ip||L.waitingIp);if(s.uplink.gw)h+=row(L.gateway,s.uplink.gw);box.innerHTML='<table class=\\'info\\'>'+h+'</table>';return;}"
    "if(!l.clients.length){h+=row(br?L.device:L.ip,br?L.notDetected:L.noLease);}"
    "l.clients.forEach((c,i)=>{let p=l.clients.length>1?' ('+(i+1)+')':'';if(c.name)h+=row(L.name+p,esc(c.name));h+=row(L.ip+p,c.ip||L.unknown);h+=row(L.mac+p,c.mac);h+=row(L.vendor+p,maker(c));h+=row((br?L.seen:L.assigned)+p,dur(c.seconds)+L.ago);if(c.leaseLeft!=null)h+=row(L.leaseLeft+p,c.leaseLeft?short(c.leaseLeft):L.expired);});"
    "if(br){h+=row(L.addr,L.byRouter);if(l.staMac)h+=row(L.appears,l.staMac);h+=row(L.toWifi,l.toWifi);h+=row(L.toLan,l.toLan);h+=row(L.dropped,l.dropped);}"
    "else{h+=row(L.gw,'192.168.50.1 / 1.1.1.1');if(l.leaseMinutes)h+=row(L.lease,l.leaseMinutes+' min');if(l.bridgeMac)h+=row(L.bridgeMac,l.bridgeMac);}"
    "box.innerHTML='<table class=\\'info\\'>'+h+'</table>';}"
    "function scan(){document.getElementById('networks').textContent=L.searching;poll(true,0);}"
    "function poll(start,errors){fetch('/networks'+(start?'?start=1':'')).then(r=>{if(r.status==401){location.reload();throw 0;}return r.json();}).then(s=>{"
    "if(s.state=='running'){setTimeout(()=>poll(false,0),800);return;}"
    "if(s.state!='done'){document.getElementById('networks').textContent=L.scanFail;return;}"
    "showNetworks(s.networks);}).catch(()=>{if(errors<8)setTimeout(()=>poll(false,errors+1),1500);else document.getElementById('networks').textContent=L.scanFail;});}"
    "function showNetworks(list){let box=document.getElementById('networks');box.textContent='';if(!list.length){box.textContent=L.none;return;}"
    "list.forEach(n=>{let b=document.createElement('button');b.type='button';b.className='network';let name=document.createElement('b');name.textContent=n.ssid||L.hidden;"
    "let detail=document.createElement('small');detail.textContent=n.rssi+' dBm - '+n.percent+' % - '+n.encryption+(n.secured?L.secured:'');b.append(name,detail);"
    "b.onclick=()=>{document.querySelector('[name=ssid]').value=n.ssid;box.textContent=L.selected+n.ssid;};box.appendChild(b);});}"
    "status();setInterval(status,2000);</script>";

  // Piktogramme
  const char *svgStart = "<svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round' aria-hidden='true'>";
  const String natIcon = String(svgStart) + "<rect x='9' y='2' width='6' height='5' rx='1'/><path d='M12 7v4M5 11h14M5 11v4M12 11v4M19 11v4'/>"
    "<rect x='2.5' y='15' width='5' height='5' rx='1'/><rect x='9.5' y='15' width='5' height='5' rx='1'/><rect x='16.5' y='15' width='5' height='5' rx='1'/></svg>";
  const String bridgeIcon = String(svgStart) + "<path d='M3 15Q12 3 21 15'/><path d='M2 15h20M7.5 10.5V15M12 9v6M16.5 10.5V15M4 15v5M20 15v5'/></svg>";
  const String apNatIcon = String(svgStart) + "<path d='M3 9.5a13 13 0 0 1 18 0M6 12.8a8.5 8.5 0 0 1 12 0M9 16a4 4 0 0 1 6 0'/><circle cx='12' cy='19.5' r='1.3' fill='currentColor'/></svg>";
  const String apBridgeIcon = String(svgStart) + "<path d='M7 6a7 7 0 0 1 10 0M9.3 8.6a3.6 3.6 0 0 1 5.4 0'/><circle cx='12' cy='11' r='1.1' fill='currentColor'/>"
    "<path d='M3 21Q12 12 21 21'/><path d='M2 21h20M7.5 17.4V21M12 16.5V21M16.5 17.4V21'/></svg>";

  auto modeCard = [&](const char *value, BridgeMode mode, const String &icon, const char *title, const char *text, const String &badges) {
    return String("<label class='mode'><input type='radio' name='mode' value='") + value + "'" + (bridgeMode == mode ? " checked" : "") + ">" + icon +
      "<span><b>" + title + "</b><p>" + text + "</p>" + badges + "</span></label>";
  };

  const String apBridgeWarning = String("<div class='dangerbox'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' aria-hidden='true'><path d='M12 3L2 21h20L12 3z'/><path d='M12 10v5M12 18v.5'/></svg><div><b>") +
    T("Achtung: Webinterface nicht mehr unter 192.168.4.1!", "Warning: web interface no longer at 192.168.4.1!") + "</b><br>" +
    T("In dieser Betriebsart holt sich die Bridge ihre Adresse vom Router. Das Webinterface erreichst du dann &uuml;ber diese Adresse &ndash; aus dem Heimnetz und aus dem WLAN der Bridge: <b>http://" ,
      "In this mode the bridge gets its address from your router. You then reach the web interface via this address &ndash; from your home network and from the bridge's WiFi: <b>http://") +
      BRIDGE_HOSTNAME + T(".fritz.box</b> (bei einer FRITZ!Box) oder die IP, die dein Router f&uuml;r &bdquo;" , ".fritz.box</b> (with a FRITZ!Box) or the IP your router shows for \"") + BRIDGE_HOSTNAME +
      T("&ldquo; anzeigt.", "\".") + "<br>" +
    T("<b>Zur&uuml;ck zur Einrichtung:</b> die Stromversorgung <b>3-mal hintereinander</b> kurz aus- und wieder einschalten (jeweils innerhalb von 10 Sekunden). Danach startet die Bridge im NAT-Modus mit dem Einrichtungs-WLAN.",
      "<b>Back to setup:</b> switch the power off and on again <b>3 times in a row</b> (each within 10 seconds). The bridge then starts in NAT mode with the setup WiFi.") +
    "<label class='ack'><input type='checkbox' name='ack' value='1'>" + T("Verstanden: Das Webinterface finde ich danach &uuml;ber die IP vom Router.", "Understood: afterwards I will find the web interface via the IP from the router.") + "</label></div></div>";

  // WLAN-Name und Passwort direkt bei den Access-Point-Karten (nur sichtbar, wenn eine davon gewaehlt ist)
  const bool apHasPassword = !setupApPassword.isEmpty();
  // In den Access-Point-Modi sind das die einzigen WLAN-Einstellungen (Sprungziel #ap fuer die Warnhinweise)
  const String apFields = String("<div class='apfields'") + (apMode ? " id='ap'" : "") + "><b>" + T("WLAN des Access Points", "Access point WiFi") + "</b>"
    "<label>" + T("WLAN-Name", "WiFi name") + "<input name='m_ssid' maxlength='32' value='" + htmlEscape(apSsid) + "'></label>"
    "<label class='chk'><input type='checkbox' name='m_open' value='1'" + (apOpenChosen ? " checked" : "") + "><span><b>" + T("Offenes WLAN ohne Passwort", "Open WiFi without password") + "</b><small>" +
      T("Nur f&uuml;r besondere F&auml;lle, z. B. ein G&auml;ste-WLAN mit aktiver Firewall.", "Only for special cases, e.g. a guest WiFi with the firewall enabled.") + "</small></span></label>" + openWifiWarningHtml() +
    "<div class='pwfields'><label>" + T("Passwort", "Password") + "<input name='m_pass' type='password' minlength='8' maxlength='63' autocomplete='new-password' placeholder='" +
      (apHasPassword ? T("Leer lassen, um das bisherige zu behalten", "Leave empty to keep the current one") : T("mindestens 8 Zeichen", "at least 8 characters")) + "'></label>"
    "<label>" + T("Passwort wiederholen", "Repeat password") + "<input name='m_repeat' type='password' minlength='8' maxlength='63' autocomplete='new-password'></label></div>"
    "<p><small>" + (apMode ? T("Mit diesem Namen und Passwort verbinden sich deine Ger&auml;te. &Auml;nderungen werden mit &bdquo;&Uuml;bernehmen und neu starten&ldquo; gespeichert.",
                                 "Your devices connect with this name and password. Changes are saved with \"Apply and restart\".")
                    : T("Mit diesem Namen und Passwort verbinden sich deine Ger&auml;te. Es ist dasselbe WLAN wie unten unter &bdquo;Einrichtungs-WLAN&ldquo;.",
                        "Your devices connect with this name and password. It is the same WiFi as below under \"Setup WiFi\".")) + "</small></p></div>";

  const String modeForm = String("<h2>") + T("Betriebsart", "Operating mode") + "</h2>"
    "<form method='post' action='/mode' onsubmit=\"var m=this.querySelector('input[name=mode]:checked');if(!m)return true;"
    "if(m.value.indexOf('ap')==0&&!this.m_open.checked){if(this.m_pass.value!=this.m_repeat.value){alert(L.mismatch);return false;}" + (apHasPassword ? "" : "if(!this.m_pass.value){alert(L.pwNeeded);return false;}") + "}"
    "if(m.value=='apbridge'&&!this.ack.checked){alert(L.ackNeeded);return false;}return true;\">"
    "<p class='group'>" + T("WLAN-Client: Ger&auml;t am LAN-Port, Bridge im WLAN deines Routers", "WiFi client: device on the LAN port, bridge joins your router's WiFi") + "</p>" +
    modeCard("nat", MODE_NAT, natIcon, T("NAT &ndash; eigenes Netzwerk", "NAT &ndash; own network"),
      T("Die Bridge baut am LAN-Port ein eigenes Netz (192.168.50.x) auf und vergibt die Adressen selbst. Ideal, wenn mehrere Ger&auml;te &uuml;ber einen Switch angeschlossen werden sollen.",
        "The bridge creates its own network (192.168.50.x) on the LAN port and assigns the addresses itself. Ideal if you want to connect several devices through a switch."),
      String("<span class='badge slow'>") + T("Datenrate bis ca. 10 Mbit/s", "Data rate up to approx. 10 Mbit/s") + "</span>") +
    modeCard("bridge", MODE_BRIDGE, bridgeIcon, T("Bridge &ndash; direkt ins Heimnetz", "Bridge &ndash; straight into your home network"),
      T("Das angeschlossene Ger&auml;t erh&auml;lt seine IP-Adresse direkt vom Router und ist im Heimnetz wie jedes andere Ger&auml;t erreichbar. F&uuml;r genau ein Ger&auml;t, nur IPv4.",
        "The connected device gets its IP address directly from your router and is reachable in your home network like any other device. For exactly one device, IPv4 only."),
      String("<span class='badge fast'>") + T("Datenrate &uuml;ber 30 Mbit/s", "Data rate above 30 Mbit/s") + "</span>") +
    "<p class='group'>" + T("Access Point: LAN-Port am Router, die Bridge spannt ein eigenes WLAN auf", "Access point: LAN port to your router, the bridge creates its own WiFi") + "</p>" +
    modeCard("apnat", MODE_AP_NAT, apNatIcon, T("Access Point &ndash; eigenes WLAN-Netz (NAT)", "Access point &ndash; own WiFi network (NAT)"),
      T("Der LAN-Port wird mit dem Router verbunden. Die Bridge spannt ein eigenes WLAN auf (Name und Passwort direkt hier festlegen) und gibt den WLAN-Ger&auml;ten Adressen im Netz 192.168.4.x. Das Webinterface bleibt erreichbar.",
        "Connect the LAN port to your router. The bridge creates its own WiFi (set name and password right here) and gives WiFi devices addresses in the 192.168.4.x network. The web interface stays reachable."),
      String("<span class='badge fast'>") + T("Webinterface bleibt erreichbar", "Web interface stays reachable") + "</span> <span class='badge slow'>" + T("bis ca. 8 WLAN-Ger&auml;te, nur 2,4 GHz", "up to approx. 8 WiFi devices, 2.4 GHz only") + "</span>") +
    modeCard("apbridge", MODE_AP_BRIDGE, apBridgeIcon, T("Access Point &ndash; WLAN direkt im Heimnetz (Bridge)", "Access point &ndash; WiFi straight into your home network (bridge)"),
      T("Der LAN-Port wird mit dem Router verbunden. WLAN-Ger&auml;te bekommen ihre Adressen direkt vom Router und sind im Heimnetz sichtbar (AirPlay, Chromecast, Drucker).",
        "Connect the LAN port to your router. WiFi devices get their addresses directly from your router and are visible in your home network (AirPlay, Chromecast, printers)."),
      String("<span class='badge slow'>") + T("Webinterface &uuml;ber die IP vom Router", "Web interface via the IP from the router") + "</span> <span class='badge slow'>" + T("bis ca. 8 WLAN-Ger&auml;te, nur 2,4 GHz", "up to approx. 8 WiFi devices, 2.4 GHz only") + "</span>") +
    apFields + apBridgeWarning +
    "<button type='submit'>" + T("&Uuml;bernehmen und neu starten", "Apply and restart") + "</button></form>";

  const bool apOpen = setupApPassword.isEmpty();
  const bool defaultApPassword = !apOpen && setupApPassword == SETUP_AP_PASSWORD;
  String apState;
  if (apOpen && apOpenChosen) apState = String("<p class='bad'>") + T("<b>Offenes WLAN (bewusst gew&auml;hlt).</b> Kein Passwort, keine Verschl&uuml;sselung.", "<b>Open WiFi (chosen deliberately).</b> No password, no encryption.") + "</p>";
  else if (apOpen) apState = String("<p class='bad'>") + T("<b>Kein Passwort gesetzt.</b> Das WLAN ist offen. Lege jetzt ein Passwort fest.", "<b>No password set.</b> The WiFi is open. Set a password now.") + "</p>";
  else if (defaultApPassword) apState = String("<p class='bad'>") + T("Es ist noch das Standard-Passwort aktiv. Da es &ouml;ffentlich bekannt ist, solltest du es jetzt &auml;ndern.", "The default password is still active. As it is publicly known, you should change it now.") + "</p>";
  else apState = String("<p class='ok'>") + T("Ein eigenes Passwort ist gesetzt.", "A custom password is set.") + "</p>";
  const String apForm = String("<h2 id='ap'>") + (apMode ? T("WLAN (Access Point)", "WiFi (access point)") : T("Einrichtungs-WLAN", "Setup WiFi")) + "</h2><div class='" + (apOpen ? "apbox open" : "apbox") + "'>" + apState +
    "<p><small>" + T("Name und Passwort gelten f&uuml;r das Einrichtungs-WLAN und in den Access-Point-Betriebsarten f&uuml;r dein WLAN.", "Name and password apply to the setup WiFi and, in the access point modes, to your WiFi.") + "</small></p>"
    "<form method='post' action='/appass' onsubmit=\"if(!this.ap_open.checked){if(this.ap_new.value!=this.ap_repeat.value){alert(L.mismatch);return false;}" + (apOpen ? "if(!this.ap_new.value){alert(L.pwNeeded);return false;}" : "") + "}return confirm(L.confirmPw);\">"
    "<label>" + T("WLAN-Name", "WiFi name") + "<input name='ap_ssid' maxlength='32' value='" + htmlEscape(apSsid) + "' required></label>"
    "<label class='chk'><input type='checkbox' name='ap_open' value='1'" + (apOpenChosen ? " checked" : "") + "><span><b>" + T("Offenes WLAN ohne Passwort", "Open WiFi without password") + "</b><small>" +
      T("Nur f&uuml;r besondere F&auml;lle, z. B. ein G&auml;ste-WLAN mit aktiver Firewall.", "Only for special cases, e.g. a guest WiFi with the firewall enabled.") + "</small></span></label>" + openWifiWarningHtml() +
    "<div class='pwfields'><label>" + T("Neues Passwort", "New password") + "<input name='ap_new' type='password' minlength='8' maxlength='63' autocomplete='new-password' placeholder='" + (apOpen ? "" : T("Leer lassen, um es zu behalten", "Leave empty to keep it")) + "'></label>"
    "<label>" + T("Neues Passwort wiederholen", "Repeat new password") + "<input name='ap_repeat' type='password' minlength='8' maxlength='63' autocomplete='new-password'></label></div>"
    "<p class='pwfields'><small>" + T("8 bis 63 Zeichen, keine Umlaute. Nach dem Speichern startet die Bridge neu; danach mit dem neuen Namen bzw. Passwort verbinden.", "8 to 63 characters, ASCII only. The bridge restarts after saving; then reconnect with the new name or password.") + "</small></p>"
    "<button type='submit'>" + (apOpen && !apOpenChosen ? T("Passwort festlegen", "Set password") : T("Speichern", "Save")) + "</button></form></div>";

  // Auffaelliger Warnhinweis ganz oben, solange das WLAN offen ist
  String apAlert;
  if (apOpen && apOpenChosen) {
    // Bewusst offen: dauerhaft sichtbarer, aber ruhiger Hinweis
    apAlert = String("<div class='alert static'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' aria-hidden='true'><path d='M12 3L2 21h20L12 3z'/><path d='M12 10v5M12 18v.5'/></svg><div><b>") +
      T("Offenes WLAN aktiv", "Open WiFi active") + "</b><br>" + T("Das WLAN", "The WiFi") + " <b>" + htmlEscape(apSsid) + "</b> " +
      T("hat kein Passwort und ist unverschl&uuml;sselt. Jeder in Reichweite kann es nutzen.", "has no password and is unencrypted. Anyone in range can use it.") + "<br><a href='#ap'>" +
      T("Passwort festlegen &darr;", "Set a password &darr;") + "</a></div></div>";
  } else if (apOpen) {
    apAlert = String("<div class='alert'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' aria-hidden='true'><path d='M12 3L2 21h20L12 3z'/><path d='M12 10v5M12 18v.5'/></svg><div><b>") +
      T("Kein WLAN-Passwort gesetzt!", "No WiFi password set!") + "</b><br>" +
      T("Das WLAN", "The WiFi") + " <b>" + htmlEscape(apSsid) + "</b> " +
      T("ist offen. Jeder in Reichweite kann diese Seite &ouml;ffnen und die Einstellungen &auml;ndern.", "is open. Anyone in range can open this page and change the settings.") + "<br><a href='#ap'>" +
      T("Jetzt Passwort festlegen &darr;", "Set a password now &darr;") + "</a></div></div>";
  }
  if (recoveryTriggered) {
    apAlert += String("<p class='bad'><b>") + T("Notfall-Reset ausgef&uuml;hrt:", "Emergency reset performed:") + "</b> " + T("Die Betriebsart wurde auf NAT zur&uuml;ckgesetzt und der Passwortschutz des Webinterface entfernt.", "The operating mode was reset to NAT and the web interface password protection was removed.") + "</p>";
  }
  if (apMode && !webPasswordSet()) {
    // In den Access-Point-Modi ist das Webinterface auch aus dem Heimnetz erreichbar
    apAlert += String("<div class='dangerbox'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' aria-hidden='true'><rect x='4' y='10' width='16' height='11' rx='2'/><path d='M8 10V7a4 4 0 0 1 7.5-2'/></svg><div><b>") +
      T("Webinterface ohne Passwort", "Web interface without password") + "</b><br>" +
      T("Jeder in deinem Heimnetz und in diesem WLAN kann diese Seite &ouml;ffnen und die Einstellungen &auml;ndern.", "Anyone in your home network and in this WiFi can open this page and change the settings.") +
      " <a href='#webpw'>" + T("Passwort festlegen &darr;", "Set a password &darr;") + "</a></div></div>";
  }

  String footer;
  if (apNat) footer = T("Im Access-Point-Modus mit NAT leitet die Bridge den Verkehr der WLAN-Ger&auml;te &uuml;ber das LAN-Kabel zum Router.", "In access point mode with NAT the bridge routes the traffic of the WiFi devices over the LAN cable to the router.");
  else if (bridge) footer = T("Im Bridge-Modus reicht die Bridge die Daten direkt zum Router durch. Sie selbst hat im Router-Netz keine eigene Adresse; diese Seite ist nur &uuml;ber das Einrichtungs-WLAN erreichbar.",
        "In bridge mode the bridge passes the data straight to the router. It has no address of its own in the router network; this page is only reachable through the setup WiFi.");
  else if (!apMode) footer = T("Das Ethernet-Ger&auml;t bekommt Adresse, Gateway und DNS von der Bridge. Die Bridge &uuml;bersetzt die Verbindung zum Router.",
        "The Ethernet device gets its address, gateway and DNS from the bridge. The bridge translates the connection to the router.");
  footer = "<p><small>" + footer + "</small></p>";

  String routerSection;
  if (!apMode) {
    routerSection = String("<h2>") + T("Router-WLAN", "Router WiFi") + "</h2><button class='secondary' type='button' onclick='scan()'>" + T("Verf&uuml;gbare WLANs suchen", "Search for WiFi networks") + "</button><div id='networks'></div>"
      "<form method='post' action='/save'><label>" + T("WLAN-Name des Routers", "Router WiFi name") + "<input name='ssid' maxlength='32' value='" + htmlEscape(routerSsid) + "' required></label>"
      "<label>" + T("WLAN-Passwort des Routers", "Router WiFi password") + "<input name='password' type='password' maxlength='63' placeholder='" + T("Nur &auml;ndern, wenn n&ouml;tig", "Only change if needed") + "'></label>"
      "<button type='submit'>" + T("Speichern und verbinden", "Save and connect") + "</button></form>";
  }

  const String html = pageHeader(PRODUCT_NAME) + languageSwitchHtml() +
    "<h1>" + PRODUCT_NAME + " <small class='sub'>" + T("Ethernet &#8644; WLAN", "Ethernet &#8644; WiFi") + "</small></h1>" + apAlert + routerState + (apMode ? String("") : signalMeterHtml()) + ethernetState + linkState +
    (bridgeMode == MODE_AP_BRIDGE ? String("") : String("<p>") + (apMode ? T("Diese Seite ist im WLAN erreichbar unter ", "This page is reachable in the WiFi at ") : T("Diese Seite bleibt &uuml;ber das Einrichtungs-WLAN erreichbar: ", "This page stays reachable through the setup WiFi: ")) + "<b>192.168.4.1</b>.</p>") +
    // In den Access-Point-Modi stehen Name und Passwort direkt bei der Betriebsart; der separate Abschnitt entfaellt
    routerSection + modeForm + firewallSectionHtml() + (apMode ? String("") : apForm) + webPasswordSectionHtml() + footer + "<p><small>" + T("Firmware-Version ", "Firmware version ") + FIRMWARE_VERSION + "</small></p>" + script + pageFooter();
  webServer.send(200, "text/html; charset=utf-8", html);
}

void connectToRouter() {
  if (routerSsid.isEmpty()) return;
  WiFi.disconnect(false, false);
  delay(100);
  // Bekannter Kanal: nur dort suchen (kurz, stoert das Einrichtungs-WLAN kaum). Jeder dritte
  // Fehlversuch sucht auf allen Kanaelen, falls der Router den Kanal gewechselt hat.
  const bool useKnownChannel = routerChannel != 0 && (failedConnectAttempts % 3) != 2;
  if (useKnownChannel) WiFi.begin(routerSsid.c_str(), routerPassword.c_str(), routerChannel, routerBssid);
  else WiFi.begin(routerSsid.c_str(), routerPassword.c_str());
  Serial.printf("Verbinde mit %s (%s)\n", routerSsid.c_str(), useKnownChannel ? "bekannter Kanal" : "alle Kanaele");
}

// Plant den naechsten Verbindungsversuch mit wachsendem Abstand (10 s, 20 s, 40 s ... max. 120 s)
void scheduleReconnect() {
  nextConnectAttemptMs = millis() + reconnectDelayMs;
  if (nextConnectAttemptMs == 0) nextConnectAttemptMs = 1;
  reconnectDelayMs = reconnectDelayMs * 2 > RECONNECT_MAX_MS ? RECONNECT_MAX_MS : reconnectDelayMs * 2;
}

void resetReconnect() {
  reconnectDelayMs = RECONNECT_MIN_MS;
  failedConnectAttempts = 0;
  nextConnectAttemptMs = 0;
}

// Wird im loop() aufgerufen (Client-Betriebsarten)
void handleRouterConnection() {
  if (staGotConnected) {
    staGotConnected = false;
    resetReconnect();
    uint8_t *bssid = WiFi.BSSID();
    const uint8_t channel = WiFi.channel();
    if (bssid != nullptr && channel != 0 && (channel != routerChannel || memcmp(bssid, routerBssid, 6) != 0)) {
      routerChannel = channel;
      memcpy(routerBssid, bssid, 6);
      preferences.putUChar("r_chan", routerChannel);
      preferences.putBytes("r_bssid", routerBssid, 6);
    }
  }
  if (routerSsid.isEmpty() || wifiConnected || staLinkUp || scanPausedConnect) return;
  if (nextConnectAttemptMs == 0) {
    scheduleReconnect();
    return;
  }
  if (static_cast<int32_t>(millis() - nextConnectAttemptMs) < 0) return;
  ++failedConnectAttempts;
  connectToRouter();
  scheduleReconnect();
}

// Grund der letzten Trennung (fuer Log und Webinterface)
void onStaDisconnectEvent(void *argument, esp_event_base_t eventBase, int32_t eventId, void *eventData) {
  if (eventId == WIFI_EVENT_STA_DISCONNECTED) {
    const wifi_event_sta_disconnected_t *info = static_cast<const wifi_event_sta_disconnected_t *>(eventData);
    lastStaDisconnectReason = info->reason;
    staLinkUp = false;
  } else if (eventId == WIFI_EVENT_STA_CONNECTED) {
    lastStaDisconnectReason = 0;
    staLinkUp = true;
    staGotConnected = true;
  }
}

const char *staReasonText() {
  switch (lastStaDisconnectReason) {
    case 0: return "";
    case WIFI_REASON_NO_AP_FOUND:
      return T("Router-WLAN nicht gefunden (Name falsch, zu weit entfernt oder nur 5 GHz?)", "Router WiFi not found (wrong name, too far away or 5 GHz only?)");
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
      return T("Anmeldung fehlgeschlagen (Passwort falsch?)", "Authentication failed (wrong password?)");
    default:
      return T("Verbindung fehlgeschlagen", "Connection failed");
  }
}

void saveSettings() {
  detectLanguage();
  const String newSsid = webServer.arg("ssid");
  if (newSsid != routerSsid) {
    routerChannel = 0;  // anderes WLAN: auf allen Kanaelen suchen
    preferences.putUChar("r_chan", 0);
  }
  resetReconnect();
  routerSsid = newSsid;
  const String newPassword = webServer.arg("password");
  preferences.putString("ssid", routerSsid);
  if (!newPassword.isEmpty()) {
    routerPassword = newPassword;
    preferences.putString("password", routerPassword);
  }
  webServer.send(200, "text/html; charset=utf-8", pageHeader(T("Gespeichert", "Saved")) + "<h1>" + T("Gespeichert", "Saved") + "</h1><p>" +
    T("Die Bridge verbindet sich jetzt mit ", "The bridge is now connecting to ") + "<b>" + htmlEscape(routerSsid) + "</b>. " +
    T("Kehre nach ein paar Sekunden zur <a href='/'>Startseite</a> zur&uuml;ck.", "Return to the <a href='/'>start page</a> after a few seconds.") + "</p>" + pageFooter());
  connectToRouter();
}

void sendModeError(const String &message) {
  webServer.send(400, "text/html; charset=utf-8", pageHeader(T("Fehler", "Error")) + "<h1>" + T("Betriebsart nicht ge&auml;ndert", "Operating mode not changed") + "</h1><p class='bad'>" + message + "</p><p><a href='/'>" + T("Zur&uuml;ck zur Startseite", "Back to the start page") + "</a></p>" + pageFooter());
}

void saveMode() {
  detectLanguage();
  const String value = webServer.arg("mode");
  BridgeMode newMode = MODE_NAT;
  if (value == "bridge") newMode = MODE_BRIDGE;
  else if (value == "apnat") newMode = MODE_AP_NAT;
  else if (value == "apbridge") newMode = MODE_AP_BRIDGE;
  const bool newIsAp = newMode == MODE_AP_NAT || newMode == MODE_AP_BRIDGE;

  if (newMode == MODE_AP_BRIDGE && webServer.arg("ack") != "1") {
    sendModeError(T("Bitte best&auml;tige den Hinweis zum Webinterface.", "Please confirm the note about the web interface."));
    return;
  }
  if (newIsAp) {
    // WLAN-Name und Passwort kommen direkt aus dem Formular der Betriebsart
    const bool openWifi = webServer.arg("m_open") == "1";
    if (!openWifi && setupApPassword.isEmpty() && webServer.arg("m_pass").isEmpty()) {
      sendModeError(T("F&uuml;r die Access-Point-Betriebsarten bitte ein WLAN-Passwort festlegen (mindestens 8 Zeichen).",
                      "For the access point modes please set a WiFi password (at least 8 characters)."));
      return;
    }
    String error;
    if (!applyApCredentials(webServer.arg("m_ssid"), webServer.arg("m_pass"), webServer.arg("m_repeat"), error, openWifi)) {
      sendModeError(error);
      return;
    }
  }

  preferences.putUChar("mode", newMode);

  String name = "NAT";
  if (newMode == MODE_BRIDGE) name = "Bridge";
  else if (newMode == MODE_AP_NAT) name = T("Access Point (NAT)", "Access point (NAT)");
  else if (newMode == MODE_AP_BRIDGE) name = T("Access Point (Bridge)", "Access point (bridge)");

  String body = String("<p>") + T("Betriebsart ", "Operating mode ") + "<b>" + name + "</b> " + T("gespeichert. Die Bridge startet neu.", "saved. The bridge is restarting.") + "</p>";
  if (newMode == MODE_AP_NAT) {
    body += String("<p>") + T("Verbinde den LAN-Port mit deinem Router. Verbinde dich danach mit dem WLAN ", "Connect the LAN port to your router. Then connect to the WiFi ") + "<b>" + htmlEscape(apSsid) + "</b> " +
      T("und &ouml;ffne", "and open") + " <a href='/'>192.168.4.1</a>.</p>";
  } else if (newMode == MODE_AP_BRIDGE) {
    body += String("<p class='bad'><b>") + T("Das Webinterface ist ab jetzt nicht mehr unter 192.168.4.1 erreichbar, sondern unter der IP, die der Router der Bridge gibt: ", "From now on the web interface is no longer at 192.168.4.1 but at the IP your router assigns to the bridge: ") +
      "http://" + BRIDGE_HOSTNAME + ".fritz.box</b></p><p>" +
      T("Verbinde den LAN-Port mit deinem Router. WLAN-Ger&auml;te verbinden sich mit ", "Connect the LAN port to your router. WiFi devices connect to ") + "<b>" + htmlEscape(apSsid) + "</b> " +
      T("und bekommen ihre Adresse vom Router.", "and get their address from the router.") + "</p><p>" +
      T("<b>Zur&uuml;ck zur Einrichtung:</b> Stromversorgung 3-mal hintereinander kurz aus- und wieder einschalten (jeweils innerhalb von 10 Sekunden).", "<b>Back to setup:</b> switch the power off and on again 3 times in a row (each within 10 seconds).") + "</p>";
  } else {
    body += String("<p>") + T("Verbinde dich danach wieder mit dem WLAN ", "Afterwards, reconnect to the WiFi ") + "<b>" + htmlEscape(apSsid) + "</b> " + T("und &ouml;ffne", "and open") + " <a href='/'>192.168.4.1</a>.</p><p><small>" +
      T("Ziehe am LAN-Ger&auml;t kurz das Kabel ab oder erneuere dort die IP-Adresse, damit es eine Adresse aus dem neuen Netz holt.", "Briefly unplug the cable of the LAN device or renew its IP address so it gets an address from the new network.") + "</small></p>";
  }
  webServer.send(200, "text/html; charset=utf-8", pageHeader(T("Neustart", "Restart")) + "<h1>" + T("Neustart", "Restarting") + "</h1>" + body + pageFooter());
  restartAtMs = millis() + 1500;  // Antwort erst noch ausliefern
}

// WPA2-Passphrase: 8 bis 63 druckbare ASCII-Zeichen
bool isValidWifiPassword(const String &password) {
  if (password.length() < 8 || password.length() > 63) return false;
  for (size_t i = 0; i < password.length(); ++i) {
    const char c = password[i];
    if (c < 32 || c > 126) return false;
  }
  return true;
}

void sendApPasswordError(const String &message) {
  webServer.send(400, "text/html; charset=utf-8", pageHeader(T("Fehler", "Error")) + "<h1>" + T("Passwort nicht ge&auml;ndert", "Password not changed") + "</h1><p class='bad'>" + message + "</p><p><a href='/'>" + T("Zur&uuml;ck zur Startseite", "Back to the start page") + "</a></p>" + pageFooter());
}

// Warnung zum offenen WLAN (wird nur angezeigt, wenn die Option angehakt ist)
String openWifiWarningHtml() {
  return String("<div class='openwarn'><svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' aria-hidden='true'><path d='M12 3L2 21h20L12 3z'/><path d='M12 10v5M12 18v.5'/></svg><div><b>") +
    T("Offenes WLAN: kein Passwort, keine Verschl&uuml;sselung!", "Open WiFi: no password, no encryption!") + "</b><br>" +
    T("Jeder in Reichweite kann sich verbinden, deinen Internetanschluss nutzen und dieses Webinterface &ouml;ffnen. Der Funkverkehr ist unverschl&uuml;sselt und kann mitgelesen werden. Im Modus &bdquo;Access Point (Bridge)&ldquo; landen fremde Ger&auml;te direkt in deinem Heimnetz.",
      "Anyone in range can connect, use your internet connection and open this web interface. Radio traffic is unencrypted and can be read by others. In \"access point (bridge)\" mode, foreign devices end up directly in your home network.") + "<br>" +
    T("<b>Tipp:</b> in der Firewall &bdquo;Nur Internet, kein Heimnetz&ldquo; und &bdquo;Webinterface der Bridge sperren&ldquo; einschalten.",
      "<b>Tip:</b> enable \"Internet only, no home network\" and \"Block the bridge web interface\" in the firewall.") + "</div></div>";
}

// Prueft und speichert Name/Passwort des eigenen WLANs. Leeres Passwort = bisheriges behalten.
bool applyApCredentials(String ssid, const String &password, const String &repeat, String &error, bool openWifi) {
  ssid.trim();
  if (!isValidSsid(ssid)) {
    error = T("Der WLAN-Name muss 1 bis 32 Zeichen lang sein (keine Umlaute).", "The WiFi name must be 1 to 32 characters long (ASCII only).");
    return false;
  }
  if (openWifi) {
    // Bewusst offenes WLAN: Passwort loeschen und die Wahl merken
    preferences.putString("ap_ssid", ssid);
    apSsid = ssid;
    preferences.putString("ap_pass", "");
    setupApPassword = "";
    apOpenChosen = true;
    preferences.putBool("ap_openok", true);
    Serial.println("WLAN der Bridge: OFFEN (bewusst gewaehlt)");
    return true;
  }
  if (password != repeat) {
    error = T("Die beiden Passwort-Eingaben stimmen nicht &uuml;berein.", "The two password entries do not match.");
    return false;
  }
  const bool keepPassword = password.isEmpty() && !setupApPassword.isEmpty();
  if (!keepPassword && !isValidWifiPassword(password)) {
    error = T("Das Passwort muss 8 bis 63 Zeichen lang sein und darf nur Buchstaben, Ziffern, Leerzeichen und die &uuml;blichen Sonderzeichen enthalten (keine Umlaute).",
              "The password must be 8 to 63 characters long and may only contain letters, digits, spaces and common special characters (ASCII only).");
    return false;
  }
  preferences.putString("ap_ssid", ssid);
  apSsid = ssid;
  if (!keepPassword) {
    preferences.putString("ap_pass", password);
    setupApPassword = password;
  }
  if (apOpenChosen) {
    apOpenChosen = false;
    preferences.putBool("ap_openok", false);
  }
  Serial.println("WLAN-Name/Passwort der Bridge gespeichert");
  return true;
}

void saveApPassword() {
  detectLanguage();
  String error;
  if (!applyApCredentials(webServer.arg("ap_ssid"), webServer.arg("ap_new"), webServer.arg("ap_repeat"), error, webServer.arg("ap_open") == "1")) {
    sendApPasswordError(error);
    return;
  }
  webServer.send(200, "text/html; charset=utf-8", pageHeader(T("Gespeichert", "Saved")) + "<h1>" + T("Gespeichert", "Saved") + "</h1><p>" +
    T("Die Einstellungen f&uuml;r das WLAN ", "The settings for the WiFi ") + "<b>" + htmlEscape(apSsid) + "</b> " +
    T("sind gespeichert. Die Bridge startet jetzt neu.", "have been saved. The bridge is restarting now.") + "</p><p>" +
    T("Verbinde dich danach mit dem WLAN (ggf. mit dem neuen Namen bzw. Passwort) und &ouml;ffne <a href='/'>192.168.4.1</a>. Eventuell musst du das WLAN auf deinem Ger&auml;t vorher &bdquo;vergessen&ldquo;.",
      "Afterwards, connect to the WiFi (with the new name or password, if changed) and open <a href='/'>192.168.4.1</a>. You may have to \"forget\" the WiFi on your device first.") +
    "</p>" + pageFooter());
  restartAtMs = millis() + 1500;
}

// ---------------------------------------------------------------------------
// Passwortschutz fuer das Webinterface (optional)
// Anmeldung per Formular; danach merkt sich der Browser ein zufaelliges Sitzungs-Cookie.
// Gespeichert wird nur ein gesalzener SHA-256-Hash des Passworts. Der Notfall-Reset entfernt den Schutz.
// ---------------------------------------------------------------------------

struct WebSession {
  char token[33];   // leer = Platz frei
  uint32_t lastMs;  // letzte Nutzung
};
constexpr int WEB_MAX_SESSIONS = 4;
constexpr uint32_t WEB_SESSION_IDLE_MS = 24UL * 60 * 60 * 1000;  // nach 24 h ohne Nutzung neu anmelden
constexpr uint8_t WEB_MAX_FAILS = 5;                             // danach ...
constexpr uint32_t WEB_BLOCK_MS = 60000;                         // ... 1 Minute keine Anmeldung
String webPassHash;  // Hex, leer = kein Passwortschutz
String webPassSalt;
WebSession webSessions[WEB_MAX_SESSIONS] = {};
uint8_t webLoginFails = 0;
uint32_t webLoginBlockedUntil = 0;

bool webPasswordSet() { return !webPassHash.isEmpty(); }

String randomHex(int words) {
  String text;
  char part[9];
  for (int i = 0; i < words; ++i) {
    snprintf(part, sizeof(part), "%08lx", static_cast<unsigned long>(esp_random()));
    text += part;
  }
  return text;
}

String hashPassword(const String &salt, const String &password) {
  const String input = salt + password;
  uint8_t digest[32];
  mbedtls_sha256(reinterpret_cast<const unsigned char *>(input.c_str()), input.length(), digest, 0);
  char hex[65];
  for (int i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", digest[i]);
  return String(hex);
}

// Vergleich mit konstanter Laufzeit (verraet nicht, ab welchem Zeichen es abweicht)
bool sameSecret(const String &a, const String &b) {
  if (a.length() != b.length()) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < a.length(); ++i) diff |= static_cast<uint8_t>(a[i] ^ b[i]);
  return diff == 0;
}

// Leeres Passwort = Schutz entfernen. Beendet alle Anmeldungen.
void setWebPassword(const String &password) {
  if (password.isEmpty()) {
    webPassHash = "";
    webPassSalt = "";
    preferences.remove("web_hash");
    preferences.remove("web_salt");
  } else {
    webPassSalt = randomHex(4);
    webPassHash = hashPassword(webPassSalt, password);
    preferences.putString("web_salt", webPassSalt);
    preferences.putString("web_hash", webPassHash);
  }
  memset(webSessions, 0, sizeof(webSessions));
}

String sessionToken() {
  const String cookie = webServer.header("Cookie");
  const int position = cookie.indexOf("nb_session=");
  if (position < 0) return String();
  return cookie.substring(position + 11, position + 11 + 32);
}

bool sessionValid() {
  const String token = sessionToken();
  if (token.length() != 32) return false;
  const uint32_t now = millis();
  for (WebSession &session : webSessions) {
    if (session.token[0] == 0) continue;
    if (now - session.lastMs > WEB_SESSION_IDLE_MS) {
      session.token[0] = 0;
      continue;
    }
    if (sameSecret(String(session.token), token)) {
      session.lastMs = now;
      return true;
    }
  }
  return false;
}

// Neue Sitzung fuer diesen Browser (Cookie wird mit der naechsten Antwort gesendet)
void startSession() {
  WebSession *slot = &webSessions[0];
  for (WebSession &session : webSessions) {
    if (session.token[0] == 0) {
      slot = &session;
      break;
    }
    if (static_cast<int32_t>(session.lastMs - slot->lastMs) < 0) slot = &session;
  }
  const String token = randomHex(4);
  memcpy(slot->token, token.c_str(), 32);
  slot->token[32] = 0;
  slot->lastMs = millis();
  webServer.sendHeader("Set-Cookie", String("nb_session=") + token + "; Path=/; HttpOnly; SameSite=Strict");
}

void showLogin(const String &message, int code) {
  detectLanguage();
  webServer.send(code, "text/html; charset=utf-8", pageHeader(T("Anmelden", "Log in")) + languageSwitchHtml() +
    "<h1>" + PRODUCT_NAME + " <small class='sub'>" + T("Anmeldung", "Login") + "</small></h1>" +
    (message.isEmpty() ? String("") : String("<p class='bad'>") + message + "</p>") +
    "<form method='post' action='/login'><label>" + T("Passwort des Webinterface", "Web interface password") +
    "<input name='pw' type='password' autocomplete='current-password' autofocus required></label><button type='submit'>" + T("Anmelden", "Log in") + "</button></form>"
    "<p><small>" + T("Passwort vergessen? Stromversorgung 3-mal hintereinander kurz aus- und wieder einschalten (jeweils innerhalb von 10 Sekunden). Danach ist der Passwortschutz entfernt und die Bridge startet im NAT-Modus mit dem Einrichtungs-WLAN.",
      "Forgot the password? Switch the power off and on again 3 times in a row (each within 10 seconds). Password protection is then removed and the bridge starts in NAT mode with the setup WiFi.") +
    "</small></p>" + pageFooter());
}

// true = Anfrage darf bearbeitet werden. Sonst wurde schon die Anmeldeseite (bzw. 401 fuer Abfragen) gesendet.
bool requireAuth() {
  if (!webPasswordSet() || sessionValid()) return true;
  const String uri = webServer.uri();
  if (uri == "/status" || uri == "/networks") {
    webServer.send(401, "application/json", "{\"login\":true}");  // die Seite laedt sich dann neu
    return false;
  }
  showLogin("", 401);
  return false;
}

template <void (*Handler)()>
void guarded() {
  if (requireAuth()) Handler();
}

void showHomeGuarded() {
  if (captiveRedirect()) return;  // Captive Portal zuerst: die Weiterleitung verraet nichts
  if (requireAuth()) showHome();
}

void redirectHome() {
  webServer.sendHeader("Location", "/");
  webServer.send(303, "text/plain", "");
}

void doLogin() {
  detectLanguage();
  if (!webPasswordSet()) {
    redirectHome();
    return;
  }
  if (webLoginBlockedUntil != 0) {
    if (static_cast<int32_t>(millis() - webLoginBlockedUntil) < 0) {
      showLogin(T("Zu viele Fehlversuche. Bitte warte eine Minute.", "Too many failed attempts. Please wait a minute."), 429);
      return;
    }
    webLoginBlockedUntil = 0;
  }
  if (sameSecret(hashPassword(webPassSalt, webServer.arg("pw")), webPassHash)) {
    webLoginFails = 0;
    startSession();
    Serial.println("Webinterface: Anmeldung erfolgreich");
    redirectHome();
    return;
  }
  Serial.println("Webinterface: falsches Passwort");
  if (++webLoginFails >= WEB_MAX_FAILS) {
    webLoginFails = 0;
    webLoginBlockedUntil = stampMs() + WEB_BLOCK_MS;
    if (webLoginBlockedUntil == 0) webLoginBlockedUntil = 1;
    Serial.println("Webinterface: zu viele Fehlversuche, Anmeldung 1 Minute gesperrt");
  }
  showLogin(T("Falsches Passwort.", "Wrong password."), 401);
}

void doLogout() {
  const String token = sessionToken();
  for (WebSession &session : webSessions) {
    if (session.token[0] != 0 && token.length() == 32 && sameSecret(String(session.token), token)) session.token[0] = 0;
  }
  webServer.sendHeader("Set-Cookie", "nb_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
  redirectHome();
}

void sendWebPasswordPage(int code, const char *titleDe, const char *titleEn, const String &body) {
  webServer.send(code, "text/html; charset=utf-8", pageHeader(T(titleDe, titleEn)) + "<h1>" + T(titleDe, titleEn) + "</h1>" + body +
    "<p><a href='/'>" + T("Zur&uuml;ck zur Startseite", "Back to the start page") + "</a></p>" + pageFooter());
}

void saveWebPassword() {
  detectLanguage();
  if (webServer.arg("web_off") == "1") {
    setWebPassword("");
    Serial.println("Webinterface: Passwortschutz entfernt");
    sendWebPasswordPage(200, "Passwortschutz entfernt", "Password protection removed", String("<p class='bad'>") +
      T("Das Webinterface ist jetzt wieder ohne Passwort erreichbar.", "The web interface can now be opened without a password again.") + "</p>");
    return;
  }
  const String password = webServer.arg("web_new");
  if (password != webServer.arg("web_repeat")) {
    sendWebPasswordPage(400, "Passwort nicht ge&auml;ndert", "Password not changed", String("<p class='bad'>") +
      T("Die beiden Passwort-Eingaben stimmen nicht &uuml;berein.", "The two password entries do not match.") + "</p>");
    return;
  }
  if (!isValidWifiPassword(password)) {
    sendWebPasswordPage(400, "Passwort nicht ge&auml;ndert", "Password not changed", String("<p class='bad'>") +
      T("Das Passwort muss 8 bis 63 Zeichen lang sein (keine Umlaute).", "The password must be 8 to 63 characters long (ASCII only).") + "</p>");
    return;
  }
  setWebPassword(password);
  startSession();  // dieser Browser bleibt angemeldet, alle anderen muessen sich neu anmelden
  Serial.println("Webinterface: Passwort gespeichert");
  sendWebPasswordPage(200, "Passwort gespeichert", "Password saved", String("<p class='ok'>") +
    T("Das Webinterface ist jetzt durch ein Passwort gesch&uuml;tzt. Dieser Browser bleibt angemeldet; andere Ger&auml;te m&uuml;ssen sich anmelden.",
      "The web interface is now protected by a password. This browser stays logged in; other devices have to log in.") + "</p>");
}

String webPasswordSectionHtml() {
  const bool set = webPasswordSet();
  String html = String("<h2 id='webpw'>") + T("Passwort f&uuml;r dieses Webinterface", "Password for this web interface") + "</h2>";
  if (set) {
    html += String("<p class='ok'>") + T("Passwortschutz ist aktiv.", "Password protection is enabled.") + "</p>";
  } else {
    html += String("<p class='wait'>") + T("Kein Passwort gesetzt: Jeder, der diese Seite erreicht, kann die Einstellungen &auml;ndern.", "No password set: anyone who can reach this page can change the settings.") +
      (isApMode() ? T(" Im Access-Point-Modus ist sie auch aus deinem Heimnetz erreichbar.", " In access point mode it can also be reached from your home network.") : "") + "</p>";
  }
  html += String("<form method='post' action='/webpass' onsubmit=\"if(!(this.web_off&&this.web_off.checked)){if(this.web_new.value.length<8){alert(L.webPwShort);return false;}if(this.web_new.value!=this.web_repeat.value){alert(L.mismatch);return false;}}return true;\">");
  if (set) {
    html += String("<label class='chk'><input type='checkbox' name='web_off' value='1'><span><b>") + T("Passwortschutz entfernen", "Remove password protection") + "</b></span></label>";
  }
  html += String("<div class='pwfields'><label>") + T("Neues Passwort", "New password") + "<input name='web_new' type='password' maxlength='63' autocomplete='new-password'></label>"
    "<label>" + T("Neues Passwort wiederholen", "Repeat new password") + "<input name='web_repeat' type='password' maxlength='63' autocomplete='new-password'></label>"
    "<p><small>" + T("8 bis 63 Zeichen, keine Umlaute. Vergessen? Der Notfall-Reset (3-mal Strom aus/an) entfernt den Passwortschutz.",
      "8 to 63 characters, ASCII only. Forgot it? The emergency reset (power off/on 3 times) removes the password protection.") + "</small></p></div>"
    "<button type='submit'>" + (set ? T("Speichern", "Save") : T("Passwort festlegen", "Set password")) + "</button></form>";
  if (set) {
    html += String("<form method='post' action='/logout'><button class='secondary' type='submit'>") + T("Abmelden", "Log out") + "</button></form>";
  }
  return html;
}

// ---------------------------------------------------------------------------
// Access-Point-Modi
// ---------------------------------------------------------------------------

bool isApMode() { return bridgeMode == MODE_AP_NAT || bridgeMode == MODE_AP_BRIDGE; }

// SSID: 1 bis 32 druckbare ASCII-Zeichen
bool isValidSsid(const String &ssid) {
  if (ssid.length() < 1 || ssid.length() > 32) return false;
  for (size_t i = 0; i < ssid.length(); ++i) {
    const char c = ssid[i];
    if (c < 32 || c > 126) return false;
  }
  return true;
}

// AP-NAT: LAN-Port holt sich per DHCP eine Adresse vom Router (Uplink), das WLAN bekommt NAT.
void startApNatUplink() {
  esp_netif_inherent_config_t netifConfig = ESP_NETIF_INHERENT_DEFAULT_ETH();  // DHCP-Client
  netifConfig.if_key = "ETH_UPLINK";
  netifConfig.if_desc = "ethernet-uplink";
  esp_netif_config_t config{};
  config.base = &netifConfig;
  config.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;
  ethernetNetif = esp_netif_new(&config);
  if (ethernetNetif == nullptr || esp_netif_attach(ethernetNetif, esp_eth_new_netif_glue(ethernetHandle)) != ESP_OK) {
    Serial.println("AP-NAT: Uplink-Schnittstelle konnte nicht angelegt werden");
    return;
  }
  esp_netif_set_hostname(ethernetNetif, BRIDGE_HOSTNAME);
  esp_eth_update_input_path(ethernetHandle, onApNatUplinkInput, nullptr);  // Empfang mit Bytezaehler
  useCountingTransmit();
  esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &onUplinkIpEvent, nullptr);
  if (esp_eth_start(ethernetHandle) != ESP_OK) {
    Serial.println("AP-NAT: Ethernet konnte nicht eingeschaltet werden");
    return;
  }
  const esp_err_t result = esp_netif_napt_enable(WiFi.AP.netif());
  if (result != ESP_OK) Serial.printf("AP-NAT: NAT konnte nicht aktiviert werden: 0x%x\n", static_cast<unsigned>(result));
  ethernetLanReady = true;
  Serial.println("AP-NAT: WLAN 192.168.4.x, Uplink per DHCP ueber den LAN-Port");
}

void onUplinkIpEvent(void *argument, esp_event_base_t eventBase, int32_t eventId, void *eventData) {
  const ip_event_got_ip_t *info = static_cast<const ip_event_got_ip_t *>(eventData);
  if (info->esp_netif != ethernetNetif) return;
  Serial.printf("Uplink-IP vom Router: " IPSTR " (Gateway " IPSTR ")\n", IP2STR(&info->ip_info.ip), IP2STR(&info->ip_info.gw));
  fwRouterIp = toHostOrder(info->ip_info.gw.addr);
  fwOwnIpExtra = toHostOrder(info->ip_info.ip.addr);
  homeIp = info->ip_info.ip.addr;
  homeMask = info->ip_info.netmask.addr;
  if (bridgeMode == MODE_AP_NAT) apDnsUpdatePending = true;
}

// AP-NAT: DNS-Server des Routers an die WLAN-Clients weitergeben (im loop, nicht im Event-Task)
void updateApDns() {
  apDnsUpdatePending = false;
  esp_netif_dns_info_t dns{};
  if (ethernetNetif == nullptr || esp_netif_get_dns_info(ethernetNetif, ESP_NETIF_DNS_MAIN, &dns) != ESP_OK || dns.ip.u_addr.ip4.addr == 0) return;
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0), IPAddress((uint32_t)0), IPAddress(dns.ip.u_addr.ip4.addr));
  esp_netif_napt_enable(WiFi.AP.netif());
  fwDnsIp = toHostOrder(dns.ip.u_addr.ip4.addr);
  Serial.printf("AP-NAT: DNS fuer WLAN-Clients: " IPSTR "\n", IP2STR(&dns.ip.u_addr.ip4));
}


// NAT-Modus: Frames vom LAN-Geraet pruefen, bevor sie in den TCP/IP-Stack gehen
esp_err_t onNatLanInput(esp_eth_handle_t handle, uint8_t *buffer, uint32_t len, void *priv) {
  countLanRx(len);
  if (!fwCheckFrame(buffer, len, true)) {
    free(buffer);
    return ESP_OK;
  }
  sniffDhcp(buffer, len);
  return esp_netif_receive(ethernetNetif, buffer, len, nullptr);
}

// AP-NAT: Frames der WLAN-Geraete pruefen, bevor sie in den TCP/IP-Stack gehen
esp_err_t onApNatWifiFrame(void *buffer, uint16_t len, void *eb) {
  if (!fwCheckApFrame(static_cast<uint8_t *>(buffer), len)) {
    esp_wifi_internal_free_rx_buffer(eb);
    return ESP_OK;
  }
  sniffDhcp(static_cast<uint8_t *>(buffer), len);
  return esp_netif_receive(apNetif, buffer, len, eb);
}

// Access-Point-Modi: verbundene Geraete merken, MAC-Filter, Empfangsfilter (nach den Standard-Handlern)
void onApStaEvent(void *argument, esp_event_base_t eventBase, int32_t eventId, void *eventData) {
  if (eventId == WIFI_EVENT_AP_STACONNECTED) {
    const wifi_event_ap_staconnected_t *info = static_cast<const wifi_event_ap_staconnected_t *>(eventData);
    if (fwEnabled && fwMacFilter && !fwMacListed(info->mac)) {
      Serial.printf("MAC-Filter: %s abgewiesen\n", macToString(info->mac).c_str());
      esp_wifi_deauth_sta(info->aid);
      return;
    }
    portENTER_CRITICAL(&fwLock);
    for (int i = 0; i < FW_MAX_STATIONS; ++i) {
      if (!apStationUsed[i]) { apStationUsed[i] = true; memcpy(apStationMacs[i], info->mac, 6); break; }
    }
    portEXIT_CRITICAL(&fwLock);
    noteClientConnected(info->mac);
  } else if (eventId == WIFI_EVENT_AP_STADISCONNECTED) {
    const wifi_event_ap_stadisconnected_t *info = static_cast<const wifi_event_ap_stadisconnected_t *>(eventData);
    portENTER_CRITICAL(&fwLock);
    for (int i = 0; i < FW_MAX_STATIONS; ++i) {
      if (apStationUsed[i] && memcmp(apStationMacs[i], info->mac, 6) == 0) apStationUsed[i] = false;
    }
    portEXIT_CRITICAL(&fwLock);
  }
  if (bridgeMode == MODE_AP_NAT && (eventId == WIFI_EVENT_AP_START || eventId == WIFI_EVENT_AP_STACONNECTED)) {
    esp_wifi_internal_reg_rxcb(WIFI_IF_AP, onApNatWifiFrame);
  }
}

// AP-Bridge: WLAN -> Ethernet (laeuft im WLAN-Task)
// Kopie eines Frames an den eigenen TCP/IP-Stack der Bridge geben (Webinterface im AP-Bridge-Modus)
void deliverCopyToBridge(const void *frame, size_t len) {
  if (ethernetNetif == nullptr) return;
  void *copy = malloc(len);
  if (copy == nullptr) return;
  memcpy(copy, frame, len);
  esp_netif_receive(ethernetNetif, copy, len, nullptr);  // gibt die Kopie spaeter mit free() frei
}

esp_err_t onApWifiFrame(void *buffer, uint16_t len, void *eb) {
  const uint8_t *frame = static_cast<const uint8_t *>(buffer);
  if (len >= BR_ETH_HEADER_LEN && fwCheckApFrame(frame, len)) {
    sniffDhcp(frame, len);
    const bool forBridge = memcmp(frame, ethMac, 6) == 0;
    const bool group = frame[0] & 0x01;
    if (forBridge || group) deliverCopyToBridge(frame, len);  // Webinterface, ARP, DHCP
    if (!forBridge && ethernetLinkUp) {
      if (esp_eth_transmit(ethernetHandle, buffer, len) == ESP_OK) {
        framesToLan = framesToLan + 1;
        countLanTx(len);
      } else {
        framesDropped = framesDropped + 1;
      }
    }
  }
  esp_wifi_internal_free_rx_buffer(eb);
  return ESP_OK;
}

// Sendeweg des eigenen TCP/IP-Stacks im AP-Bridge-Modus: an WLAN-Geraete ueber den AP, sonst ueber Ethernet
void apBridgeNetifFree(void *handle, void *buffer) { free(buffer); }

esp_err_t apBridgeNetifTransmit(void *handle, void *buffer, size_t len) {
  const uint8_t *frame = static_cast<const uint8_t *>(buffer);
  const bool group = frame[0] & 0x01;
  const bool toStation = !group && fwIsApStation(frame);
  if (group || toStation) esp_wifi_internal_tx(WIFI_IF_AP, buffer, static_cast<uint16_t>(len));
  if (toStation) return ESP_OK;
  const esp_err_t result = esp_eth_transmit(ethernetHandle, buffer, len);
  if (result == ESP_OK) countLanTx(len);
  return result;
}

// NAT und AP-NAT: Sendeweg des TCP/IP-Stacks ueber Ethernet, mit Bytezaehler (ersetzt den Weg von esp_netif_attach)
esp_err_t countingNetifTransmit(void *handle, void *buffer, size_t len) {
  const esp_err_t result = esp_eth_transmit(ethernetHandle, buffer, len);
  if (result == ESP_OK) countLanTx(len);
  return result;
}

// AP-NAT: Empfang vom Uplink zaehlen und an den TCP/IP-Stack geben
esp_err_t onApNatUplinkInput(esp_eth_handle_t handle, uint8_t *buffer, uint32_t len, void *priv) {
  countLanRx(len);
  return esp_netif_receive(ethernetNetif, buffer, len, nullptr);
}

void useCountingTransmit() {
  esp_netif_driver_ifconfig_t driver{};
  driver.handle = ethernetHandle;
  driver.transmit = countingNetifTransmit;
  driver.driver_free_rx_buffer = apBridgeNetifFree;
  esp_netif_set_driver_config(ethernetNetif, &driver);
}


// AP-Bridge: Ethernet -> Warteschlange (laeuft im Empfangs-Task des Ethernet-Treibers)
esp_err_t onUplinkFrame(esp_eth_handle_t handle, uint8_t *buffer, uint32_t len, void *priv) {
  countLanRx(len);
  ForwardFrame frame{buffer, static_cast<uint16_t>(len)};
  if (len < BR_ETH_HEADER_LEN || len > 1600) {
    free(buffer);
    return ESP_OK;
  }
  // An die Bridge selbst (Webinterface, DHCP-Antwort): direkt an den eigenen TCP/IP-Stack
  if (ethernetNetif != nullptr && memcmp(buffer, ethMac, 6) == 0) {
    return esp_netif_receive(ethernetNetif, buffer, len, nullptr);
  }
  if (buffer[0] & 0x01) deliverCopyToBridge(buffer, len);  // Broadcast/Multicast: auch an die Bridge
  fwLearnDhcp(buffer, static_cast<uint16_t>(len));
  learnNeighbor(buffer, len);    // Geraete im Heimnetz
  sniffDhcp(buffer, len, true);  // DHCP-Bestaetigung fuer WLAN-Geraete, Namen der Geraete im Heimnetz
  if (!fwCheckFrame(buffer, len, false)) {
    free(buffer);
    return ESP_OK;
  }
  if (apStationCount <= 0 || len > 1600 || xQueueSend(apForwardQueue, &frame, pdMS_TO_TICKS(20)) != pdTRUE) {
    if (apStationCount > 0) framesDropped = framesDropped + 1;
    free(buffer);
  }
  return ESP_OK;
}

// AP-Bridge: Warteschlange -> WLAN. Das WLAN ist langsamer als Ethernet, daher mit kurzen Wiederholungen.
void apForwardTask(void *argument) {
  ForwardFrame frame;
  for (;;) {
    if (xQueueReceive(apForwardQueue, &frame, portMAX_DELAY) != pdTRUE) continue;
    int result = -1;
    for (uint32_t wait = 0; wait < 100; wait += 2) {
      result = esp_wifi_internal_tx(WIFI_IF_AP, frame.data, frame.len);
      if (result == ESP_OK) break;
      vTaskDelay(pdMS_TO_TICKS(wait));
    }
    if (result == ESP_OK) framesToWifi = framesToWifi + 1;
    else framesDropped = framesDropped + 1;
    free(frame.data);
  }
}

void onApDriverEvent(void *argument, esp_event_base_t eventBase, int32_t eventId, void *eventData) {
  if (eventId == WIFI_EVENT_AP_STACONNECTED) {
    apStationCount = apStationCount + 1;
    // Alle WLAN-Frames an die Bridge statt an den eigenen TCP/IP-Stack liefern
    esp_wifi_internal_reg_rxcb(WIFI_IF_AP, onApWifiFrame);
  } else if (eventId == WIFI_EVENT_AP_STADISCONNECTED) {
    if (apStationCount > 0) apStationCount = apStationCount - 1;
  }
}

// AP-Bridge: Ethernet und WLAN-Access-Point auf Ebene 2 verbinden (wie Espressifs Beispiel "eth2ap").
void startApBridge() {
  apForwardQueue = xQueueCreate(40, sizeof(ForwardFrame));
  if (apForwardQueue == nullptr || xTaskCreate(apForwardTask, "eth2ap", 3072, nullptr, tskIDLE_PRIORITY + 2, nullptr) != pdPASS) {
    Serial.println("AP-Bridge: Warteschlange konnte nicht angelegt werden");
    return;
  }
  // Eigene Netzwerk-Schnittstelle mit DHCP-Client, damit das Webinterface im Heimnetz erreichbar ist
  esp_netif_inherent_config_t netifConfig = ESP_NETIF_INHERENT_DEFAULT_ETH();
  netifConfig.if_key = "ETH_MGMT";
  netifConfig.if_desc = "ethernet-management";
  esp_netif_config_t config{};
  config.base = &netifConfig;
  config.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;
  ethernetNetif = esp_netif_new(&config);
  if (ethernetNetif != nullptr && esp_netif_attach(ethernetNetif, esp_eth_new_netif_glue(ethernetHandle)) == ESP_OK) {
    esp_netif_set_hostname(ethernetNetif, BRIDGE_HOSTNAME);
    // Senden: an WLAN-Geraete ueber den Access Point, sonst ueber Ethernet (ersetzt den Weg von esp_netif_attach)
    esp_netif_driver_ifconfig_t driver{};
    driver.handle = ethernetHandle;
    driver.transmit = apBridgeNetifTransmit;
    driver.driver_free_rx_buffer = apBridgeNetifFree;
    esp_netif_set_driver_config(ethernetNetif, &driver);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &onUplinkIpEvent, nullptr);
  } else {
    ethernetNetif = nullptr;
    Serial.println("AP-Bridge: Verwaltungs-Schnittstelle konnte nicht angelegt werden (kein Webinterface)");
  }
  // Empfang: alle Frames zuerst an onUplinkFrame (ersetzt den Eingang, den esp_netif_attach gesetzt hat)
  if (esp_eth_update_input_path(ethernetHandle, onUplinkFrame, nullptr) != ESP_OK) {
    Serial.println("AP-Bridge: Ethernet-Empfang konnte nicht umgeleitet werden");
    return;
  }
  bool promiscuous = true;
  esp_eth_ioctl(ethernetHandle, ETH_CMD_S_PROMISCUOUS, &promiscuous);
  if (esp_eth_start(ethernetHandle) != ESP_OK) {
    Serial.println("AP-Bridge: Ethernet konnte nicht eingeschaltet werden");
    return;
  }
  ethernetLanReady = true;
  Serial.printf("AP-Bridge: WLAN-Geraete bekommen ihre Adressen direkt vom Router. Webinterface: http://%s.fritz.box bzw. IP vom Router\n", BRIDGE_HOSTNAME);
  Serial.println("Notfall-Reset: Stromversorgung 3x hintereinander kurz (unter 10 s) aus- und wieder einschalten.");
}


// ---------------------------------------------------------------------------
// Firewall im Webinterface
// ---------------------------------------------------------------------------

String firewallStatusJson() {
  String json = "{\"enabled\":" + String(fwEnabled ? "true" : "false") + ",\"blocked\":" + String(fwBlocked) + ",\"hits\":[";
  for (int i = 0; i < fwRuleCount; ++i) {
    if (i) json += ',';
    json += String(fwHits[i]);
  }
  return json + "]}";
}

String fwCheckbox(const char *name, bool checked, const char *title, const String &text) {
  return String("<label class='chk'><input type='checkbox' name='") + name + "' value='1'" + (checked ? " checked" : "") + "><span><b>" + title + "</b><small>" + text + "</small></span></label>";
}

String fwOption(const char *value, const char *label, bool selected) {
  return String("<option value='") + value + "'" + (selected ? " selected" : "") + ">" + label + "</option>";
}

String firewallSectionHtml() {
  const bool apMode = isApMode();
  String html = String("<h2 id='fw'>") + T("Firewall", "Firewall") + "</h2><form method='post' action='/firewall'><div class='fwbox'>" +
    fwCheckbox("fw_on", fwEnabled, T("Firewall aktiv", "Firewall enabled"),
      T("Pr&uuml;ft den Datenverkehr der angeschlossenen Ger&auml;te (LAN-Port bzw. WLAN-Ger&auml;te im Access-Point-Modus). Nur IPv4; IPv6 wird bei aktiver Firewall gesperrt.",
        "Checks the traffic of the connected devices (LAN port or WiFi devices in access point mode). IPv4 only; IPv6 is blocked while the firewall is enabled.")) +
    fwCheckbox("fw_nohome", fwNoHome, T("Nur Internet, kein Heimnetz", "Internet only, no home network"),
      T("Sperrt Zugriffe auf private Adressen (10.x, 172.16&ndash;31.x, 192.168.x, Multicast). Erlaubt bleiben DHCP sowie DNS und Ping zum Router. Ideal f&uuml;r Smart-TV, IoT-Ger&auml;te oder G&auml;ste.",
        "Blocks access to private addresses (10.x, 172.16&ndash;31.x, 192.168.x, multicast). DHCP as well as DNS and ping to the router stay allowed. Ideal for smart TVs, IoT devices or guests.")) +
    fwCheckbox("fw_noadmin", fwNoAdmin, T("Webinterface der Bridge sperren", "Block the bridge web interface"),
      String(T("Angeschlossene Ger&auml;te k&ouml;nnen diese Seite nicht &ouml;ffnen.", "Connected devices cannot open this page.")) +
      (bridgeMode == MODE_AP_NAT ? String(" <b class='bad'>") + T("Achtung: Im Access-Point-Modus (NAT) sperrt dich das im WLAN aus; die Seite ist dann nur noch &uuml;ber die IP der Bridge im Heimnetz erreichbar.",
                                                               "Warning: in access point mode (NAT) this locks you out on the WiFi; the page is then only reachable via the bridge IP in the home network.") + "</b>" : String("")));
  if (apMode) {
    html += fwCheckbox("fw_isolate", fwIsolate, T("WLAN-Ger&auml;te voneinander trennen", "Isolate WiFi devices from each other"),
      T("Die WLAN-Ger&auml;te der Bridge k&ouml;nnen sich gegenseitig nicht erreichen.", "The bridge's WiFi devices cannot reach each other."));
  }

  html += String("<h3>") + T("Eigene Regeln", "Custom rules") + "</h3><p><small>" +
    T("Werden von oben nach unten gepr&uuml;ft, die erste passende Regel entscheidet. Ziel: IP (192.168.1.10), Netz (192.168.1.0/24) oder * f&uuml;r alle. Port leer = alle Ports, sonst z. B. 443 oder 1000-2000. Ziel leeren, um eine Regel zu l&ouml;schen. Rechts: Treffer.",
      "Checked from top to bottom, the first matching rule decides. Target: IP (192.168.1.10), network (192.168.1.0/24) or * for all. Empty port = all ports, otherwise e.g. 443 or 1000-2000. Clear the target to delete a rule. Right: hits.") + "</small></p>";
  const int rows = fwRuleCount + 3 > FW_MAX_RULES ? FW_MAX_RULES : fwRuleCount + 3;
  for (int i = 0; i < rows; ++i) {
    const bool used = i < fwRuleCount;
    const FwRule rule = used ? fwRules[i] : FwRule{1, 1, FW_ANY, 32, 0, 0, 0};
    const String n = "r" + String(i) + "_";
    html += "<div class='fwrule'><input type='checkbox' name='" + n + "on' value='1'" + (rule.enabled ? " checked" : "") + " title='" + T("aktiv", "active") + "'>"
      "<select name='" + n + "act'>" + fwOption("block", T("Sperren", "Block"), rule.block) + fwOption("allow", T("Erlauben", "Allow"), !rule.block) + "</select>"
      "<select name='" + n + "proto'>" + fwOption("any", T("Alle", "All"), rule.proto == FW_ANY) + fwOption("tcp", "TCP", rule.proto == FW_TCP) + fwOption("udp", "UDP", rule.proto == FW_UDP) + fwOption("icmp", "ICMP", rule.proto == FW_ICMP) + "</select>"
      "<input name='" + n + "dst' maxlength='18' placeholder='" + T("Ziel, z. B. 192.168.1.0/24", "Target, e.g. 192.168.1.0/24") + "' value='" + (used ? fwTargetText(rule) : String("")) + "'>"
      "<input name='" + n + "port' maxlength='11' placeholder='" + T("Port", "Port") + "' value='" + (used ? fwPortText(rule) : String("")) + "'>"
      "<span class='hits' id='hit" + String(i) + "'>" + (used ? String(fwHits[i]) : String("")) + "</span></div>";
  }
  html += String("<label>") + T("Alles andere", "Everything else") + "<select name='fw_default'>" +
    fwOption("allow", T("erlauben", "allow"), !fwDefaultBlock) + fwOption("block", T("sperren (dann DNS, Port 53, per Regel erlauben)", "block (then allow DNS, port 53, with a rule)"), fwDefaultBlock) + "</select></label>";

  if (apMode) {
    html += String("<h3>") + T("Zugelassene WLAN-Ger&auml;te", "Allowed WiFi devices") + "</h3>" +
      fwCheckbox("fw_macon", fwMacFilter, T("Nur diese Ger&auml;te zulassen (MAC-Filter)", "Only allow these devices (MAC filter)"),
        T("Eine MAC-Adresse pro Zeile, z. B. AA:BB:CC:DD:EE:FF. Andere Ger&auml;te werden sofort wieder getrennt. Trage zuerst dein eigenes Ger&auml;t ein.",
          "One MAC address per line, e.g. AA:BB:CC:DD:EE:FF. Other devices are disconnected immediately. Add your own device first.")) +
      "<textarea name='fw_macs' spellcheck='false'>";
    for (int i = 0; i < fwMacCount; ++i) html += macToString(fwMacs[i]) + "\n";
    html += String("</textarea><p><small>") + T("Gerade verbunden:", "Currently connected:") + "</small></p><div id='fwMacPick'></div>";
  }
  html += String("<p><small>") + T("Gesperrte Pakete seit dem Start: ", "Blocked packets since start: ") + "<b id='fwBlocked'>" + String(fwBlocked) + "</b></small></p>"
    "<button type='submit'>" + T("Firewall speichern", "Save firewall") + "</button></div></form>";
  return html;
}

// MAC-Adresse des anfragenden WLAN-Geraets (AP-NAT), um Selbstaussperren zu verhindern
bool requesterMac(uint8_t *mac) {
  if (bridgeMode != MODE_AP_NAT) return false;
  const uint32_t remote = static_cast<uint32_t>(webServer.client().remoteIP());
  wifi_sta_list_t list{};
  if (esp_wifi_ap_get_sta_list(&list) != ESP_OK || list.num <= 0) return false;
  esp_netif_pair_mac_ip_t pairs[ESP_WIFI_MAX_CONN_NUM] = {};
  const int count = list.num < ESP_WIFI_MAX_CONN_NUM ? list.num : ESP_WIFI_MAX_CONN_NUM;
  for (int i = 0; i < count; ++i) memcpy(pairs[i].mac, list.sta[i].mac, 6);
  if (esp_netif_dhcps_get_clients_by_mac(apNetif, count, pairs) != ESP_OK) return false;
  for (int i = 0; i < count; ++i) {
    if (pairs[i].ip.addr != 0 && pairs[i].ip.addr == remote) { memcpy(mac, pairs[i].mac, 6); return true; }
  }
  return false;
}

void saveFirewall() {
  detectLanguage();
  String errors;
  FwRule rules[FW_MAX_RULES] = {};
  int count = 0;
  for (int i = 0; i < FW_MAX_RULES; ++i) {
    const String n = "r" + String(i) + "_";
    if (!webServer.hasArg(n + "dst")) continue;
    String target = webServer.arg(n + "dst");
    target.trim();
    if (target.isEmpty()) continue;
    FwRule rule{};
    rule.enabled = webServer.arg(n + "on") == "1";
    rule.block = webServer.arg(n + "act") != "allow";
    const String proto = webServer.arg(n + "proto");
    rule.proto = proto == "tcp" ? FW_TCP : proto == "udp" ? FW_UDP : proto == "icmp" ? FW_ICMP : FW_ANY;
    const String rowLabel = String(T("Regel ", "Rule ")) + String(i + 1) + ": ";
    if (!fwParseTarget(target, rule.net, rule.prefix)) {
      errors += "<li>" + rowLabel + T("Ziel ung&uuml;ltig (z. B. 192.168.1.10, 192.168.1.0/24 oder *)", "invalid target (e.g. 192.168.1.10, 192.168.1.0/24 or *)") + "</li>";
      continue;
    }
    if (!fwParsePorts(webServer.arg(n + "port"), rule.portFrom, rule.portTo)) {
      errors += "<li>" + rowLabel + T("Port ung&uuml;ltig (1-65535, z. B. 443 oder 1000-2000)", "invalid port (1-65535, e.g. 443 or 1000-2000)") + "</li>";
      continue;
    }
    if (rule.portFrom != 0 && (rule.proto == FW_ICMP)) {
      errors += "<li>" + rowLabel + T("ICMP hat keine Ports", "ICMP has no ports") + "</li>";
      continue;
    }
    rules[count++] = rule;
  }

  uint8_t macs[FW_MAX_MACS][6] = {};
  int macCount = 0;
  String macText = webServer.arg("fw_macs");
  macText.replace("\r", "");
  int start = 0;
  while (start <= static_cast<int>(macText.length())) {
    int end = macText.indexOf('\n', start);
    if (end < 0) end = macText.length();
    String line = macText.substring(start, end);
    line.trim();
    if (!line.isEmpty()) {
      uint8_t mac[6];
      if (!fwParseMac(line, mac)) errors += "<li>" + String(T("MAC-Adresse ung&uuml;ltig: ", "invalid MAC address: ")) + htmlEscape(line) + "</li>";
      else if (macCount >= FW_MAX_MACS) errors += "<li>" + String(T("Maximal 16 MAC-Adressen", "At most 16 MAC addresses")) + "</li>";
      else memcpy(macs[macCount++], mac, 6);
    }
    start = end + 1;
  }

  const bool enabled = webServer.arg("fw_on") == "1";
  const bool macFilter = webServer.arg("fw_macon") == "1";
  if (enabled && macFilter && isApMode()) {
    if (macCount == 0) {
      errors += "<li>" + String(T("Der MAC-Filter braucht mindestens eine Adresse.", "The MAC filter needs at least one address.")) + "</li>";
    } else {
      uint8_t own[6];
      bool listed = true;
      if (requesterMac(own)) {
        listed = false;
        for (int i = 0; i < macCount; ++i) if (memcmp(macs[i], own, 6) == 0) listed = true;
      }
      if (!listed) errors += "<li>" + String(T("Dein Ger&auml;t ", "Your device ")) + macToString(own) + T(" steht nicht in der Liste &ndash; du w&uuml;rdest dich aussperren.", " is not in the list &ndash; you would lock yourself out.") + "</li>";
    }
  }

  if (!errors.isEmpty()) {
    webServer.send(400, "text/html; charset=utf-8", pageHeader(T("Fehler", "Error")) + "<h1>" + T("Firewall nicht gespeichert", "Firewall not saved") + "</h1><ul class='bad'>" + errors + "</ul><p><a href='/#fw'>" + T("Zur&uuml;ck", "Back") + "</a></p>" + pageFooter());
    return;
  }

  portENTER_CRITICAL(&fwLock);
  memcpy(fwRules, rules, sizeof(rules));
  fwRuleCount = count;
  for (int i = 0; i < FW_MAX_RULES; ++i) fwHits[i] = 0;
  memcpy(fwMacs, macs, sizeof(macs));
  fwMacCount = macCount;
  fwNoHome = webServer.arg("fw_nohome") == "1";
  fwNoAdmin = webServer.arg("fw_noadmin") == "1";
  fwIsolate = webServer.arg("fw_isolate") == "1";
  fwDefaultBlock = webServer.arg("fw_default") == "block";
  fwMacFilter = macFilter;
  fwEnabled = enabled;
  portEXIT_CRITICAL(&fwLock);
  fwSave();
  Serial.printf("Firewall gespeichert: %s, %d Regeln\n", fwEnabled ? "aktiv" : "aus", fwRuleCount);

  webServer.send(200, "text/html; charset=utf-8", pageHeader(T("Gespeichert", "Saved")) + "<h1>" + T("Firewall gespeichert", "Firewall saved") + "</h1><p>" +
    (fwEnabled ? T("Die Firewall ist aktiv. Die Regeln gelten sofort, ein Neustart ist nicht n&ouml;tig.", "The firewall is enabled. The rules apply immediately, no restart needed.")
               : T("Die Firewall ist ausgeschaltet.", "The firewall is disabled.")) +
    "</p><p><a href='/#fw'>" + T("Zur&uuml;ck zur Startseite", "Back to the start page") + "</a></p>" + pageFooter());

  // Bereits verbundene, nicht zugelassene WLAN-Geraete sofort trennen (erst nach der Antwort)
  if (fwEnabled && fwMacFilter && isApMode()) {
    wifi_sta_list_t list{};
    if (esp_wifi_ap_get_sta_list(&list) == ESP_OK) {
      for (int i = 0; i < list.num && i < ESP_WIFI_MAX_CONN_NUM; ++i) {
        uint16_t aid = 0;
        if (!fwMacListed(list.sta[i].mac) && esp_wifi_ap_get_sta_aid(list.sta[i].mac, &aid) == ESP_OK) esp_wifi_deauth_sta(aid);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Ereignisse
// ---------------------------------------------------------------------------

void onEthernetEvent(void *argument, esp_event_base_t eventBase, int32_t eventId, void *eventData) {
  if (eventId == ETHERNET_EVENT_CONNECTED) {
    esp_eth_handle_t handle = *static_cast<esp_eth_handle_t *>(eventData);
    eth_speed_t speed = ETH_SPEED_10M;
    eth_duplex_t duplex = ETH_DUPLEX_HALF;
    esp_eth_ioctl(handle, ETH_CMD_G_SPEED, &speed);
    esp_eth_ioctl(handle, ETH_CMD_G_DUPLEX_MODE, &duplex);
    ethernetSpeedMbit = (speed == ETH_SPEED_100M) ? 100 : 10;
    ethernetFullDuplex = (duplex == ETH_DUPLEX_FULL);
    ethernetLinkSinceMs = millis();
    ethernetLinkUp = true;
    if (bridgeMode == MODE_NAT) ethernetServicesPending = true;
    Serial.printf("Ethernet-Kabel verbunden (%d Mbit/s, %s)\n", ethernetSpeedMbit, ethernetFullDuplex ? "Vollduplex" : "Halbduplex");
  } else if (eventId == ETHERNET_EVENT_DISCONNECTED) {
    ethernetLinkUp = false;
    if (bridgeMode == MODE_NAT) ethernetLanReady = false;
    clearLanClients();  // im Bridge-Modus darf danach ein anderes Geraet angesteckt werden
    Serial.println("Ethernet-Kabel getrennt");
  } else if (eventId == ETHERNET_EVENT_STOP) {
    ethernetLanReady = false;
  }
}

// NAT-Modus: wird bei jeder DHCP-Vergabe ausgeloest (auch fuer das Setup-WLAN, daher der Filter).
void onIpEvent(void *argument, esp_event_base_t eventBase, int32_t eventId, void *eventData) {
  if (eventId != IP_EVENT_AP_STAIPASSIGNED) return;
  const ip_event_ap_staipassigned_t *info = static_cast<const ip_event_ap_staipassigned_t *>(eventData);
  if (ethernetNetif == nullptr || info->esp_netif != ethernetNetif) return;
  rememberLanClient(info->mac, info->ip.addr);
  Serial.printf("LAN-Geraet %s hat " IPSTR " bekommen\n", macToString(info->mac).c_str(), IP2STR(&info->ip));
}

// Bridge-Modus: WLAN-Treiber-Ereignisse. Wird NACH den Standard-Handlern von ESP-IDF aufgerufen.
void onWifiDriverEvent(void *argument, esp_event_base_t eventBase, int32_t eventId, void *eventData) {
  if (eventId == WIFI_EVENT_STA_CONNECTED) {
    // Die Bridge selbst soll im Router-Netz keine Adresse holen - das macht das LAN-Geraet.
    esp_netif_t *staNetif = WiFi.STA.netif();
    if (staNetif != nullptr) esp_netif_dhcpc_stop(staNetif);
    // Alle WLAN-Frames an die Bridge statt an den eigenen TCP/IP-Stack liefern
    esp_wifi_internal_reg_rxcb(WIFI_IF_STA, onWifiFrame);
    bridgeWifiLinked = true;
  } else if (eventId == WIFI_EVENT_STA_DISCONNECTED) {
    bridgeWifiLinked = false;
  }
}

void onNetworkEvent(WiFiEvent_t event) {
  if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED && bridgeMode == MODE_BRIDGE) {
    wifiConnected = true;
    Serial.println("WLAN verbunden (Bridge-Modus)");
  } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP && bridgeMode == MODE_NAT) {
    wifiConnected = true;
    Serial.print("Router-IP: ");
    Serial.println(WiFi.localIP());
  } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    wifiConnected = false;
  }
}

// ---------------------------------------------------------------------------
// Ethernet
// ---------------------------------------------------------------------------

bool installEthernetDriver() {
  // WT32-ETH01: GPIO16 schaltet den 50-MHz-Oszillator des LAN8720 ein.
  // Der Takt muss laufen, bevor der EMAC initialisiert wird.
  pinMode(ETH_PHY_POWER_PIN, OUTPUT);
  digitalWrite(ETH_PHY_POWER_PIN, HIGH);
  delay(50);

  eth_mac_config_t macConfig = ETH_MAC_DEFAULT_CONFIG();
  eth_phy_config_t phyConfig = ETH_PHY_DEFAULT_CONFIG();
  phyConfig.phy_addr = ETH_PHY_ADDRESS;
  phyConfig.reset_gpio_num = -1;
  esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phyConfig);
  eth_esp32_emac_config_t emacConfig = ETH_ESP32_EMAC_DEFAULT_CONFIG();
  // MDC=23 und MDIO=18 sind bereits die Standardwerte von ETH_ESP32_EMAC_DEFAULT_CONFIG().
  emacConfig.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
  emacConfig.clock_config.rmii.clock_gpio = EMAC_CLK_IN_GPIO;
  esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emacConfig, &macConfig);
  esp_eth_config_t ethConfig = ETH_DEFAULT_CONFIG(mac, phy);
  ethConfig.check_link_period_ms = 2000;

  if (phy == nullptr || mac == nullptr || esp_eth_driver_install(&ethConfig, &ethernetHandle) != ESP_OK) {
    Serial.println("LAN8720-Treiber konnte nicht gestartet werden");
    return false;
  }
  esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &onEthernetEvent, nullptr);
  esp_eth_ioctl(ethernetHandle, ETH_CMD_G_MAC_ADDR, ethMac);
  return true;
}

void startNatLan() {
  esp_netif_ip_info_t ipInfo{};
  ipInfo.ip.addr = ESP_IP4TOADDR(192, 168, 50, 1);
  ipInfo.gw.addr = ESP_IP4TOADDR(192, 168, 50, 1);
  ipInfo.netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0);
  esp_netif_inherent_config_t netifConfig = ESP_NETIF_INHERENT_DEFAULT_ETH();
  // AUTOUP ist noetig: Der DHCP-Server startet nur, wenn die Schnittstelle beim Start "up" ist.
  netifConfig.flags = (esp_netif_flags_t)(ESP_NETIF_DHCP_SERVER | ESP_NETIF_FLAG_AUTOUP);
  netifConfig.ip_info = &ipInfo;
  netifConfig.if_key = "ETH_LAN";
  netifConfig.if_desc = "ethernet-lan";
  esp_netif_config_t config{};
  config.base = &netifConfig;
  config.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;
  ethernetNetif = esp_netif_new(&config);
  if (ethernetNetif == nullptr) {
    Serial.println("Ethernet-LAN-Schnittstelle konnte nicht angelegt werden");
    return;
  }
  if (esp_netif_attach(ethernetNetif, esp_eth_new_netif_glue(ethernetHandle)) != ESP_OK) {
    Serial.println("Ethernet-LAN konnte nicht mit dem Netzwerk verbunden werden");
    return;
  }
  // Empfang ueber die Firewall leiten (ersetzt den Eingang, den esp_netif_attach gesetzt hat)
  esp_eth_update_input_path(ethernetHandle, onNatLanInput, nullptr);
  useCountingTransmit();

  // DNS fuer die LAN-Clients: erst das Angebot aktivieren, dann die Adresse setzen.
  esp_netif_dns_info_t dns{};
  dns.ip.u_addr.ip4.addr = ESP_IP4TOADDR(1, 1, 1, 1);
  dns.ip.type = ESP_IPADDR_TYPE_V4;
  dhcps_offer_t offerDns = OFFER_DNS;
  esp_netif_dhcps_option(ethernetNetif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offerDns, sizeof(offerDns));
  esp_netif_set_dns_info(ethernetNetif, ESP_NETIF_DNS_MAIN, &dns);
  esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &onIpEvent, nullptr);
  if (esp_eth_start(ethernetHandle) != ESP_OK) {
    Serial.println("Ethernet-LAN konnte nicht eingeschaltet werden");
    return;
  }
  Serial.println("NAT-Modus: Ethernet-LAN gestartet, warte auf Kabel ...");
  startLanServices();
}

// NAT-Modus: startet DHCP-Server und NAPT. Wird beim Start und bei jedem Link-Up aufgerufen.
void startLanServices() {
  ethernetServicesPending = false;
  if (ethernetNetif == nullptr) return;

  esp_err_t result = esp_netif_dhcps_start(ethernetNetif);
  if (result != ESP_OK && result != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
    Serial.printf("DHCP-Server konnte nicht gestartet werden: 0x%x\n", static_cast<unsigned>(result));
    ethernetLanReady = false;
    return;
  }
  esp_netif_dhcp_status_t status;
  if (esp_netif_dhcps_get_status(ethernetNetif, &status) == ESP_OK && status == ESP_NETIF_DHCP_STARTED) {
    if (!ethernetLanReady) Serial.println("Ethernet-LAN: 192.168.50.1, DHCP-Server laeuft");
    ethernetLanReady = true;
  }

  result = esp_netif_napt_enable(ethernetNetif);
  if (result != ESP_OK) Serial.printf("Internetfreigabe konnte nicht aktiviert werden: 0x%x\n", static_cast<unsigned>(result));
}

void startBridgeLan() {
  // Kein TCP/IP-Stack am Ethernet: alle Frames gehen direkt an onLanFrame().
  if (esp_eth_update_input_path(ethernetHandle, onLanFrame, nullptr) != ESP_OK) {
    Serial.println("Bridge: Ethernet-Empfang konnte nicht umgeleitet werden");
    return;
  }
  // Promiscuous: auch Frames an die MAC des Routers annehmen (nicht nur an die eigene).
  bool promiscuous = true;
  if (esp_eth_ioctl(ethernetHandle, ETH_CMD_S_PROMISCUOUS, &promiscuous) != ESP_OK) {
    Serial.println("Bridge: Promiscuous-Modus konnte nicht aktiviert werden");
    return;
  }
  if (esp_eth_start(ethernetHandle) != ESP_OK) {
    Serial.println("Bridge: Ethernet konnte nicht eingeschaltet werden");
    return;
  }
  ethernetLanReady = true;
  Serial.printf("Bridge-Modus: Ethernet <-> WLAN, Geraet erscheint im Router als %s\n", macToString(staMac).c_str());
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(250);
  WiFi.onEvent(onNetworkEvent);
  preferences.begin("bridge", false);

  // Notfall-Reset: 3x hintereinander Strom aus/an (jeweils < 10 s) setzt die Betriebsart auf NAT zurueck.
  uint8_t quickBoots = preferences.getUChar("boots", 0);
  quickBoots = (esp_reset_reason() == ESP_RST_POWERON) ? quickBoots + 1 : 0;
  if (quickBoots >= 3) {
    preferences.putUChar("mode", MODE_NAT);
    preferences.remove("web_hash");  // Passwortschutz des Webinterface entfernen (Passwort vergessen)
    preferences.remove("web_salt");
    quickBoots = 0;
    recoveryTriggered = true;
    Serial.println("Notfall-Reset: Betriebsart auf NAT zurueckgesetzt, Passwortschutz des Webinterface entfernt");
  }
  preferences.putUChar("boots", quickBoots);
  bootCounterClearAtMs = millis() + 10000;

  fwLoad();
  if (preferences.isKey("web_hash") && preferences.isKey("web_salt")) {
    webPassHash = preferences.getString("web_hash", "");
    webPassSalt = preferences.getString("web_salt", "");
    if (webPassHash.length() != 64) webPassHash = "";
  }
  Serial.println(webPasswordSet() ? "Webinterface: Passwortschutz aktiv" : "Webinterface: ohne Passwort");
  routerSsid = preferences.getString("ssid", "");
  routerPassword = preferences.getString("password", "");
  apSsid = preferences.getString("ap_ssid", SETUP_AP_SSID);
  for (const char *oldName : OLD_SETUP_AP_SSIDS) {
    if (apSsid == oldName) apSsid = SETUP_AP_SSID;
  }
  if (!isValidSsid(apSsid)) apSsid = SETUP_AP_SSID;
  setupApPassword = preferences.getString("ap_pass", SETUP_AP_PASSWORD);
  if (!isValidWifiPassword(setupApPassword)) setupApPassword = isValidWifiPassword(SETUP_AP_PASSWORD) ? SETUP_AP_PASSWORD : "";
  const uint8_t storedMode = preferences.getUChar("mode", MODE_NAT);
  bridgeMode = storedMode <= MODE_AP_BRIDGE ? static_cast<BridgeMode>(storedMode) : MODE_NAT;
  apOpenChosen = setupApPassword.isEmpty() && preferences.getBool("ap_openok", false);
  if (isApMode() && setupApPassword.isEmpty() && !apOpenChosen) bridgeMode = MODE_NAT;  // kein ungewollt offener Access Point
  const char *modeNames[] = {"NAT", "Bridge", "Access Point (NAT)", "Access Point (Bridge)"};
  Serial.printf("%s, Firmware %s\n", PRODUCT_NAME, FIRMWARE_VERSION);
  Serial.printf("Betriebsart: %s\n", modeNames[bridgeMode]);

  const char *apPassword = setupApPassword.isEmpty() ? nullptr : setupApPassword.c_str();
  if (isApMode()) {
    WiFi.mode(WIFI_MODE_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0), IPAddress((uint32_t)0), IPAddress(1, 1, 1, 1));
    WiFi.softAP(apSsid.c_str(), apPassword, 1, 0, 8);
    apNetif = WiFi.AP.netif();
    // Nach den Standard-Handlern registrieren: Geraeteliste, MAC-Filter, Empfangsfilter
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onApStaEvent, nullptr);
    if (bridgeMode == MODE_AP_NAT) esp_wifi_internal_reg_rxcb(WIFI_IF_AP, onApNatWifiFrame);
    if (bridgeMode == MODE_AP_BRIDGE) {
      // Die Adressen vergibt der Router: eigenen DHCP-Server des Access Points abschalten
      esp_netif_dhcps_stop(WiFi.AP.netif());
      esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onApDriverEvent, nullptr);
      esp_wifi_internal_reg_rxcb(WIFI_IF_AP, onApWifiFrame);
    }
  } else {
    WiFi.setHostname(BRIDGE_HOSTNAME);
    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.setSleep(false);  // kein WLAN-Energiesparen: geringere Latenz, stabilere Bridge
    // DNS-Server fuer die Geraete im Einrichtungs-WLAN ist die Bridge selbst (Captive Portal)
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0), IPAddress((uint32_t)0), IPAddress(192, 168, 4, 1));
    WiFi.softAP(apSsid.c_str(), apPassword);
    // Captive Portal nur im Einrichtungs-WLAN der Client-Betriebsarten. In den Access-Point-Modi ist das
    // WLAN ein normales Netz fuer Geraete, dort muss DNS ganz normal funktionieren.
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    captivePortal = dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
    if (captivePortal) Serial.println("Captive Portal aktiv: Einrichtungsseite oeffnet sich nach dem Verbinden automatisch");
    esp_wifi_get_mac(WIFI_IF_STA, staMac);
    if (bridgeMode == MODE_BRIDGE) {
      // Nach WiFi.mode() registrieren, damit dieser Handler nach denen von ESP-IDF/Arduino laeuft.
      esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onWifiDriverEvent, nullptr);
    }
  }
  if (setupApPassword.isEmpty() && apOpenChosen) {
    Serial.println("WARNUNG: WLAN der Bridge ist bewusst OFFEN (ohne Passwort, unverschluesselt).");
  } else if (setupApPassword.isEmpty()) {
    Serial.println("WARNUNG: WLAN der Bridge ist OFFEN (kein Passwort). Bitte im Webinterface ein Passwort festlegen.");
  } else if (setupApPassword == SETUP_AP_PASSWORD) {
    Serial.println("Hinweis: WLAN der Bridge nutzt noch das Standard-Passwort");
  }

  // Alle Seiten ausser Anmeldung und Sprachwahl sind bei gesetztem Passwort geschuetzt
  webServer.on("/", HTTP_GET, showHomeGuarded);
  webServer.on("/status", HTTP_GET, guarded<showStatus>);
  webServer.on("/networks", HTTP_GET, guarded<showNetworks>);
  webServer.on("/save", HTTP_POST, guarded<saveSettings>);
  webServer.on("/mode", HTTP_POST, guarded<saveMode>);
  webServer.on("/appass", HTTP_POST, guarded<saveApPassword>);
  webServer.on("/firewall", HTTP_POST, guarded<saveFirewall>);
  webServer.on("/webpass", HTTP_POST, guarded<saveWebPassword>);
  webServer.on("/login", HTTP_POST, doLogin);
  webServer.on("/logout", HTTP_POST, doLogout);
  webServer.on("/lang", HTTP_GET, setLanguage);
  static const char *collectedHeaders[] = {"Cookie", "Accept-Language"};
  webServer.collectHeaders(collectedHeaders, 2);
  webServer.onNotFound(showHomeGuarded);
  webServer.begin();

  if (installEthernetDriver()) {
    switch (bridgeMode) {
      case MODE_BRIDGE: startBridgeLan(); break;
      case MODE_AP_NAT: startApNatUplink(); break;
      case MODE_AP_BRIDGE: startApBridge(); break;
      default: startNatLan(); break;
    }
  }
  if (!isApMode()) {
    // Arduino wuerde sonst pausenlos neu verbinden; das erledigt handleRouterConnection() gedrosselt.
    WiFi.setAutoReconnect(false);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onStaDisconnectEvent, nullptr);
    routerChannel = preferences.getUChar("r_chan", 0);
    if (!preferences.isKey("r_bssid") || preferences.getBytesLength("r_bssid") != 6 || preferences.getBytes("r_bssid", routerBssid, 6) != 6) routerChannel = 0;
    connectToRouter();
    scheduleReconnect();
  }
  if (bridgeMode != MODE_AP_BRIDGE) Serial.println("Webinterface: http://192.168.4.1");
}

// Einmal pro Sekunde: Bytezaehler -> Datenrate (Bit/s), Summen und Hoechstwerte
void updateLanRates() {
  static uint32_t lastMs = 0;
  static uint32_t lastRx = 0;
  static uint32_t lastTx = 0;
  const uint32_t now = millis();
  const uint32_t elapsed = now - lastMs;
  if (elapsed < 1000) return;
  const uint32_t rx = __atomic_load_n(&lanRxBytes, __ATOMIC_RELAXED);
  const uint32_t tx = __atomic_load_n(&lanTxBytes, __ATOMIC_RELAXED);
  const uint32_t rxDelta = rx - lastRx;  // Ueberlauf nach 4 GB rechnet sich so von selbst heraus
  const uint32_t txDelta = tx - lastTx;
  if (lastMs != 0) {
    lanRxTotal += rxDelta;
    lanTxTotal += txDelta;
    lanRxRate = static_cast<uint32_t>(static_cast<uint64_t>(rxDelta) * 8000 / elapsed);
    lanTxRate = static_cast<uint32_t>(static_cast<uint64_t>(txDelta) * 8000 / elapsed);
    if (lanRxRate > lanRxPeak) lanRxPeak = lanRxRate;
    if (lanTxRate > lanTxPeak) lanTxPeak = lanTxRate;
  }
  lastMs = now;
  lastRx = rx;
  lastTx = tx;
}

void loop() {
  if (ethernetServicesPending) startLanServices();
  if (apDnsUpdatePending) updateApDns();
  if (!isApMode()) handleRouterConnection();
  // Eigene IP im Router-Netz fuer die Firewall aktuell halten (NAT-Modus)
  static uint32_t lastOwnIpUpdate = 0;
  if (bridgeMode == MODE_NAT && millis() - lastOwnIpUpdate > 2000) {
    lastOwnIpUpdate = millis();
    fwOwnIpExtra = wifiConnected ? toHostOrder(static_cast<uint32_t>(WiFi.localIP())) : 0;
  }
  if (captivePortal) dnsServer.processNextRequest();
  webServer.handleClient();
  logDiagnostics();
  updateLanRates();
  // Falls die Seite geschlossen wurde, bevor das Scan-Ergebnis abgeholt war
  if (scanPausedConnect && millis() - scanStartedMs > 20000) {
    WiFi.scanDelete();
    resumeRouterConnection();
  }
  // Nach 10 s Laufzeit zaehlt ein Neustart nicht mehr als "schnelles Aus-/Einschalten"
  if (bootCounterClearAtMs != 0 && static_cast<int32_t>(millis() - bootCounterClearAtMs) >= 0) {
    preferences.putUChar("boots", 0);
    bootCounterClearAtMs = 0;
  }
  if (restartAtMs != 0 && static_cast<int32_t>(millis() - restartAtMs) >= 0) {
    ESP.restart();
  }
}
