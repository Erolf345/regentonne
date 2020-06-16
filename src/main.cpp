#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <MS5xxx.h>
#include <auth.h>
#include <PubSubClient.h>
#include <string.h>

char ssid[] = SSID;          //  network SSID (supplied in auth.h)
char pass[] = WIFI_PASSWORD; // network password

#define IRRIGATION_SWITCH_PIN 34
#define IRRIGATION_SWITCH_CHANNEL 2
#define IRRIGATION_PUMP_PIN 32
#define TANK_PUMP_PIN 33
#define IRRIGATION_PUMP_CHANNEL 0
#define TANK_PUMP_CHANNEL 1
#define BATTERY_PIN 35
#define MASTER_SENSOR_PIN 27

const char *mqtt_server = "192.168.178.97";

//Baro stuff
MS5xxx tonne_unten(&Wire);
MS5xxx tonne_oben(&Wire);
unsigned long baro_update_interval = 1000 * 60 * 60; //60 minutes
unsigned long last_update;

// setting PWM properties
const int pwm_freq = 5000;
const int pwm_resolution = 8;

WiFiClient espClient;
PubSubClient client(espClient);

void connectToNetwork()
{
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.println("Establishing connection to WiFi..");
  }

  Serial.println("Connected to network");
}

void reconnect()
{
  // Loop until we're reconnected
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str()))
    {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("Regentonne/log", "hello world");
      // ... and resubscribe
      client.subscribe("Regentonne/cmnd");
      client.subscribe("Regentonne/cmnd/pump");
      client.subscribe("Regentonne/cmnd/water");
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void pumpe_anschalten()
{
  Serial.println("pumpe ANGESCHALTET---------------------------------");
  client.publish("Regentonne/tank_pump", "PUMP ON");
  ledcWrite(TANK_PUMP_CHANNEL, 252);
}

void pumpe_ausschalten()
{
  Serial.println("pumpe AUSGESCHALTET---------------------------------");
  client.publish("Regentonne/tank_pump", "PUMP OFF");
  ledcWrite(TANK_PUMP_CHANNEL, 0);
}

void wasser_anschalten(int override)
{
  if (override >= 0 && override <= 255)
  {
    Serial.println("Wasser ANGESCHALTET---------------------------------");
    client.publish("Regentonne/water", "WATER ON");
    ledcWrite(IRRIGATION_PUMP_CHANNEL, 252);
    ledcWrite(IRRIGATION_SWITCH_CHANNEL, override);
  }
  else
  {
    Serial.println("Wasser ANGESCHALTET---------------------------------");
    client.publish("Regentonne/water", "WATER ON");
    ledcWrite(IRRIGATION_PUMP_CHANNEL, 252);
    ledcWrite(IRRIGATION_SWITCH_CHANNEL, 252);
  }
}

void wasser_ausschalten()
{
  Serial.println("Wasser AUSGESCHALTET---------------------------------");
  client.publish("Regentonne/water", "WATER OFF");
  ledcWrite(IRRIGATION_PUMP_CHANNEL, 0);
  ledcWrite(IRRIGATION_SWITCH_CHANNEL, 0);
}

/*
* Read Barometer and publish values to MQTT
*/
void read_baro()
{
  digitalWrite(MASTER_SENSOR_PIN, HIGH);
  delay(3000);

  //Tonne Unten
  Serial.println("Untere Tonne:");
  tonne_unten.ReadProm();
  tonne_unten.Readout();

  double temp = tonne_unten.GetTemp() / 100;
  char tempmes[200];
  sprintf(tempmes, "%2.2f", temp);
  Serial.print("Temperature °C: ");
  Serial.println(temp);
  client.publish("Regentonne/tonne_unten_temp", tempmes);

  double pres = tonne_unten.GetPres();
  char presmes[200];
  sprintf(presmes, "%2.0f", pres);
  Serial.print("Pressure [Pa]: ");
  Serial.println(pres);
  client.publish("Regentonne/tonne_unten", presmes);

    //Tonne Oben
  Serial.println("Obere Tonne:");
  tonne_oben.ReadProm();
  tonne_oben.Readout();

  temp = tonne_oben.GetTemp() / 100;
  sprintf(tempmes, "%2.2f", temp);
  Serial.print("Temperature °C: ");
  Serial.println(temp);
  client.publish("Regentonne/tonne_oben_temp", tempmes);

  pres = tonne_oben.GetPres();
  sprintf(presmes, "%2.0f", pres);
  Serial.print("Pressure [Pa]: ");
  Serial.println(pres);
  client.publish("Regentonne/tonne_oben", presmes);

  Serial.println("---");
  last_update = millis();
  digitalWrite(MASTER_SENSOR_PIN, LOW);
}

/*
* Read battery voltage and update MQTT
* ESP has 4096 levels of analog Read
* We dampen the battery signal so the esp is not damaged
* 
* Measured: 12.18V at around 2465
* 
* 
*/
void read_voltage()
{
  double toVolt = 12.18 / 2465;

  int measuredAnalog = analogRead(BATTERY_PIN);

  double voltage = measuredAnalog * toVolt;

  char voltmes[200];
  sprintf(voltmes, "%.2f", voltage);
  Serial.print("Voltage: ");
  client.publish("Regentonne/battery", voltmes);
  Serial.println(voltmes);
}

//Callback function for MQTT client
void callback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Command arrived [");
  Serial.print(topic);
  Serial.print("] ");

  char cmnd[length];

  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
    cmnd[i] = (char)payload[i];
  }
  Serial.println();

  if (strcmp(topic, "Regentonne/cmnd/pump") == 0)
  {
    if (strncmp("ON", cmnd, length) == 0)
    {
      pumpe_anschalten();
    }

    if (strncmp("OFF", cmnd, length) == 0)
    {
      pumpe_ausschalten();
    }
  }
  else if (strcmp(topic, "Regentonne/cmnd/water") == 0)
  {
    if (strncmp("OFF", cmnd, length) == 0)
    {
      wasser_ausschalten();
    }
    else if (strncmp("ON", cmnd, length) == 0)
    {
      wasser_anschalten(-1);
    }
    else
    {
      int val = std::atoi(cmnd);
      wasser_anschalten(val);
    }
  }
  else if (strcmp(topic, "Regentonne/cmnd") == 0)
  {
    if (strncmp("updateBaro", cmnd, length) == 0)
      read_baro();
    if (strncmp("updateBattery", cmnd, length) == 0)
      read_voltage();
  }
}

