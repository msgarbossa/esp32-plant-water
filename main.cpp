#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <PubSubClient.h>
#include <DHT.h>          // Library for DHT sensors

#define DHTPIN 14               // DHT Pin 
// Uncomment depending on your sensor type:
#define DHTTYPE DHT11           // DHT 11 
//#define DHTTYPE DHT22         // DHT 22  (AM2302)

// relay
const int relayPin = 13;

// LED for blink and status
const int ledPin = 2;

// Track time in milliseconds for scheduling of operations
unsigned long currentMillis;

// Temperature variables
DHT dht(DHTPIN, DHTTYPE);  // DHT object
char data[80];
const int tempWaitSeconds = 300; // how long to wait between temp readings, in seconds
unsigned long tempLastMillis;
unsigned long tempWaitMillis;

// Plant water variable
// initial watering values for relay: 3s every 240m
const int waterDurationSec = 16; // how long to water in seconds (20)
const int waterWaitMinutes = 180; // how long to wait between watering, in minutes (240)
// only water during daylight hours
const int startHour = 7;
const int endHour = 19;
//some global variables available anywhere in the program
unsigned long waterLastMillis;
unsigned long waterDurationMillis;
unsigned long waterWaitMillis;

// wifi credentials
const char* ssid     = "<MYSSID>";
const char* password = "<MYPASSWORD>";
int NAcounts=0; // used to reset if there are connection issues

// NTP client
const char* ntpServer = "<NTPSERVER>";
const long  gmtOffset_sec = -25200;
//const int   daylightOffset_sec = 3600;
const int   daylightOffset_sec = 0;

// local time calculations
time_t now;
struct tm * timeinfo;

// MQTT Network
IPAddress broker(192,168,1,X); // IP address of your MQTT broker eg. 192.168.1.50
//const char *BROKER_USER = "-----"
//const char *BROKER_PASS = "-----"
const char *ID = "plantWater";  // Name of our device, must be unique
const char *TOPIC_SUB = "home/greenhouse";  // topic to subscribe to
const char *TOPIC_PUB_STATE = "home/greenhouse/state";  // topic to publish state to
const char *TOPIC_PUB_JSON = "home/greenhouse/json";  // topic to publish state to
//const char *TOPIC_TEMP = "home/garage1/temp";  // topic to publish temp to
WiFiClient wclient;  // Setup wifi object
PubSubClient client(wclient); // Setup MQTT client object

// Reconnect to client
void mqtt_reconnect() {
  // Loop until we're reconnected
  NAcounts = 0;
  while (!client.connected() && NAcounts < 7) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if(client.connect(ID)) {
      client.subscribe(TOPIC_SUB);
      Serial.println("connected");
      Serial.print("Subcribed to: ");
      Serial.println(TOPIC_SUB);
      Serial.println('\n');

    } else {
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
      NAcounts++;
    }
  }
}

