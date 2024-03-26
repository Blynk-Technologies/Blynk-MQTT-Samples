# SPDX-FileCopyrightText: 2024 Volodymyr Shymanskyy for Blynk Technologies Inc.
# SPDX-License-Identifier: Apache-2.0
#
# The software is provided "as is", without any warranties or guarantees (explicit or implied).
# This includes no assurances about being fit for any specific purpose.

import random

LOGO = r"""
      ___  __          __
     / _ )/ /_ _____  / /__
    / _  / / // / _ \/  '_/
   /____/_/\_, /_//_/_/\_\
          /___/
"""

class Device:
    heating_on = False
    target_temp = 10    # Target temperature, can be set from 10 to 30
    current_temp = 10   # Initial current temperature

    def __init__(self, mqtt):
        self.mqtt = mqtt

    def connected(self):
        # Get latest settings from Blynk.Cloud
        self.mqtt.publish("get/ds", "Power,Set Temperature")

        # Display Blynk logo, just for fun
        self.terminal_print(LOGO)

    def terminal_print(self, *args):
        self.mqtt.publish("ds/Terminal", " ".join(map(str, args)) + "\n")

    def process_message(self, topic, payload):
        if topic == "downlink/ds/Power":
            self.heating_on = bool(int(payload))
            settemp_disabled = 0 if self.heating_on else 1
            self.mqtt.publish("ds/Set Temperature/prop/isDisabled", settemp_disabled)
        elif topic == "downlink/ds/Set Temperature":
            self.target_temp = float(payload)
        elif topic == "downlink/ds/Terminal":
            reply = f"Your command: {payload}"
            self.mqtt.publish("ds/Terminal", reply)

    def _update_temperature(self):
        target = self.target_temp if self.heating_on else 10
        next_temp = self.current_temp + (target - self.current_temp) * 0.05
        next_temp = max(10, min(next_temp, 35))
        next_temp += (0.5 - random.uniform(0, 1)) * 0.3
        self.current_temp = next_temp
        self.mqtt.publish("ds/Current Temperature", self.current_temp)

    def _update_widget_state(self):
        if not self.heating_on:
            state = 1 # OFF
        elif abs(self.current_temp - self.target_temp) < 1.0:
            state = 2 # Idle
        elif self.target_temp > self.current_temp:
            state = 3 # Heating
        elif self.target_temp < self.current_temp:
            state = 4 # Cooling

        state_colors = [None, "E4F6F7", "E6F7E4", "F7EAE4", "E4EDF7"]
        self.mqtt.publish("ds/Status", state)
        self.mqtt.publish("ds/Status/prop/color", state_colors[state])

    def update(self):
        self._update_temperature()
        self._update_widget_state()
