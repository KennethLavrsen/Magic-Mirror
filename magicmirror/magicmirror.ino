// Magic Mirror Genetic ESP8266 like a Sonoff TH10
// Note to Kenneth - the module is modified for 4M flash
// The sensor input on the Sonoff is used for an external on/off button

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include "secrets.h"

/****** All the configuration happens here ******/
// Wifi
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;
IPAddress ip(192, 168, 1, 43); // update this to the desired IP Address
IPAddress dns(192, 168, 1, 1); // set dns to local
IPAddress gateway(192, 168, 1, 1); // set gateway to match your network
IPAddress subnet(255, 255, 255, 0); // set subnet mask to match your network

// MQTT
const char* mqttServer        = MQTT_SERVER;
const char* mqttUsername      = MQTT_USER;
const char* mqttPassword      = MQTT_PASSWORD;
const int   mqttPort          = 1883;
const char* mqttTopicAnnounce = "magicmirror/announce";
const char* mqttTopicSet      = "magicmirror/set";
const char* mqttTopicState    = "magicmirror/state";

// Host name and OTA password
const char* hostName    = "magicmirror";
const char* otaPassword = OTA_PASSWORD;


// ****** End of configuration ******


// Objects
ESP8266WebServer server(80);

WiFiClient espClient;
PubSubClient client(espClient);

// Numbers are GPIO numbers
#define RELAY     12
#define REDLED    13
#define EXTBUTTON 14         // Pin which is connected to the DHT sensor.
#define BUTTON    0

// Globals
bool lightStatus = false;
bool previousLightStatus = false;
int debounceCounter = 0;
unsigned long mqttReconnectTimer = 0;
unsigned long wifiReconnectTimer = 0;


void mqttCallback(char* topic, byte* payload, unsigned int length) {

    // Creating safe local copies of topic and payload
    // enables publishing MQTT within the callback function
    // We avoid functions using malloc to avoid memory leaks
    
    char topicCopy[strlen(topic) + 1];
    strcpy(topicCopy, topic);
    
    char message[length + 1];
    for (int i = 0; i < length; i++) message[i] = (char)payload[i];
    
    message[length] = '\0';
  
    if ( strcmp(topicCopy, mqttTopicSet) == 0 ) {
        if ( strcmp(message, "on") == 0 ) lightOn();
        if ( strcmp(message, "off") == 0 ) lightOff();
    }

    else {
        return;
    }

}

bool mqttConnect() {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(hostName, mqttUsername, mqttPassword)) {
        Serial.println("connected");
        // Once connected, publish an announcement...
        client.publish(mqttTopicAnnounce, "connected");
        client.subscribe(mqttTopicSet);
    }

    return client.connected();
}


void sendWebPage(void) {

    String webPage = "";
   
    Serial.println("Sending webpage");
    webPage += "<html><head></head><body>\n";
    webPage += "<h1>Magic Mirror light</h1>\n";
    webPage += "<p>light is ";
    webPage += lightStatus;
    webPage += "</p>\n";
    webPage += "<p><a href=\"/on\"><button>ON</button></a> <a href=\"/off\"><button>OFF</button></a></p>\n";
    webPage += "<p><a href=\"/\"><button>Status Only</button></a></p>\n";
    webPage += "</body></html>\n";
    server.send(200, "text/html", webPage);
}

bool lightOn() {
        lightStatus = true;
        digitalWrite(RELAY, HIGH);
        client.publish(mqttTopicState, "on", true );
        return true;
}

bool lightOff(void) {
        lightStatus = false;
        digitalWrite(RELAY, LOW);
        client.publish(mqttTopicState, "off", true );
        return true;
}

void setup_wifi() {
  
    Serial.print(F("Setting static ip to : "));
    Serial.println(ip);
   
    // Connect to WiFi network
    Serial.println();
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);
  
    // ESP8266 does not follow same order as Arduino
    WiFi.mode(WIFI_STA);
    WiFi.config(ip, gateway, subnet, dns); 
    WiFi.begin(ssid, password);
   
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");  
}

void setup(void) 
{
    Serial.begin(115200);
    Serial.println("Booting...");

    pinMode(RELAY, OUTPUT);
    pinMode(REDLED, OUTPUT);
    pinMode(EXTBUTTON, INPUT);
    digitalWrite(RELAY, LOW);
    digitalWrite(REDLED, LOW);

    // Setup WIFI
    setup_wifi();
    wifiReconnectTimer = millis();
    digitalWrite(REDLED, HIGH);

    // Setup Webserver
    server.on("/", [](){
        sendWebPage();
    });
  
    server.on("/on", [](){
        lightOn();
        sendWebPage();
    });

    server.on("/off", [](){
        lightOff();
        sendWebPage();
    });

    server.begin();
    Serial.println("HTTP server started");

    
    //Setup Over The Air (OTA) reprogramming
    ArduinoOTA.setHostname(hostName);
    ArduinoOTA.setPassword(otaPassword);
    
    ArduinoOTA.onStart([]() {
        Serial.end();
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
          type = "sketch";
        else // U_SPIFFS
          type = "filesystem";
    
        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    });
    ArduinoOTA.onEnd([]() {  });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {  });
    ArduinoOTA.onError([](ota_error_t error) {  });
    ArduinoOTA.begin();
    
    //Setup MQTT
    client.setServer(mqttServer, mqttPort);
    client.setCallback(mqttCallback);
    mqttReconnectTimer = 0;
    mqttConnect();
    
}
 
void loop()
{
    ESP.wdtFeed();
    delay(1);
    
    unsigned long currentTime = millis();

    // Handle WiFi
    if ( WiFi.status() != WL_CONNECTED ) {
        if ( currentTime - wifiReconnectTimer > 20000 )
            ESP.reset();
    } else
        wifiReconnectTimer = currentTime;

    // Handle MQTT
    if (!client.connected()) {
        if ( currentTime - mqttReconnectTimer > 5000 ) {
            mqttReconnectTimer = currentTime;
            if ( mqttConnect() ) {
                mqttReconnectTimer = 0;
            }
        }
    } else {
      client.loop();
    }

    // Handle Webserver Requests
    server.handleClient();
    // Handle OTA requests
    ArduinoOTA.handle();

    if ( debounceCounter == 0 && ( !digitalRead(BUTTON) || !digitalRead(EXTBUTTON) ) ) {
        if ( lightStatus ) {
            lightOff();
        } else {
            lightOn();
        }
        debounceCounter = 300;
    }
    if ( debounceCounter > 0 ) debounceCounter--;
}
