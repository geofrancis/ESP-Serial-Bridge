/*********************************************************************************
 * ESP-Serial-Bridge
 *
 * Simple WiFi Serial Bridge for Espressif microcontrollers
 *
 * Forked from https://github.com/AlphaLima/ESP32-Serial-Bridge
 *
 * Added compatibility for ESP8266, WiFi reconnect on failure, and mDNS
 *discovery.
 *
 * Note: ESP8266 is limited to 115200 baud and may be somewhat unreliable in
 *       this application.
 *
 *   -- Yuri - Aug 2021
 *
 * Fixed compatibility with Arduino framework 2.0 -- Yuri - Apr 2023
 * Implemented UDP + bug fixes                    -- Yuri - Apr 2023
 *
 * Disclaimer: Don't use for life support systems or any other situation
 * where system failure may affect user or environmental safety.
 *********************************************************************************/

#include <WireGuard-ESP32.h>

#include <Arduino.h>

#ifdef ESP32
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_wifi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <SoftwareSerial.h>  // use SoftwareSerial for ESP8266
#endif

#include "config.h"

#ifdef OTA_HANDLER
#include <ArduinoOTA.h>
#endif

#ifdef ESP32

#ifdef PROTOCOL_UDP
#include <AsyncUDP.h>
AsyncUDP udp;
uint16_t udp_port[NUM_COM] = {SERIAL0_UDP_PORT, SERIAL1_UDP_PORT,
                              SERIAL2_UDP_PORT};
#endif
#include <WireGuard-ESP32.h>

IPAddress local_ip(10, 0, 1, 3);                                      // IP address of the local interface
const char private_key[] = "COrTA6sjSk70qEJ1m7Pp2PNUeRwe8gqJG13YVAv0P28=";   // Private key of the local interface
const char endpoint_address[] = "xxxxxx.com";                           // Address of the endpoint peer
const char public_key[] = "Al/RIyqRr3YfScMXc19f567567k3hRPo6BJsgSGyA=";    // Public key of the endpoint peer
const char preshared_key[] = "RCcy9PT/5zq5675675675679gHv1piPmKW5CvpVo="; // Pre-Shared Key
uint16_t endpoint_port = 51820;                                              // Port of the endpoint peer

// WireGuard class instance
static WireGuard wg;
#ifdef BLUETOOTH  // inside the ESP32 condition, since ESP8266 doesn't have BT
#include <BluetoothSerial.h>
BluetoothSerial SerialBT;
#endif

HardwareSerial Serial_one(1);
HardwareSerial Serial_two(2);
HardwareSerial* COM[NUM_COM] = {&Serial, &Serial_one, &Serial_two};
#elif defined(ESP8266)
SoftwareSerial Serial_zero(SERIAL0_RXPIN, SERIAL0_TXPIN);
SoftwareSerial Serial_one(SERIAL1_RXPIN, SERIAL1_TXPIN);
SoftwareSerial* COM[NUM_COM] = {&Serial_zero, &Serial_one};
#endif

#ifdef PROTOCOL_TCP
#include <WiFiClient.h>
WiFiServer server_0(SERIAL0_TCP_PORT);
WiFiServer server_1(SERIAL1_TCP_PORT);

#ifdef ESP32
WiFiServer server_2(SERIAL2_TCP_PORT);
WiFiServer* server[NUM_COM] = {&server_0, &server_1, &server_2};
#elif defined(ESP8266)
WiFiServer* server[NUM_COM] = {&server_0, &server_1};
#endif
WiFiClient TCPClient[NUM_COM][MAX_NMEA_CLIENTS];
#endif

uint8_t buf1[NUM_COM][BUFFERSIZE];
uint8_t buf2[NUM_COM][BUFFERSIZE];

#ifdef ESP32
uint16_t i1[NUM_COM] = {0, 0, 0};
uint16_t i2[NUM_COM] = {0, 0, 0};
#elif defined(ESP8266)
uint16_t i1[NUM_COM] = {0, 0};
uint16_t i2[NUM_COM] = {0, 0};
#endif

uint8_t BTbuf[BUFFERSIZE];
uint16_t iBT = 0;

#ifdef MODE_STA
#if defined(ESP32)
void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
#endif


