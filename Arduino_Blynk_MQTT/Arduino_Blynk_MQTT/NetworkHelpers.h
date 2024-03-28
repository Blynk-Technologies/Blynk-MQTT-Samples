/*
 * SPDX-FileCopyrightText: 2024 Volodymyr Shymanskyy for Blynk Technologies Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * The software is provided "as is", without any warranties or guarantees (explicit or implied).
 * This includes no assurances about being fit for any specific purpose.
 */

#include <Arduino.h>
#include "arduino_secrets.h"
#include "Certificates.h"

/*
 * Settings: vadlidation and default values
 */

#if !defined(BLYNK_MQTT_BROKER)
  #define BLYNK_MQTT_BROKER       "blynk.cloud"
#endif

#if !defined(BLYNK_MQTT_PORT)
  #if defined(BLYNK_MQTT_UNSECURE)
    #define BLYNK_MQTT_PORT       1883
  #else
    #define BLYNK_MQTT_PORT       8883
  #endif
#endif

#if !defined(BLYNK_FIRMWARE_VERSION)
  #define BLYNK_FIRMWARE_VERSION  "0.0.1"
#endif

#if !defined(BLYNK_FIRMWARE_TYPE)
  #define BLYNK_FIRMWARE_TYPE     BLYNK_TEMPLATE_ID
#endif

#if !defined(BLYNK_FIRMWARE_BUILD)
  #define BLYNK_FIRMWARE_BUILD    __DATE__ " " __TIME__
#endif

#if !defined(BLYNK_AUTH_TOKEN)
  #error "This example uses static token, please define BLYNK_AUTH_TOKEN"
#endif

#if !defined(BLYNK_TEMPLATE_ID) || !defined(BLYNK_TEMPLATE_NAME)
  #error "Please define BLYNK_TEMPLATE_ID and BLYNK_TEMPLATE_NAME"
#endif

#if !defined(WIFI_SSID)
  #error "Please define WIFI_SSID"
#endif

#if !defined(WIFI_PASS)
  #define WIFI_PASS               ""
#endif

/*
 * Validate boards
 */

#if defined(PLATFORMIO)
  #if defined(ARDUINO_NANO_RP2040_CONNECT)
    #error "Please use official Arduino IDE for this board"
  #endif
#endif

/*
 * Connectivity libraries for each board type
 */

#if defined(ESP32)
  #include <WiFi.h>
  #include <WiFiClientSecure.h>
  #define USE_NODELAY
  #define NO_WIFI_SLEEP
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <WiFiClientSecure.h>
  #define USE_NODELAY
  #define NO_WIFI_SLEEP
#elif defined(ARDUINO_RASPBERRY_PI_PICO_W)
  #include <WiFi.h>
  #include <WiFiClientSecure.h>
  #define USE_NODELAY
#elif defined(WIO_TERMINAL)
  #include <rpcWiFi.h>
  #include <WiFiClientSecure.h>
  #define USE_NODELAY
  #define NO_WIFI_SLEEP
#elif defined(ARDUINO_UNOR4_WIFI)
  #include <SPI.h>
  #include <WiFiS3.h>
  #include <WiFiSSLClient.h>
  #define WiFiClientSecure WiFiSSLClient
  #define USE_ARDUINO_WIFI_MODULE
#elif defined(ARDUINO_PORTENTA_C33)
  #include <SPI.h>
  #include <WiFiC3.h>
  #include <WiFiSSLClient.h>
  #define WiFiClientSecure WiFiSSLClient
  #define USE_ARDUINO_WIFI_MODULE
#elif defined(ARDUINO_NANO_RP2040_CONNECT)   || \
      defined(ARDUINO_SAMD_NANO_33_IOT)      || \
      defined(ARDUINO_SAMD_MKRWIFI1010)
  #include <SPI.h>
  #include <WiFiNINA.h>
  #include <WiFiSSLClient.h>
  #define WiFiClientSecure WiFiSSLClient
  #define USE_ARDUINO_WIFI_MODULE
#elif defined(ARDUINO_OPTA)
  #include <WiFi.h>
  #include <WiFiSSLClient.h>
  #define WiFiClientSecure WiFiSSLClient
  #define USE_ARDUINO_WIFI_MODULE