void setup()
{
  Serial.begin(115200);

  //Setup PWM Parameters
  ledcSetup(TANK_PUMP_CHANNEL, pwm_freq, pwm_resolution);
  ledcSetup(IRRIGATION_PUMP_CHANNEL, pwm_freq, pwm_resolution);
  ledcSetup(IRRIGATION_SWITCH_CHANNEL, pwm_freq, pwm_resolution);
  ledcAttachPin(TANK_PUMP_PIN, TANK_PUMP_CHANNEL);
  ledcAttachPin(IRRIGATION_PUMP_PIN, IRRIGATION_PUMP_CHANNEL);
  ledcAttachPin(IRRIGATION_SWITCH_PIN, IRRIGATION_SWITCH_CHANNEL);

  //Master Sensor pin
  pinMode(MASTER_SENSOR_PIN, OUTPUT);
  digitalWrite(MASTER_SENSOR_PIN, LOW);

  //Setup barometers
  tonne_unten.setI2Caddr(0x76);

  if (tonne_unten.connect() > 0)
  {
    Serial.println("Error connecting to tonne_unten barometer");
  }

  tonne_oben.setI2Caddr(0x77);
  if (tonne_oben.connect() > 0)
  {
    Serial.println("Error connecting to tonne_oben barometer");
  }
  read_baro();
  last_update = millis();

  //Setup MQTT
  //TODO: Set behaviour for failed connection
  connectToNetwork();
  client.setServer(mqtt_server, 1883);
  reconnect();
  client.setCallback(callback);
}

void loop()
{

  //MQTT-Handler
  if (!client.connected())
  {
    reconnect();
  }
  client.loop();

  //Barometer readout
  if (last_update + baro_update_interval <= millis())
    read_baro();

  delay(500);
}