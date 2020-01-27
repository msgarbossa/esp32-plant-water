# ESP32 Plant watering system

## Description

DIY plant watering system.

- NTP to set the clock and water according to a daily schedule
- Set start and end watering hours
- Set watering frequency and watering duration
- Subscribes to MQTT topic and can be triggered remotely
- Includes DH11 temperature and humidity sensor w/ results sent by publishing to MQTT topic
- Integration with Home Assistant via MQTT

## Hardware

I haven't gotten around to putting this in a case yet.  I recently replaced an Arduino with a clock module, which is no longer needed because of NTP and WiFi with the ESP32.
![esp32](/img/esp32-plant-water-1.jpg)

The pump was one of the key components.  This one is brushless, IP68 waterproof, runs off of 9V and is submersible ([link](https://www.amazon.com/Winkeyes-Submersible-Fountain-Electric-Brushless/dp/B07S8S8JR9)).  It's important to get a tight fitting to the tubing.  I wanted one that I could connect standard drip irrigation parts to.
![plumbing](/img/esp32-plant-water-2.jpg)

The pump forms a loop with excess water getting returned to the reservoir.  The drips are pressure compensating ([link](https://www.amazon.com/Raindrip-PC2050B-Pressure-Compensating-Drippers/dp/B0044FUQ1A)), which is important to minimize the relatively higher pressure from the first drip in the loop compared to the later drips in the loop.  For plants requiring more water, I used 1 gph drips and 1/2 gph drips for less water.  
![pump](/img/esp32-plant-water-3.jpg)

Although not shown, the power supply requires 2 different voltages.  Currently there are 2 power bricks, one USB for the ESP32 and one with 12V for the pump connected through the relay.  I might try out a buck converter in parallel with the  pump to step down to 5V to feed the ESP32 and relay, but I've heard the buck converter generates more heat when stepping down more than a few volts.

## Platform IO

- Library dependencies are listed under lib_deps.
- For OTA updates, it's a good idea to set a DHCP reservation for the MAC address of the ESP32 so the IP address doesn't change and it can be hardcoded in plaformio.ini.
- The default upload port is 3232.
- The host_port is set to 3232.  The ESP32 initiates a connection (may require TCP and UDP to be open if using a firewall)
- To switch the default settings back to serial, uncomment the upload port pointing to /dev/tty* and comment the subsequent lines.

platformio.ini:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps=
  Adafruit Unified Sensor
  DHT sensor library
  ArduinoJson
monitor_speed = 115200
monitor_rts = 0
monitor_dtr = 0
; upload_port = /dev/ttyUSB0
upload_protocol = espota
upload_port = <ESP_32_IP>
upload_flags =
    --host_port=3232
    --port=3232
```

## Home Assistant:

### binary switch

In configuration.yaml, add the following to the "switch" list to trigger the relay to water the plants (can have multiple items in the list):

```yaml
switch:
  - platform: mqtt
    name: Water plants
    command_topic: "home/plants"
    state_topic: "home/plants/state"
    availability_topic: "home/plants/state"
    payload_available: "online"
    payload_on: "toggle"
    payload_not_available: "offline"

```

### Temperature and humidity sensor

Make sure configuration.yaml has the following at the end of the configuration to include sensors.yaml:

```yaml
sensor: !include sensors.yaml
```

In sensors.yaml, add the following to read the temperature and humidity data from the JSON in MQTT:

```yaml
 - platform: mqtt
    name: "MQTT greenhouse temp"
    state_topic: "home/plants/json"
    device_class: temperature
    unit_of_measurement: 'F'
    value_template: "{{ value_json.Temperature | round(1) }}"

  - platform: mqtt
    name: "MQTT greenhouse humidity"
    state_topic: "home/plants/json"
    device_class: humidity
    unit_of_measurement: '%'
    value_template: "{{ value_json.Humidity | round(1) }}"

```

### Lovelace Entity State

Displaying the temperature, and humidity is very straight forward.  The entities can be added to a card and the data provided in the configuration files about the sensors will result in the correct icons.

### Lovelace Entity Button Configuration

In the Lovelace UI, an `Entity Button Card` can be configured to toggle the garage door relay by publishing a MQTT message to the topic the ESP32 is subscribed to.

```yaml
entity: switch.water_plants
hold_action:
  action: more-info
icon_height: 1px
show_icon: true
show_name: true
tap_action:
  action: call-service
  service: mqtt.publish
  service_data:
    payload: toggle
    topic: home/greenhouse
type: entity-button
```
### Home Assistant Prometheus Exporter

Add the following to configuration.yaml to configure Home Assistant to export metrics for Prometheus to scrape.

```yaml
prometheus:
  namespace: hass
  filter:
    exclude_entities:
      - sensor.time_date
      - sensor.date_time
      - sensor.time
      - sensor.time_utc
      - sensor.internet_time
      - climate.downstairs
    exclude_domains:
      - binary_sensor
```

## Prometheus

On the Prometheus scraper, add the following to the `scrape_configs` section of prometheus.yml:

```yaml
scrape_configs:
- job_name: hass
  honor_timestamps: true
  scrape_interval: 1m
  scrape_timeout: 10s
  metrics_path: /api/prometheus
  scheme: http
  static_configs:
  - targets:
    - <hostname_or_IP_for_home_assistant>:8123
  ```

Once the data is available in Prometheus, use Grafana to create custom dashboards.