// Connect to WiFi network
void wifi_reconnect() {
  btStop(); // turn off bluetooth
  Serial.print("\nReconnecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password); // Connect to network

  while (WiFi.status() != WL_CONNECTED) { // Wait for connection
    delay(1000);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void mqtt_publish(const char* topic, const char* msg) {
  short int pubRetry = 0;
  short int pubDone = 0;
  while (!pubDone && pubRetry < 3) {
    // Reconnect WiFi if connection is lost
    if ((WiFi.status() != WL_CONNECTED)) {
      Serial.println("publish: WiFi reconnect!");
      wifi_reconnect();
    }
    if (!client.connected()) {
      Serial.println("publish: MQTT reconnect!");
      mqtt_reconnect();
    }
    Serial.println(String(msg));
    pubDone = client.publish(topic, String(msg).c_str());
    pubRetry++;
  }
}

void blink_now() {
    digitalWrite(ledPin, HIGH);
    delay(1000);
    digitalWrite(ledPin, LOW);
}

void doWater() {
  blink_now();
  Serial.println("toggle");
  mqtt_publish(TOPIC_PUB_STATE, "toggled");
  waterLastMillis = currentMillis;  //IMPORTANT to save the start time of the last watering.
  digitalWrite(relayPin, HIGH); // turn on the relay
  digitalWrite(ledPin, HIGH); // turn on the LED
  delay(waterDurationMillis);
  digitalWrite(relayPin, LOW);  // turn off the relay
  digitalWrite(ledPin, LOW);  // turn off the LED
}

void doTemp() {
  tempLastMillis = currentMillis;  //IMPORTANT to save the start time of the last reading.

  // Read temperature in Celcius
  float t = dht.readTemperature();
  // Read humidity
  float h = dht.readHumidity();

  // Nothing to send. Warn on MQTT debug_topic and then go to sleep for longer period.
  if ( isnan(t) || isnan(h)) {
    Serial.println("[ERROR] Please check the DHT sensor !");
    //client.publish(TOPIC_TEMP, "[ERROR] Please check the DHT sensor !", true);
    mqtt_publish(TOPIC_PUB_STATE, "[ERROR] Please check the DHT sensor !");
    return;
  } else {
    t = ((t * 9)/5) + 32;  // convert to fahrenheit
  }

  Serial.print("Temperature : ");
  Serial.println(t);
  Serial.print("Humidity : ");
  Serial.println(h);

  String payload="{\"Humidity\":"+String(h)+ ",\"Temperature\":"+String(t)+"} ";
  payload.toCharArray(data, (payload.length() + 1));
  //client.publish(TOPIC_PUB_JSON, data);
  mqtt_publish(TOPIC_PUB_JSON, data);
}

void setup_wifi_ota() {
  btStop(); // turn off bluetooth

  // Don't automatically handle WiFi reconnect
  // https://github.com/esp8266/Arduino/issues/5527
  // if (WiFi.SSID() != "") {
  //   Serial.println("WiFi credentials set in flash, wiping them");
  //   WiFi.disconnect();
  // }
  // if (WiFi.getAutoConnect()) {
  //   Serial.println("Disabling auto-connect");
  //   WiFi.setAutoConnect(false);
  // }
  // WiFi.persistent(false);

  Serial.print("\nConnecting to ");
  Serial.println(ssid);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  String response;

  for (int i = 0; i < length; i++) {
    response += (char)payload[i];
  }
  Serial.print("Message arrived ");
  Serial.print(topic);
  Serial.print(" ");
  Serial.println(response);
  if(response == "toggle") { // toggle the relay
    Serial.println("toggle");
    blink_now();
    doWater();
    //client.publish(TOPIC_PUB_STATE, "toggled");
    mqtt_publish(TOPIC_PUB_STATE, "toggled");
  } else if(response == "ping") { // reply pong
    Serial.println("ping received");
    blink_now();
    //client.publish(TOPIC_PUB_STATE, "pong");
    mqtt_publish(TOPIC_PUB_STATE, "pong");
  } else if (response == "reboot") { // reboot
    Serial.println("Rebooting...");
    mqtt_publish(TOPIC_PUB_STATE, "rebooting");
    delay(1000);
    ESP.restart();
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(relayPin, OUTPUT);
  pinMode(ledPin, OUTPUT);     // Initialize the ledPin pin as an output
  delay(100);

  setup_wifi_ota(); // Connect to network

  // init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  client.setServer(broker, 1883);
  client.setCallback(callback); // Initialize the callback routine
  // Publish message that device is online
  if (!client.connected()) {
    mqtt_reconnect();
  }
  client.loop(); // returns true if still connected

  Serial.println("Setup complete");
  mqtt_publish(TOPIC_PUB_STATE, "online");
  blink_now();

  // convert seconds to milliseconds for calculations
  waterDurationMillis = waterDurationSec*1000;
  waterWaitMillis = waterWaitMinutes*60000;
  tempWaitMillis = tempWaitSeconds*1000;

  // initiliaze last-run values to trigger on first loop
  currentMillis = millis();
  waterLastMillis = currentMillis - waterWaitMillis;
  tempLastMillis = currentMillis - tempWaitMillis;

  dht.begin();
  delay(10000); // takes a few seconds for DHT to become available
}

void loop() {
  ArduinoOTA.handle();
  
  // Reconnect WiFi if connection is lost
  if ((WiFi.status() != WL_CONNECTED)) {
    Serial.println("WiFi not connected!");
    wifi_reconnect();
  }
  // Reconnect MQTT if connection is lost
  if (!client.connected()) {
    mqtt_reconnect();
  }
  client.loop(); // returns true if still connected

  currentMillis = millis();  // get the current "time" (actually the number of milliseconds since the program started)
 
  // test whether the period has elapsed for watering
  if (currentMillis - waterLastMillis >= waterWaitMillis) {
    // Check time from local clock
    time(&now);
    timeinfo = localtime(&now);
    //Serial.println(timeinfo->tm_hour);
    //if (timeinfo->tm_hour < startHour || timeinfo->tm_hour > endHour) {
    //  return; // skip doWater unless within watering time
    //}
    // skip doWater unless within watering time
    if (timeinfo->tm_hour > startHour && timeinfo->tm_hour < endHour) {
      doWater();
    }
  }

  // test whether the period has elapsed for temp reading
  if (currentMillis - tempLastMillis >= tempWaitMillis) {
    doTemp();
  }

  delay(5000);  // sleep 5s
}
