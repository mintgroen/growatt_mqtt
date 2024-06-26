#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <ModbusMaster.h>
#include <SoftwareSerial.h>
#include "secrets.h"

/******************************************************************
Secrets, change these in the secrets.h file
*******************************************************************/

const char* wifi_ssid                  = WIFI_SSID;
const char* wifi_password              = WIFI_PASSWORD;
const char* wifi_hostname              = WIFI_HOSTNAME;
const char* ota_password               = OTA_PASSWORD;

const char* mqtt_server                = MQTT_SERVER;
const int   mqtt_port                  = MQTT_PORT;
const char* mqtt_username              = MQTT_USERNAME;
const char* mqtt_password              = MQTT_PASSWORD;

const char* mqtt_topic_base            = MQTT_TOPIC_BASE;
const char* mqtt_log_topic             = MQTT_LOG_TOPIC;
const char* mqtt_lwt_topic             = MQTT_LWT_TOPIC;


/******************************************************************
Useful for debugging, outputs info to a separate mqtt topic
*******************************************************************/

const bool outputMqttLog = true;

#define MAXDATASIZE 256
char data[MAXDATASIZE];
int data_length = 0;

char log_msg[256];
char mqtt_topic[256];

bool whr_online = false;

/******************************************************************
Instantiate modbus and mqtt libraries
*******************************************************************/

ModbusMaster node;
int slave_id_growatt = 1;
WiFiClient mqtt_wifi_client;
PubSubClient mqtt_client(mqtt_wifi_client);

SoftwareSerial sConn;
#define SS_RX 12
#define SS_TX 13

/******************************************************************
Log debug to mqtt
*******************************************************************/

void log_message(char* string)
{
  if (outputMqttLog)
  {
    mqtt_client.publish(mqtt_log_topic, string); 
  } 
}

/******************************************************************************************
 Callback function that is called when a message has been pushed to one of your topics.
*******************************************************************************************/

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  char msg[length + 1];
  for (int i=0; i < length; i++) {
    msg[i] = (char)payload[i];
  }
  msg[length] = '\0';
}

/******************************************************************
Setup, only ran on application start
*******************************************************************/

void setup() {  
  Serial.begin(9600);
  sConn.begin(9600, SWSERIAL_8N1, SS_RX, SS_TX, false, 128);
  
  // instantiate modbusmaster with slave id 1 (growatt)
  node.begin(1, Serial);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_password);
  WiFi.hostname(wifi_hostname);
  
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    delay(5000);
    ESP.restart();
  }

  // Port defaults to 8266
  ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(wifi_hostname);

  // Set authentication
  ArduinoOTA.setPassword(ota_password);

  ArduinoOTA.onStart([]() {
  });
  ArduinoOTA.onEnd([]() {
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {

  });
  ArduinoOTA.onError([](ota_error_t error) {

  });
  ArduinoOTA.begin();

  mqtt_client.setServer(mqtt_server, mqtt_port);
  mqtt_client.setCallback(mqtt_callback);
    
}

/******************************************************************
Create float using values from multiple registers
*******************************************************************/

float glueFloat(unsigned int d1, unsigned int d0) {
  unsigned long t;
  t = d1 << 16;
  t += d0;

  float f;
  f = t;
  f = f / 10;
  return f;
}

/******************************************************************
Publish float var to specified sub topic
*******************************************************************/
void publishFloat(char * topic, float f) {
  String value_str = String(f, 1);
  char value_char[32] = "";
  value_str.toCharArray(value_char, 40);

  sprintf(mqtt_topic, "%s/%s", mqtt_topic_base, topic);
  
  mqtt_client.publish(mqtt_topic, value_char); 
}

/******************************************************************
Publish int var to specified sub topic
*******************************************************************/

void publishInt(char * topic, int i) {
  String value_str = String(i);
  char value_char[32] = "";
  value_str.toCharArray(value_char, 40);
  
  sprintf(mqtt_topic, "%s/%s", mqtt_topic_base, topic);
  
  mqtt_client.publish(mqtt_topic, value_char); 
}

/******************************************************************
Reconnect to mqtt broker if connection is lost and set Last Will
*******************************************************************/

