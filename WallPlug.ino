#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Ticker.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson 

/************ Defines ******************/

#define RELAY_1 13
#define RELAY_2 12
#define LED 16
#define BTN1 2

/************ Global State ******************/

WiFiManager wifiManager;
WiFiClient client_mqtt; // use WiFiClientSecure in case of SSL
Ticker ticker;

Adafruit_MQTT_Client *mqtt;
Adafruit_MQTT_Publish *mqtt_status;
Adafruit_MQTT_Subscribe *mqtt_outlet1;
Adafruit_MQTT_Subscribe *mqtt_outlet2;
Adafruit_MQTT_Subscribe *mqtt_led;

char mqtt_server[100] = "";
char mqtt_port[7] = "";
char mqtt_username[50] = "";
char mqtt_password[50] = "";

//flag for saving data
bool shouldSaveConfig = false;

// MQTT topic. Must be a global variable because MQTT lib just stores a pointer
char mqtt_publish_status[100];
char mqtt_subscribe_outlet1[100];
char mqtt_subscribe_outlet2[100];
char mqtt_subscribe_led[100];

/*************************** Sketch Code ************************************/

void tick()
{
  //toggle state
  int state = digitalRead(LED);  // get the current state of GPIO pin
  digitalWrite(LED, !state);     // set pin to the opposite state
}

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}