#if defined(ESP8266)
    WiFiEventHandler stationDisconnectedHandler;
    void WiFiStationDisconnected(
        const WiFiEventSoftAPModeStationDisconnected& evt) {
#endif
        debug.print("WiFi disconnected: ");
#ifdef ESP32
        debug.println(info.wifi_sta_disconnected.reason);
#endif
        debug.println("Trying to reconnect..");
        WiFi.begin(SSID, PASSWD);
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            debug.print(".");
        }
        debug.println("connected");
        debug.print("IP address: ");
        debug.println(WiFi.localIP());
    }
#endif

    void setup() {
        delay(500);

#ifdef ESP32
        COM[0]->begin(UART_BAUD0, SERIAL_PARAM0, SERIAL0_RXPIN, SERIAL0_TXPIN);
        COM[1]->begin(UART_BAUD1, SERIAL_PARAM1, SERIAL1_RXPIN, SERIAL1_TXPIN);
        COM[2]->begin(UART_BAUD2, SERIAL_PARAM2, SERIAL2_RXPIN, SERIAL2_TXPIN);
#elif defined(ESP8266)
    COM[0]->begin(UART_BAUD0);
    COM[1]->begin(UART_BAUD1);
    debug.begin(UART_BAUD0);
#endif

        debug.print("\n\nWiFi serial bridge ");
        debug.println(VERSION);

#ifdef MODE_AP
        debug.println("Open ESP Access Point Mode");
        WiFi.mode(WIFI_AP);
        WiFi.softAP(SSID, PASSWD);  // configure SSID and password for softAP
        delay(2000);            // VERY IMPORTANT
        WiFi.softAPConfig(STATIC_IP, STATIC_IP,
                          NETMASK);  // configure ip address for softAP
#endif

#ifdef MODE_STA
        debug.println("Open ESP Station Mode");
        WiFi.mode(WIFI_STA);
#ifdef ESP32
        WiFi.onEvent(WiFiStationDisconnected,
                     ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
#elif defined(ESP8266)
    stationDisconnectedHandler =
        WiFi.onSoftAPModeStationDisconnected(&WiFiStationDisconnected);
#endif
        WiFi.begin(SSID, PASSWD);
        debug.print("Connecting to: ");
        debug.print(SSID);
        debug.print("..");
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            debug.print(".");
        }
        debug.println("connected");
        debug.print("IP address: ");
        debug.println(WiFi.localIP());

        if (!MDNS.begin(HOSTNAME)) {
            debug.println("Error starting mDNS");
        } else {
            debug.print("Started mDNS, discoverable as: ");
            debug.println(HOSTNAME);
            MDNS.addService("_telnet", "_tcp", SERIAL0_TCP_PORT);
        }
#endif


#ifdef OTA_HANDLER
        ArduinoOTA.onStart([]() {
            String type;
            if (ArduinoOTA.getCommand() == U_FLASH)
                type = "sketch";
            else  // U_SPIFFS
                type = "filesystem";

            // NOTE: if updating SPIFFS this would be the place to unmount
            // SPIFFS using SPIFFS.end()
            Serial.println("Start updating " + type);
        });
        ArduinoOTA.onEnd([]() { Serial.println("\nEnd"); });
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
        });
        ArduinoOTA.onError([](ota_error_t error) {
            Serial.printf("Error[%u]: ", error);
            if (error == OTA_AUTH_ERROR)
                Serial.println("Auth Failed");
            else if (error == OTA_BEGIN_ERROR)
                Serial.println("Begin Failed");
            else if (error == OTA_CONNECT_ERROR)
                Serial.println("Connect Failed");
            else if (error == OTA_RECEIVE_ERROR)
                Serial.println("Receive Failed");
            else if (error == OTA_END_ERROR)
                Serial.println("End Failed");
        });
        // if DNSServer is started with "*" for domain name, it will reply with
        // provided IP to all DNS request

        ArduinoOTA.begin();
#endif

#ifdef PROTOCOL_TCP
        for (int num = 0; num < NUM_COM; num++) {
            debug.print("Starting TCP Server ");
            debug.println(num + 1);
            server[num]->begin();  // start TCP server
            server[num]->setNoDelay(true);
        }
#endif