void mqtt_reconnect()
{
  // Loop until we're reconnected
  while (!mqtt_client.connected()) 
  {
    if (mqtt_client.connect(wifi_hostname, mqtt_username, mqtt_password, mqtt_lwt_topic, 0, true, "offline")) {
        mqtt_client.publish(mqtt_lwt_topic, "online");  
      // do nothing
    } else {
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

/******************************************************************
Growatt
*******************************************************************/

void update_growatt() {
  static uint32_t i;
  uint8_t j, result;
  uint16_t data[6];
  
  i++;

  String tmp;
  char topic[40] = "";
  char value[40] = "";
  log_message("Trying to read inverter..");
  
  // instantiate modbusmaster with slave id growatt
  node.begin(slave_id_growatt, sConn);
  
  result = node.readInputRegisters(0, 57);                                                        // Change to max register +1 to be parsed
  // do something with data if read is successful
  if (result == node.ku8MBSuccess){
    log_message("success!");
    publishInt("status", node.getResponseBuffer(0));                                              // Inverter status: -1 off, 0 idle, 1 production, 3 error
    publishFloat("Ppv", glueFloat(node.getResponseBuffer(1), node.getResponseBuffer(2)));         // Pv total Power
    
    publishFloat("Ppv1", glueFloat(node.getResponseBuffer(5), node.getResponseBuffer(6)));        // Pv string 1 Power
    publishFloat("Ppv2", glueFloat(node.getResponseBuffer(9), node.getResponseBuffer(10)));       // Pv string 2 Power
    publishFloat("Vpv1", glueFloat(0, node.getResponseBuffer(3)));                                // Pv string 1 Voltage
    publishFloat("Vpv2", glueFloat(0, node.getResponseBuffer(7)));                                // Pv string 2 Voltage
      
    publishFloat("Vac1", glueFloat(0, node.getResponseBuffer(38)));                               // AC Phase 1 Voltage
    publishFloat("Vac2", glueFloat(0, node.getResponseBuffer(42)));                               // AC Phase 2 Voltage
    publishFloat("Vac3", glueFloat(0, node.getResponseBuffer(46)));                               // AC Phase 3 Voltage
        
    publishFloat("PacT", glueFloat(node.getResponseBuffer(35), node.getResponseBuffer(36)));      // AC total Power
    publishFloat("Pac1", glueFloat(node.getResponseBuffer(40), node.getResponseBuffer(41)));      // AC Phase 1 Power
    publishFloat("Pac2", glueFloat(node.getResponseBuffer(44), node.getResponseBuffer(45)));      // AC Phase 2 Power
    publishFloat("Pac3", glueFloat(node.getResponseBuffer(48), node.getResponseBuffer(49)));      // AC Phase 3 Power
    
    
    publishFloat("Eactoday", glueFloat(node.getResponseBuffer(53), node.getResponseBuffer(54)));  // Energy generated today
    publishFloat("Eactotal", glueFloat(node.getResponseBuffer(55), node.getResponseBuffer(56)));  // Energy generated total

    //publishFloat("inv1", glueFloat(0, node.getResponseBuffer(93)));                                // Inverter temperature
    //publishFloat("ipmT", glueFloat(0, node.getResponseBuffer(94)));                                // Inside IPM inverter temperature
    //publishFloat("BoostT", glueFloat(0, node.getResponseBuffer(95)));                              // Boost temperature
    //publishFloat("Pbus", glueFloat(0, node.getResponseBuffer(98)));                                // Pbus temperature
    //publishFloat("Nbus", glueFloat(0, node.getResponseBuffer(99)));                                // Nbus temperature

    } else {
    tmp = String(result, HEX);
    tmp.toCharArray(value, 40);
     
    log_message("error!");
    log_message(value);
    publishInt("status", -1);
  }
  node.clearResponseBuffer();
}


/******************************************************************
Application keeps doing this after running setup
*******************************************************************/

typedef void (* GenericFP)();
const uint num_functions = 1;
GenericFP fnArr[num_functions] = {&update_growatt};
unsigned long next_poll = 0;

void loop() {
  // Handle OTA first.
  ArduinoOTA.handle();

  if (!mqtt_client.connected())
  {
    mqtt_reconnect();
  }
  mqtt_client.loop();
  
  if(millis() >= next_poll){
    for (int i = 0; i < num_functions; i++)
    {
      fnArr[i]();
      mqtt_client.loop();
    }
    next_poll = millis() + 10000;
  }
}
