#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>

/************ WIFI and MQTT INFORMATION (CHANGE THESE FOR YOUR SETUP) ******************/
#define wifi_ssid "metalab" //enter your WIFI SSID
#define wifi_password 0 //enter your WIFI Password

#define mqtt_server "10.20.30.97" // Enter your MQTT server adderss or IP. I use my DuckDNS adddress (yourname.duckdns.org) in this field
#define mqtt_user "" //enter your MQTT username
#define mqtt_password "" //enter your password

#define SENSORNAME "mr-tea" //change this to whatever you want to call your device
#define OTApassword "" //the password you will need to enter to upload remotely via the ArduinoIDE
int OTAport = 8266;

/************ Sensing Board Defintion ******************/
#define DATA_PIN D2 

/****************************** MQTT TOPICS (change these topics as you wish)  ***************************************/
// Customize this for your device
String topicsRoot = String("bruh/") + String(SENSORNAME) + String("/");

// The other topics will be generated with the root topic prefixed by default
String topicLWT        = topicsRoot + "LWT";
String topicStatePower = topicsRoot + "POWER";

char message_buff[100] = "";

WiFiClient espClient; //this needs to be unique for each controller
PubSubClient client(espClient); //this needs to be unique for each controller

////////////////////////////////////////////////////////////

void setup() {
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  Serial.begin(115200);

  pinMode(DATA_PIN, INPUT);

  setup_wifi();

  client.setServer(mqtt_server, 1883); //CHANGE PORT HERE IF NEEDED
  client.setCallback(callback);

  ArduinoOTA.setPort(OTAport);
  ArduinoOTA.setHostname(SENSORNAME);
  ArduinoOTA.setPassword((const char *)OTApassword);
  
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
    Serial.println("Start updating " + type);
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });

  ArduinoOTA.begin();
  
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(wifi_ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  unsigned int i = 0;

  // Copy the data into our message buffer ...
  for (i = 0; i < length; i++) {
    message_buff[i] = payload[i];
  }
  message_buff[i] = '\0';
  
  Serial.println("Bug? Got message in subscribed topic but don't know what to do with it: " + String(topic));
  Serial.println("The message was: " + String(message_buff));
}

// Those values derive from the sensing board
static const int POWER_ON = 0;
static const int POWER_OFF = 1;

// Determined by educated guessing
static const int POWER_OFF_COUNT_THRESHOLD = 500;

// Number of consecutive reads that reported that the power is OFF
int power_off_count = 0;

// Initially we assume that the power is OFF
int POWER_STATE = POWER_OFF;

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  ArduinoOTA.handle();

  // Read the current power value reported from the board
  int currentValue = digitalRead(DATA_PIN);

  // In case the board reported that no power is
  // used we have to be smart because we get a lot
  // of false "negatives".
  if(currentValue == POWER_OFF) {
    // Only if we are currently ON we have to be smart.
    if(POWER_STATE == POWER_ON) {
      // Increment the power off counter which
      // decides if an actual state change should happen.
      power_off_count ++;

      // If we are already above the threshold change the
      // power state finally to OFF and report change via MQTT.
      if(power_off_count > POWER_OFF_COUNT_THRESHOLD) {
        POWER_STATE = POWER_OFF;
        
        Serial.println("Reporting POWER=OFF");
        client.publish(topicStatePower.c_str(), "OFF", true);
      }
    }
  } else {
    // Reset the counter
    power_off_count = 0;

    if(POWER_STATE == POWER_OFF) {
      // No need to check here, change the state to
      // ON and report via MQTT.      
      POWER_STATE = POWER_ON;

      Serial.println("Reporting POWER=ON");
      client.publish(topicStatePower.c_str(), "ON", true);
    }
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(SENSORNAME, mqtt_user, mqtt_password, topicLWT.c_str(), 0, true, "offline")) {
      Serial.println("connected");
      
      // Publish the initial state after connecting
      client.publish(topicStatePower.c_str(), "OFF", true);

      client.publish(topicLWT.c_str(), "online", true);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}