#ifdef ESP32
#ifdef PROTOCOL_UDP
        bool listening = false;
        for (uint16_t num = 0; num < NUM_COM; num++) {
            if (udp.listen(udp_port[num])) {
                listening = true;  // at least one port is opened
                debug.printf("Listening on UDP port %d\n", udp_port[num]);
            }
        }
        if (listening) {
            udp.onPacket([](AsyncUDPPacket packet) {
                for (uint16_t num = 0; num < NUM_COM; num++) {
                    if (packet.remotePort() == udp_port[num]) {
                        COM[num]->write(packet.data(), packet.length());
                        break;
                    }
                }
            });
        }
#endif
#ifdef BLUETOOTH
        debug.printf("Bluetooth discoverable at %s\n", SSID);
        SerialBT.begin(SSID);  // Bluetooth device name
#endif
#endif

#ifdef BATTERY_SAVER
#ifdef ESP32
        esp_err_t esp_wifi_set_max_tx_power(50);  // lower WiFi Power
#elif defined(ESP8266)
    WiFi.setOutputPower(15);
#endif
#endif

 // Waiting until we connect to WiFi
  while (!WiFi.isConnected()) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  
  // Print the IP address in the local network
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());
  
  // Print the IP address in the WireGuard network
  Serial.print("WireGuard IP: ");
  Serial.println(local_ip.toString());

  // Synchronize system time via NTP
  configTime(9 * 60 * 60, 0, "time.google.com", "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");

  // Starting the WireGuard interface
  Serial.println("Initializing WG interface...");
  if (wg.begin(local_ip, private_key, endpoint_address, public_key, endpoint_port, preshared_key)) {
    Serial.println("OK");
  } else {
    Serial.println("FAIL");
  }
}
    

    void loop() {
      
#ifdef OTA_HANDLER
        ArduinoOTA.handle();
#endif

#ifdef ESP32
#ifdef BLUETOOTH
        if (SerialBT.hasClient()) {
            while (SerialBT.available()) {
                BTbuf[iBT] = SerialBT.read();
                iBT++;
                if (iBT == BUFFERSIZE - 1) break;
            }
            COM[BLUETOOTH]->write(BTbuf, iBT);
            iBT = 0;
        }
#endif
#endif

#ifdef PROTOCOL_TCP
        for (int num = 0; num < NUM_COM; num++) {
            if (server[num]->hasClient()) {
                for (byte i = 0; i < MAX_NMEA_CLIENTS; i++) {
                    // find free/disconnected spot
                    if (!TCPClient[num][i] || !TCPClient[num][i].connected()) {
                        if (TCPClient[num][i]) TCPClient[num][i].stop();
                        TCPClient[num][i] = server[num]->available();
                        debug.print("New client for COM");
                        debug.print(num);
                        debug.print(" #");
                        debug.println(i);
                        continue;
                    }
                }
                // no free/disconnected spot so reject
                WiFiClient TmpserverClient = server[num]->available();
                TmpserverClient.stop();
            }
        }
#endif

        for (int num = 0; num < NUM_COM; num++) {
            if (COM[num] != NULL) {
#ifdef PROTOCOL_TCP
                for (byte cln = 0; cln < MAX_NMEA_CLIENTS; cln++) {
                    if (TCPClient[num][cln]) {
                        while (TCPClient[num][cln].available()) {
                            buf1[num][i1[num]] =
                                TCPClient[num][cln]
                                    .read();  // read char from client
                            i1[num]++;
                            if (i1[num] == BUFFERSIZE - 1) break;
                        }

                        COM[num]->write(buf1[num],
                                        i1[num]);  // now send to UART(num):
                        i1[num] = 0;
                    }
                }
#endif

                if (COM[num]->available()) {
                    while (COM[num]->available()) {
                        buf2[num][i2[num]] =
                            COM[num]->read();  // read char from UART(num)
                        i2[num]++;
                        if (i2[num] == BUFFERSIZE - 1) break;
                    }

#ifdef PROTOCOL_TCP
                    for (byte cln = 0; cln < MAX_NMEA_CLIENTS; cln++) {
                        if (TCPClient[num][cln])
                            TCPClient[num][cln].write(buf2[num], i2[num]);
                    }
#endif

#ifdef ESP32
#ifdef PROTOCOL_UDP
                    udp.broadcastTo(buf2[num], i2[num], udp_port[num]);
#endif
#ifdef BLUETOOTH
                    // now send to Bluetooth:
                    if (num == BLUETOOTH && SerialBT.hasClient())
                        SerialBT.write(buf2[num], i2[num]);
#endif
#endif
                    i2[num] = 0;
                }
            }
        }
   
    }
