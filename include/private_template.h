
// Template file. Rename to private.h to avoid commiting to github

#define AIO_SERVER      "io.adafruit.com"       // or other MQTT service
#define AIO_SERVERPORT  1883                    // 1883 or 8883 for SSL security
#define AIO_CLIENTID    "YOUR MQTT CLIENT ID"   // Client ID for this device, must be unique 1-23 chars and improves successful connections
#define AIO_USERNAME    "YOUR USERNAME"         // Adafruit IO username
#define AIO_KEY         "YOUR AIO KEY"          // AIO Key, available online dashboard

#define HOSTNAME        "MyWifiHostname"        // Hostname for AP
#define OTA_PASS        "OTA PASSWORD"          // Password for OTA updates, I couldn't get it to work
