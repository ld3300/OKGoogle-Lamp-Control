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
#include "private.h"                      // place to store passwords and keys.  add private.h to .gitignore

#define PUSHUPDATETIME 3600000            // 60 minutes -- How often to push current state to server to maintain sync

#define LAMPPIN 0                         // Pin controlling lamp output
#define SWITCHPIN 12                      // Pin that reads change of built-in lamp switch (soldered to chip on esp-01)
#define LAMPON "lampon"                   // strings to look for in on and off functions
#define LAMPOFF "lampoff"
#define STATUSREQUEST "lampstatus"        // we will use this string to identify when someone wants us to publish current on/off state
#define AIOTHROTTLETIMEOUT 60000          // time to wait before trying to publish again if we hit throttle status

const IPAddress apIP(192, 168, 1, 1);     // IP when in AP mode (when wifi connect fails)
const char *apSSID = "Bedroom_Lamp_SETUP";     // AP name to connect to to configure wifi
bool settingMode;                         // Store setting of wifi connect
String ssidList;                          // Scanned SSIDs in AP mode
bool lampState = false;                   // Is lamp currently on or off
bool lastLampState = false;
unsigned long lastUpdateTime = 0;         // Last time we sent update so we don't do it too often
bool mqtt_publish_overflow = false;       // flag for if Adafruit IO data rate exceeded

DNSServer dnsServer;
ESP8266WebServer webServer(80);

// Create an ESP8266 WiFiClient class to connect to the MQTT server.
WiFiClient client;
// or... use WiFiClientSecure for SSL
// WiFiClientSecure client;

// setup for publishing and subscribing.  AIO feeds are username/feeds/feedname or username/f/feedname
// With the exception of some like username/throttle or username/errors or time/seconds
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_CLIENTID, AIO_USERNAME, AIO_KEY);   // These values are stored in private.h file
Adafruit_MQTT_Publish bedroomPush = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/bedroomStatus", MQTT_QOS_1);
Adafruit_MQTT_Publish bedroomPub = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/bedroom", MQTT_QOS_1);
Adafruit_MQTT_Subscribe bedroom = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/bedroom", MQTT_QOS_1);
Adafruit_MQTT_Subscribe throttle = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/throttle", MQTT_QOS_1);  // check if data limit reached
// Adafruit_MQTT_Subscribe timefeed = Adafruit_MQTT_Subscribe(&mqtt, "time/seconds"); // In case want to do time based actions

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
    delay(2000); // wait 5 seconds
    retries--;
    if (retries == 0) // if we run out of tries do rest of loop
    {
      // hold, we can loop and try again later
      delay(3000);
    }
  }
  Serial.println("MQTT Connected!");
}

void wifi_setup(){
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
}

void ota_setup(){
  // OTA Port defaults to 8266
  // ArduinoOTA.setPort(8266);
  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(HOSTNAME);                     // defined in private.h
  // No authentication by default
  // ArduinoOTA.setPassword((const char *)OTA_PASS);

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
}

void lampStatePublish(bool xlampstate){       // We are going to publish our state back in case it is changed locally
  Serial.print(F("\nSending lamp val "));
  Serial.print(xlampstate);
  Serial.print("...");
  char* xlamp = LAMPOFF;                    // initialize to off
  if(xlampstate) xlamp = LAMPON;            // if our state is on
  if (! bedroomPush.publish(xlamp)) {       // return this value so controllers can update
    Serial.println(F("Failed"));
  } 
  else {
    Serial.println(F("OK!"));
  }
}