#else
  #error "Please define the connectivity method"
#endif

/*
 * Utilities
 */

// Helper macro for running actions periodically
#define EVERY_N_MILLIS(interval)                \
        for (static uint32_t lastTime = 0;      \
             millis() - lastTime >= (interval); \
             lastTime += (interval))

static
bool parseURL(String url, String& protocol, String& host, int& port, String& path)
{
  int index = url.indexOf("://");
  if(index < 0) {
    return false;
  }

  protocol = url.substring(0, index);
  url.remove(0, (index + 3)); // remove protocol part

  index = url.indexOf('/');
  const String server = url.substring(0, index);
  url.remove(0, index);       // remove server part

  index = server.indexOf(':');
  if(index >= 0) {
    host = server.substring(0, index);          // hostname
    port = server.substring(index + 1).toInt(); // port
  } else {
    host = server;
    if (protocol == "http") {
      port = 80;
    } else if (protocol == "https") {
      port = 443;
    }
  }

  if (!host.length()) {
    return false;
  }

  if (url.length()) {
    path = url;
  } else {
    path = "/";
  }
  return true;
}

static const char* BLYNK_BANNER PROGMEM = R"(
      ___  __          __
     / _ )/ /_ _____  / /__
    / _  / / // / _ \/  '_/
   /____/_/\_, /_//_/_/\_\
          /___/
)";

static
void systemShowDeviceInfo()
{
  Serial.println(BLYNK_BANNER);
  Serial.print(" Firmware ver:    ");   Serial.println(BLYNK_FIRMWARE_VERSION);
  Serial.print(" Build time:      ");   Serial.println(BLYNK_FIRMWARE_BUILD);
#if defined(ARDUINO_VARIANT)
  Serial.print(" Board:           ");   Serial.println(ARDUINO_VARIANT);
#endif
#if defined(WIO_TERMINAL)
  Serial.print(" RTL8720 fw ver:  ");
  char* version = rpc_system_version();
  Serial.println(version);
  erpc_free(version);
#elif defined(USE_ARDUINO_WIFI_MODULE)
  while (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    delay(1000);
  }
  Serial.print(" WiFi fw ver:     ");
  Serial.println(WiFi.firmwareVersion());
#endif
  Serial.println();
}

static inline
void systemReboot() {
  delay(50);
#if defined(ESP32) || defined(ESP8266)
  ESP.restart();
#elif defined(ARDUINO_ARCH_SAMD)    || \
      defined(ARDUINO_ARCH_SAM)     || \
      defined(ARDUINO_ARCH_RENESAS) || \
      defined(ARDUINO_ARCH_MBED)    || \
      defined(ARDUINO_ARCH_NRF5)    || \
      defined(ARDUINO_ARCH_AMEBAD)
  NVIC_SystemReset();
#elif defined(ARDUINO_ARCH_RP2040)
  rp2040.reboot();
#elif defined(PARTICLE)
  System.reset();
#else
  #error "Platform not supported"
#endif
  while(1) {};
}

/*
 * MQTT
 */

char                broker_host[64] = BLYNK_MQTT_BROKER;
uint16_t            broker_port     = BLYNK_MQTT_PORT;

#if defined(BLYNK_MQTT_UNSECURE)
  WiFiClient        client;
  PubSubClient      mqtt(client);
#else
  WiFiClientSecure  client;
  PubSubClient      mqtt(client);
#endif

// Forward declarations
void mqtt_connected();
void mqtt_handler(const String& topic, const String& value);