void setup() {
  Serial.begin(115200);

  Serial.print("\nReset reason: ");
  Serial.println(ESP.getResetReason());

  //set led pin as output
  pinMode(LED, OUTPUT);
  ticker.attach(0.6, tick);

  // set relay as outputs
  pinMode(RELAY_1, OUTPUT);
  pinMode(RELAY_2, OUTPUT);

  //reset settings - for testing
  //wifiManager.resetSettings();
  //SPIFFS.format();

  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);
  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_username, json["mqtt_username"]);
          strcpy(mqtt_password, json["mqtt_password"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read
 
  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("mqtt_server", "MQTT server", mqtt_server, sizeof(mqtt_server)-1);
  WiFiManagerParameter custom_mqtt_port("mqtt_port", "MQTT port", mqtt_port, sizeof(mqtt_port)-1);
  WiFiManagerParameter custom_mqtt_username("mqtt_username", "MQTT username", mqtt_username, sizeof(mqtt_username)-1);
  WiFiManagerParameter custom_mqtt_password("mqtt_password", "MQTT password", mqtt_password, sizeof(mqtt_password)-1);

   //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_username);
  wifiManager.addParameter(&custom_mqtt_password);

  if (!wifiManager.autoConnect()) {
    Serial.println("failed to connect and hit timeout");
    ESP.reset();
  }

  if (digitalRead(BTN1) == LOW ) {
    Serial.println("Config button pressed");
    wifiManager.startConfigPortal("ConfigAP");
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected to Wifi");
  ticker.detach();

  //LED off
  digitalWrite(LED, LOW);

  // OTA
  // An important note: make sure that your project setting of Flash size is at least double of size of the compiled program. Otherwise OTA fails on out-of-memory.
  ArduinoOTA.setPassword((const char *)"password");
  ArduinoOTA.onStart([]() {
    Serial.println("OTA: Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA: End");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    char errormsg[100];
    sprintf(errormsg, "OTA: Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) strcpy(errormsg+strlen(errormsg), "Auth Failed");
    else if (error == OTA_BEGIN_ERROR) strcpy(errormsg+strlen(errormsg), "Begin Failed");
    else if (error == OTA_CONNECT_ERROR) strcpy(errormsg+strlen(errormsg), "Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) strcpy(errormsg+strlen(errormsg), "Receive Failed");
    else if (error == OTA_END_ERROR) strcpy(errormsg+strlen(errormsg), "End Failed");
    Serial.println(errormsg);
  });
  ArduinoOTA.begin();

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_username, custom_mqtt_username.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_username"] = mqtt_username;
    json["mqtt_password"] = mqtt_password;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  // Setup MQTT subscription
  mqtt = new Adafruit_MQTT_Client(&client_mqtt, mqtt_server, atoi(mqtt_port), mqtt_username, mqtt_password);

  char mqtt_base_path[100] = "nodes/";
  strcat(mqtt_base_path, mqtt_username);

 // MQTT SUBSCRIBE
  strcpy(mqtt_subscribe_outlet1, mqtt_base_path);
  strcat(mqtt_subscribe_outlet1, "/commands/outlet/1");
  mqtt_outlet1 = new Adafruit_MQTT_Subscribe(mqtt, mqtt_subscribe_outlet1);
  mqtt->subscribe(mqtt_outlet1);

  strcpy(mqtt_subscribe_outlet2, mqtt_base_path);
  strcat(mqtt_subscribe_outlet2, "/commands/outlet/2");
  mqtt_outlet2 = new Adafruit_MQTT_Subscribe(mqtt, mqtt_subscribe_outlet2);
  mqtt->subscribe(mqtt_outlet2);

  strcpy(mqtt_subscribe_led, mqtt_base_path);
  strcat(mqtt_subscribe_led, "/commands/led");
  mqtt_led = new Adafruit_MQTT_Subscribe(mqtt, mqtt_subscribe_led);
  mqtt->subscribe(mqtt_led);

  // MQTT PUBLISH
  strcpy(mqtt_publish_status, mqtt_base_path);
  strcat(mqtt_publish_status, "/register");
  mqtt_status = new Adafruit_MQTT_Publish(mqtt, mqtt_publish_status);
  mqtt->will(mqtt_publish_status, "disconnected", 0, 1); // set last will and retain=true
}

void loop() {
  if (!mqtt->connected()) {
    Serial.print("Connecting to MQTT... ");
    uint8_t retries = 3;
    int8_t ret;
    while ((ret = mqtt->connect()) != 0) { // connect will return 0 for connected
       Serial.println(mqtt->connectErrorString(ret));
       Serial.println("Retrying MQTT connection in 5 seconds...");
       ArduinoOTA.handle(); // to be able to update even without MQTT
       mqtt->disconnect();
       delay(5000);  // wait 5 seconds
       retries--;
       if (retries == 0) {
//         wifiManager.startConfigPortal("ConfigAP");
         ESP.reset();
       }
    }
    Serial.println("MQTT Connected!");
    mqtt_status->publish("connected");
  }

  ArduinoOTA.handle();

  Adafruit_MQTT_Subscribe *subscription;
  while ((subscription = mqtt->readSubscription(5000))) {
    if (subscription == mqtt_outlet1) {
      Serial.print(F("Outlet1: "));
      Serial.println((char *)mqtt_outlet1->lastread);
      if (strcmp((char *)mqtt_outlet1->lastread, "ON") == 0) {
        digitalWrite(RELAY_1, HIGH); 
      }
      if (strcmp((char *)mqtt_outlet1->lastread, "OFF") == 0) {
        digitalWrite(RELAY_1, LOW); 
      }
      if (strcmp((char *)mqtt_outlet1->lastread, "TOGGLE") == 0) {
        //toggle state
        int state = digitalRead(RELAY_1);  // get the current state of GPIO pin
        digitalWrite(RELAY_1, !state);
      }
    }
    if (subscription == mqtt_outlet2) {
      Serial.print(F("Outlet2: "));
      Serial.println((char *)mqtt_outlet2->lastread);
      if (strcmp((char *)mqtt_outlet2->lastread, "ON") == 0) {
        digitalWrite(RELAY_2, HIGH); 
      }
      if (strcmp((char *)mqtt_outlet2->lastread, "OFF") == 0) {
        digitalWrite(RELAY_2, LOW); 
      }
      if (strcmp((char *)mqtt_outlet2->lastread, "TOGGLE") == 0) {
        //toggle state
        int state = digitalRead(RELAY_2);  // get the current state of GPIO pin
        digitalWrite(RELAY_2, !state);
      }
    }
    if (subscription == mqtt_led) {
      Serial.print(F("LED: "));
      Serial.println((char *)mqtt_led->lastread);
      if (strcmp((char *)mqtt_led->lastread, "ON") == 0) {
        digitalWrite(LED, HIGH); 
      }
      if (strcmp((char *)mqtt_led->lastread, "OFF") == 0) {
        digitalWrite(LED, LOW); 
      }
      if (strcmp((char *)mqtt_led->lastread, "TOGGLE") == 0) {
        //toggle state
        int state = digitalRead(LED);  // get the current state of GPIO pin
        digitalWrite(LED,!state); 
      }
    }
  }
 
  // ping the server to keep the mqtt connection alive
  if(!mqtt->ping()) {
    mqtt->disconnect();
  }
}

