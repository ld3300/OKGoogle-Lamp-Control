/***************************************************
  Using Adafruit MQTT Library

  Must use ESP8266 Arduino from:
    https://github.com/esp8266/Arduino

  Works great with Adafruit's Huzzah ESP board & Feather
  ----> https://www.adafruit.com/product/2471
  ----> https://www.adafruit.com/products/2821

  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  Written by Tony DiCola for Adafruit Industries.
  MIT license, all text above must be included in any redistribution
 ****************************************************/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>                 // This library handles Connection and AP settings
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
#include "private.h"

#define PUSHUPDATETIME 3600000  // 60 minutes

#define LAMPPIN 0                         // Pin controlling lamp output
#define SWITCHPIN 12                      // Pin that reads change of built-in lamp switch (soldered to chip on esp-01)
#define LAMPON "lampon"
#define LAMPOFF "lampoff"
#define AIOTHROTTLETIMEOUT 60000    // time to wait before trying to publish again if we hit throttle status

const IPAddress apIP(192, 168, 1, 1);     // IP when in AP mode (when wifi connect fails)
const char *apSSID = "Bedroom_Lamp_SETUP";     // AP name to connect to to configure wifi
bool settingMode;                      // Store setting of wifi connect
String ssidList;                          // Scanned SSIDs in AP mode
bool lampState = false;                    // Is lamp currently on or off
bool lastLampState = false;
unsigned long lastUpdateTime = 0;

DNSServer dnsServer;
ESP8266WebServer webServer(80);

// Create an ESP8266 WiFiClient class to connect to the MQTT server.
WiFiClient client;
// or... use WiFiClientSecure for SSL
//WiFiClientSecure client;

Adafruit_MQTT_Client mqtt(&client, MQTT_SERV, MQTT_PORT, MQTT_NAME, MQTT_PASS);   // These values are stored in private.h file
Adafruit_MQTT_Subscribe bedroom = Adafruit_MQTT_Subscribe(&mqtt, MQTT_NAME "/feeds/bedroom", MQTT_QOS_1);
Adafruit_MQTT_Publish bedroomPub = Adafruit_MQTT_Publish(&mqtt, MQTT_NAME "/feeds/bedroom", MQTT_QOS_1);
Adafruit_MQTT_Publish bedroomPush = Adafruit_MQTT_Publish(&mqtt, MQTT_NAME "/feeds/bedroomStatus", MQTT_QOS_1);
Adafruit_MQTT_Subscribe throttle = Adafruit_MQTT_Subscribe(&mqtt, MQTT_NAME "/throttle", MQTT_QOS_1);  // check if data limit reached

void MQTT_connect(){
  int8_t ret;
  // Stop if already connected
  if (mqtt.connected())
    return;

  Serial.print("Connecting to MQTT... ");
  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0)
  { // connect will return 0 for connected
    Serial.println(mqtt.connectErrorString(ret));
    Serial.println("Retrying MQTT connection in 5 seconds...");
    mqtt.disconnect();
    delay(5000); // wait 5 seconds
    retries--;
    if (retries == 0)
    {
      // basically die and wait for WDT to reset me
      while (1)
        ;
    }
  }
  Serial.println("MQTT Connected!");
}

void lampStatePublish(bool xlampstate){       // We are going to publish our state back in case it is changed locally
  Serial.print(F("\nSending lamp val "));
  Serial.print(xlampstate);
  Serial.print("...");
  char* xlamp = LAMPOFF;
  if(xlampstate) xlamp = LAMPON;
  if (! bedroomPush.publish(xlamp)) {
    Serial.println(F("Failed"));
  } else {
    Serial.println(F("OK!"));
  }
}

void handleSwitchISR() {
  static unsigned long last_interrupt_time = 0;
  unsigned long interrupt_time = millis();
  if(interrupt_time - last_interrupt_time > 200 || interrupt_time < last_interrupt_time){
    lampState = !lampState;                     // update lamp state
    digitalWrite(LAMPPIN, lampState);          // Invert state of lamp
    last_interrupt_time = interrupt_time;
  }
}

void setup(){
  pinMode(LAMPPIN, OUTPUT);
  pinMode(SWITCHPIN, INPUT);
  Serial.begin(115200);
  // switchState = digitalRead(SWITCHPIN);   // Get initial state of the lamp switch
  attachInterrupt(SWITCHPIN, handleSwitchISR, CHANGE);
  lampState = true;
  handleSwitchISR();
  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  //reset saved settings
  //wifiManager.resetSettings();

  //set custom ip for portal
  //wifiManager.setAPStaticIPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  //fetches ssid and pass from eeprom and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  wifiManager.autoConnect("BedroomLampAP");
  //or use this for auto generated name ESP + ChipID
  //wifiManager.autoConnect();

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  // OTA Port defaults to 8266
  // ArduinoOTA.setPort(8266);
  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("BedroomLamp");
  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  mqtt.subscribe(&bedroom);         // Subscribe to the bedroom feed on adafruit io
  MQTT_connect();
  bedroomPub.publish(LAMPOFF);      // write an initial state to keep things intact until ability to get last message is available
  lampStatePublish(false);
}

void loop(){
  static bool mqtt_publish_overflow = false;
  ArduinoOTA.handle();          // Check for OTA
  //Connect/Reconnect to MQTT
  MQTT_connect();

  // periodically push state to mqtt. either by state change, timeout, or millis() rollover
  if(lastLampState != lampState || millis() >= PUSHUPDATETIME + lastUpdateTime || millis() < lastUpdateTime){
    static unsigned long mqtt_last_time = millis();
    if(mqtt_publish_overflow  || millis() < mqtt_last_time){                  // Stop publishing if we have hit service overflow
      mqtt_last_time = millis();
    }
    else if (millis() - mqtt_last_time > AIOTHROTTLETIMEOUT){
      mqtt_publish_overflow = false;
      lampStatePublish(lampState);                // Push state change back to MQTT server
      lastLampState = lampState;
      lastUpdateTime = millis();
    }
  }

  // Read from our subscription queue until we run out, or
  // wait up to 5 seconds for subscription to update
  Adafruit_MQTT_Subscribe *subscription;
  while ((subscription = mqtt.readSubscription(2000))){
    //If we're in here, a subscription updated...
    if (subscription == &bedroom){
      char buffer[50];
      sprintf(buffer, "%s", (char *)bedroom.lastread);
      //Print the new value to the serial monitor
      Serial.print("Bedroom: ");
      Serial.println(buffer);
      if (strstr(buffer, LAMPON) != NULL){
        digitalWrite(LAMPPIN, HIGH);
        Serial.println("On executed");
        lampState = true;
      }
      else if (strstr(buffer, LAMPOFF) != NULL){
        digitalWrite(LAMPPIN, LOW);
        Serial.println("Off executed");
        lampState = false;
      }
    }
    else if (subscription == &throttle){
      mqtt_publish_overflow = true;
    }
  }

  // ping the server to keep the  mqtt connection alive
  // if (!mqtt.ping()){             // Commented because it seemed to be causing packet issues
  //   mqtt.disconnect();
  // }
}