void update_state(bool updateRequest) {
  // if we get an update request, or if lamp state changed, or if we hit our interval update timeout
  if(updateRequest || lastLampState != lampState || (millis() - lastUpdateTime > PUSHUPDATETIME)){
    static unsigned long flag_last_time = millis();   // Last time we saw AIO throttle flag
    static bool overflow_flag = false;                // flag to keep us from attempting to write while throttled
    if(mqtt_publish_overflow){                        // Stop publishing if we have hit service overflow
      flag_last_time = millis();                      // We saw a throttle, update flag time
      overflow_flag = true;                           // activate throttle flag
      mqtt_publish_overflow = false;                  // clear overflow so we can get updated if it happens again before timeout
    }
    // if there is no throttle flag, or if we hit the throttle timeout
    else if (!overflow_flag || (millis() - flag_last_time > AIOTHROTTLETIMEOUT)){
      lampStatePublish(lampState);                    // Push state change back to MQTT server
      lastLampState = lampState;                      // update last lamp to current state
      lastUpdateTime = millis();                      // record last update time so we don't overpublish
      overflow_flag = false;                          // clear overflow flag if it was set
    }
  }
}

void handleSwitchISR() {    // ISR for switch with debounce.  Could be handled in a less process intense way
  static unsigned long last_interrupt_time = 0;   // keep the last time interrupt called
  unsigned long interrupt_time = millis();        // get current time
  if(interrupt_time - last_interrupt_time > 200){ // If ISR gets calls more often that 200ms then it is bounce
    lampState = !lampState;                       // update lamp state
    digitalWrite(LAMPPIN, lampState);             // Invert state of lamp
    last_interrupt_time = interrupt_time;         // Set last interrupt time to block button bounce
  }
}

void readSubscriptions(){
  Adafruit_MQTT_Subscribe *subscription;
  while ((subscription = mqtt.readSubscription(500))){
    //If we're in here, a subscription updated...
    if (subscription == &bedroom){            // check if incoming is for bedroom feed
      char buffer[50];
      sprintf(buffer, "%s", (char *)bedroom.lastread);
      //Print the new value to the serial monitor
      Serial.print("Bedroom: ");
      Serial.println(buffer);                 // print the entire buffer of bedroom feed
      if (strstr(buffer, LAMPON) != NULL){    // if we get lamp on command
        digitalWrite(LAMPPIN, HIGH);          // pull high
        Serial.println("On executed");
        lampState = true;                     // update lamp state
      }
      else if (strstr(buffer, LAMPOFF) != NULL){    // if lamp off command, write lamp pin low and change state
        digitalWrite(LAMPPIN, LOW);
        Serial.println("Off executed");
        lampState = false;
      }
      else if (strstr(buffer, STATUSREQUEST) != NULL){  // If we get a status request then force state update
        update_state(true);
      }
    }
    else if (subscription == &throttle){    // if the throttle feed activated then we have a publishing overflow
      mqtt_publish_overflow = true;         // the maximum publish rate is shared among all devices on account
    }
  }

}

void setup(){
  pinMode(LAMPPIN, OUTPUT);           // Pin used to drive lamp controlling circuit see README for info
  pinMode(SWITCHPIN, INPUT);          // Pin we put in line switch on
  attachInterrupt(SWITCHPIN, handleSwitchISR, CHANGE);  // declare ISR first so that switch will work if wifi hangs

  Serial.begin(115200);
  
  wifi_setup();                     // Setup and connect to wifi
  ota_setup();                      // Setup for OTA

  mqtt.subscribe(&bedroom);         // Subscribe to the bedroom feed on adafruit io
  mqtt.subscribe(&throttle);        // throttle feed will help us if we publish too frequently
  MQTT_connect();                   // Connect to server so we can send initial state

  bedroomPub.publish(LAMPOFF);      // write an initial state to keep things intact until ability to get last message is available
  lampStatePublish(false);
}

void loop(){
  //connect wifi if not connected
  if (WiFi.status() != WL_CONNECTED) {
    delay(1);
    wifi_setup();
  }

  ArduinoOTA.handle();          // Check for OTA
  
  MQTT_connect();               // Connect/Reconnect to MQTT

  // periodically push state to mqtt. either by state change, timeout, or millis() rollover
  update_state(false);          // the boolean is used to satisfy an external update request

  // Read from our subscription queue until we run out
  readSubscriptions();

  // ping the server to keep the  mqtt connection alive
  // if (!mqtt.ping()){             // Commented because it seemed to be causing packet issues
  //   mqtt.disconnect();
  // }
}
