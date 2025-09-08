# WiFi Deauther v2.2

A multi-functional WiFi security testing and auditing tool built for the ESP8266 microcontroller. It features an OLED display for standalone use and a full web interface for remote control, capable of performing various WiFi penetration tests.

![Block Diagram](https://img.shields.io/badge/ESP8266-WiFi_Deauther-orange?style=for-the-badge) ![Version](https://img.shields.io/badge/Version-2.2-blue?style=for-the-badge) ![License](https://img.shields.io/badge/License-EDUCATIONAL-red?style=for-the-badge)

## ⚠️ Disclaimer & Legal Notice

**This project is intended for EDUCATIONAL and ETHICAL TESTING purposes only.**

-   **Unauthorized use on networks you do not own or have explicit permission to test is ILLEGAL.**
-   It is the end user's responsibility to know and comply with all local, state, and federal laws.
-   The developers and contributors are **NOT RESPONSIBLE** for any misuse or damage caused by this software.
-   Use this tool strictly on your own networks or in environments where you have obtained prior written permission.

## 🚀 Features

-   **📶 Network Scanner**: Discovers nearby WiFi Access Points.
-   **🔫 Deauthentication Attack**: Disconnects clients from selected target networks.
-   **👥 Evil Twin Attack**: Creates a rogue AP with a captive portal to capture WPA passwords.
-   **📢 Beacon Spam**: Floods the area with fake AP advertisements to clutter network lists.
-   **🔍 Probe Request Flood**: Spams probe requests to disrupt WiFi discovery.
-   **🖥️ Web Interface**: Full control via a modern web UI hosted on the device (`192.168.4.1`).
-   **📟 OLED Interface**: Standalone control via a 128x64 OLED and two buttons.
-   **⏰ On-board Clock**: Features a settable time display.

## 🛠️ Hardware Requirements

| Component | Quantity | Notes |
| :--- | :--- | :--- |
| ESP8266 Board (NodeMCU, Wemos D1 Mini) | 1 | |
| SSD1306 I2C OLED Display (128x64) | 1 | |
| Tactile Push Buttons | 2 | |
| Breadboard & Jumper Wires | - | |
| Micro-USB Cable | 1 | For power and programming |

## 🔌 Pinout Configuration

| ESP8266 Pin | Label | Connected To |
| :--- | :--- | :--- |
| GPIO4 | D2 | OLED SDA |
| GPIO5 | D1 | OLED SCL |
| GPIO14 | D5 | Button (UP) |
| GPIO13 | D7 | Button (OK) |
| GPIO2 | D4 | Built-in LED (Status) |

## 📦 Library Dependencies

Ensure you have the following libraries installed in your Arduino IDE:
*   `ESP8266WiFi` (by ESP8266 Community)
*   `ESP8266WebServer` (by ESP8266 Community)
*   `SSD1306Wire` (via **ESP8266 and ESP32 Oled Driver for SSD1306 display** by Daniel Eichhorn, Fabian Weinberger)
*   `TimeLib` (by Paul Stoffregen)
*   `DNSServer` (by ESP8266 Community)

## ⚙️ Installation & Flashing

1.  **Install Arduino IDE**: Download and install from [arduino.cc](https://www.arduino.cc/en/software).
2.  **Add ESP8266 Board Support**:
    *   Go to `File > Preferences` and add `http://arduino.esp8266.com/stable/package_esp8266com_index.json` to the *Additional Boards Manager URLs*.
    *   Go to `Tools > Board > Boards Manager...`, search for "esp8266", and install it.
3.  **Install Libraries**: Use the Arduino Library Manager (`Sketch > Include Library > Manage Libraries...`) to search for and install the libraries listed above.
4.  **Select Board and Port**: Under `Tools > Board`, select your ESP8266 board (e.g., "NodeMCU 1.0 (ESP-12E Module)"). Select the correct COM port under `Tools > Port`.
5.  **Upload Code**: Open the `esp8266.txt` file (renamed to `esp8266.ino`), connect your ESP8266, and click the **Upload** button.

## 🎮 Usage

### OLED & Button Control
1.  Power on the device. It will create a WiFi AP named `Deauther` (password: `deauther`).
2.  Use the **UP** and **OK** buttons to navigate the menus on the OLED screen.
3.  The main menu allows you to scan for networks, access the attack menu, or set the time.

### Web Interface Control
1.  Connect your computer or smartphone to the `Deauther` WiFi network (password: `deauther`).
2.  Open a web browser and navigate to **http://192.168.4.1**.
3.  You will see a dashboard with status information and quick access to all features:
    *   **Dashboard**: Overview and attack status.
    *   **Scan**: Scan for networks and view results.
    *   **Deauth**: Select targets and start a deauthentication attack.
    *   **Evil Twin**: Set up a rogue access point to capture passwords.
    *   **Beacon/Probe**: Configure and start beacon spam or probe flood attacks.

## 🧪 Attack Types Explained

-   **Deauth Attack**: Sends IEEE 802.11 deauthentication frames to forcibly disconnect all clients from the targeted access point. This is a disruption attack.
-   **Evil Twin**: Creates a perfect replica (same SSID/MAC) of a target network. When users connect, a professional-looking captive portal prompts them for the password, which is then captured and displayed. This is a credential harvesting attack.
-   **Beacon Spam**: Broadcasts fake beacon frames advertising common network names (e.g., "Starbucks_WiFi", "Airport_Free_WiFi") to clutter and confuse nearby devices' network lists.
-   **Probe Flood**: Spams probe request frames, which can disrupt normal WiFi operation and can be used to elicit responses from hidden networks.

## 🔧 Configuration

Key settings can be modified by changing constants in the code:
```cpp
#define MAX_APS 50          // Max networks to store in scan results
#define MAX_SSIDS 20        // Max number of pre-configured beacon SSIDs
#define CHANNEL_HOP_INTERVAL 200 // Ms between channel hops during attacks
#define SCAN_INTERVAL 30000 // Ms between auto-scans
String evilTwinPassword = "password123"; // Default password for Evil Twin portal