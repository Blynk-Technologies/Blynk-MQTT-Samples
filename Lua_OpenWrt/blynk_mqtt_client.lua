#!/usr/bin/env lua

--[[
  SPDX-FileCopyrightText: 2024 Volodymyr Shymanskyy for Blynk Technologies Inc.
  SPDX-License-Identifier: Apache-2.0

  The software is provided "as is", without any warranties or guarantees (explicit or implied).
  This includes no assurances about being fit for any specific purpose.

  This example uses Lua bindings to libmosquitto:
  https://flukso.github.io/lua-mosquitto/docs
]]

local mqtt = require("mosquitto")
local client = mqtt.new()
local auth_token = arg[1]

function delay(secs)
    local start = os.time()
    while (os.time() - start < secs) do
        pcall(client.loop, client)
    end
end

client.ON_CONNECT = function(ok, retcode, msg)
    if ok then
        print("Connected (secure)")
        client:subscribe("downlink/#")
    elseif retcode == 4 then
        print("Invalid auth token")
        os.exit(1)
    else
        print(retcode, msg)
        --client.ON_DISCONNECT = nil
        os.exit(1)
    end
end

client.ON_DISCONNECT = function(ok, retcode, msg)
    print("Connecting...")
    client:reconnect()
end

client.ON_MESSAGE = function(mid, topic, payload, qos, retain)
    print("Got", topic, payload)
    if topic == "downlink/ds/terminal" then
        client:publish("ds/terminal", "Your command: " .. payload)
    end
end

--client.ON_LOG = print

client:login_set("device", auth_token)
-- ISRG Root X1, expires: Mon, 04 Jun 2035 11:04:38 GMT
client:tls_set("ISRG_Root_X1.crt")
client:connect_async("blynk.cloud", 8883, 45)

start_time = os.time()
while true do
    uptime = os.time() - start_time
    client:publish("ds/uptime", tostring(uptime))
    delay(1)
end