void mqtt_handler_wrapper(char* topic, byte* payload, unsigned length)
{
  const String t(topic);
#if defined(ESP8266) || defined(WIO_TERMINAL)
  payload[length] = '\0';
  const String v((char*)payload);
#else
  const String v(payload, length);
#endif

  if (t == "downlink/redirect") {
    String protocol, host, path;
    int port = 0;
    if (parseURL(v, protocol, host, port, path)) {
      host.toCharArray(broker_host, sizeof(broker_host));
      if (port > 0 && port <= 0xFFFF) {
        broker_port = port;
      }
      mqtt.disconnect();  // trigger reconnect in the main loop
    } else {
      Serial.println("Cannot parse URL");
    }
  } else if (t == "downlink/reboot") {
    Serial.println("Rebooting...");
    systemReboot();
  } else if (t == "downlink/ping") {
    /* MQTT client library will automagically send QOS1 response */
  } else if (t == "downlink/diag") {
    Serial.print("Server says: ");
    Serial.println(v);
  } else {
    mqtt_handler(t, v);
  }
}

void connectMQTT()
{
  mqtt.setServer(broker_host, broker_port);
  mqtt.setCallback(mqtt_handler_wrapper);

  #if defined(BLYNK_MQTT_UNSECURE)
    // For unsecure connections, no certificates needed
  #elif defined(ESP32) || defined(WIO_TERMINAL) || defined(ARDUINO_RASPBERRY_PI_PICO_W)
    client.setCACert(CA_CERT);
  #elif defined(ESP8266)
    static BearSSL::X509List x509(CA_CERT);
    client.setTrustAnchors(&x509);
  #elif defined(ARDUINO_OPTA)
    client.appendCustomCACert(CA_CERT);
  #elif defined(USE_ARDUINO_WIFI_MODULE)
    // Certificates are handled by the connectivity module internally
  #else
    #error "TLS conenction certificate is not configured"
  #endif

  Serial.print("Connecting to ");
  Serial.print(broker_host);
  Serial.print(":");
  Serial.print(broker_port);
  Serial.print("... ");

  if (mqtt.connect("", "device", BLYNK_AUTH_TOKEN)) {
    #if defined(BLYNK_MQTT_UNSECURE)
    Serial.println("OK");
    Serial.println("Warning! insecure connection is used");
    #else
    Serial.println("OK [secure]");
    #endif

    // Improve device responsiveness. Please NOTE:
    // 1. Disabling WiFi sleep increases power consumption
    // 2. Enabling TCP_NODELAY could increase network traffic
    //    and possibly reduces network efficiency for bulk data transfers
    #if defined(NO_WIFI_SLEEP)
    WiFi.setSleep(false);
    #endif
    #if defined(USE_NODELAY)
    client.setNoDelay(true);
    #endif

    // Subscribe to all downlink messages
    mqtt.subscribe("downlink/#");

    // Send device info
    char info[128];
    snprintf(info, sizeof(info),
              R"json({"type":"%s","ver":"%s","build":"%s","tmpl":"%s","rxbuff":%d})json",
              BLYNK_FIRMWARE_TYPE,
              BLYNK_FIRMWARE_VERSION,
              BLYNK_FIRMWARE_BUILD,
              BLYNK_TEMPLATE_ID,
              MQTT_MAX_PACKET_SIZE);
    mqtt.publish("info/mcu", info);

    mqtt_connected();

  } else {
    Serial.println("FAILED. Check internet connectivity and Blynk credentials.");
    delay(1000);
  }
}

/*
 * WiFi
 */

#if defined(ESP8266)
bool setClock()
{
  const time_t Jan2024 = 1704067200L;
  if (time(nullptr) >= Jan2024) { return true; }

  Serial.print("NTP time sync...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  const uint32_t started = millis();
  while ((time(nullptr) < Jan2024) && (millis() - started < 20000)) {
    delay(500);
    Serial.print(".");
  }
  time_t now = time(nullptr);
  if (now >= Jan2024) {
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    Serial.print(" ");
    Serial.print(asctime(&timeinfo));
    return true;
  } else {
    Serial.println(" FAIL. Check internet connectivity.");
    return false;
  }
}
#endif

void connectWiFi()
{
  Serial.print("Connecting to " WIFI_SSID "...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 10000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(" IP:");
    Serial.print(WiFi.localIP());
    Serial.print(", RSSI:");
    Serial.print(WiFi.RSSI());
    Serial.println("dBm");
#if defined(ESP8266)
    setClock();
#endif
  } else {
    Serial.println(" FAILED. Check your WiFi credentials.");
    delay(1000);
  }
}

