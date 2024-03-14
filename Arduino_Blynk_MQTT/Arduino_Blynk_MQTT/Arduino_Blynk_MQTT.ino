/*
 * Copyright (c) 2024 Blynk Technologies Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * The software is provided "as is", without any warranties or guarantees (explicit or implied).
 * This includes no assurances about being fit for any specific purpose.
 */

#include <PubSubClient.h>
#include "NetworkHelpers.h"

// These commands are executed every time
// the device (re)connects to the Blynk Cloud
void mqtt_connected()
{
  // Publish some data
  mqtt.publish("ds/terminal", "Device connected\n");
}

// Handle incoming datastream changes
void mqtt_handler(const String& topic, const String& value)
{
  if (topic == "downlink/ds/terminal") {
    String reply = String("Your command: ") + value;
    mqtt.publish("ds/terminal", reply.c_str());
  }
}

void setup()
{
  Serial.begin(115200);
  // Wait for serial monitor, up to 3 seconds
  while (!Serial && (millis() < 3000)) { delay(10); }
  delay(100);

  printDeviceInfo();
}

void loop()
{
  EVERY_N_MILLIS(1000) {
    String uptime = String(millis() / 1000);
    mqtt.publish("ds/uptime", uptime.c_str());
  }

  EVERY_N_MILLIS(15000) {
    mqtt.publish("ds/rssi", String(WiFi.RSSI()).c_str());
  }

  // Keep WiFi and MQTT connection
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  } else if (!mqtt.connected()) {
    connectMQTT();
  } else {
    mqtt.loop();
  }

  delay(10);
}
