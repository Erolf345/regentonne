#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <MS5xxx.h>
#include <auth.h>
#include <PubSubClient.h>
#include <string.h>
#include <ArduinoOTA.h>

char ssid[] = SSID;          //  network SSID (supplied in auth.h)
char pass[] = WIFI_PASSWORD; // network password

#define IRRIGATION_SWITCH_PIN 34
#define IRRIGATION_SWITCH_CHANNEL 2
#define IRRIGATION_PUMP_PIN 32
#define TANK_PUMP_PIN 33
#define IRRIGATION_PUMP_CHANNEL 0
#define TANK_PUMP_CHANNEL 1
#define BATTERY_PIN 35

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

void read_baro()
{
  tonne_unten.ReadProm();
  tonne_unten.Readout();

  double temp = tonne_unten.GetTemp() / 100;
  char tempmes[200];
  sprintf(tempmes, "%2.2f", temp);
  client.publish("Regentonne/tonne_unten_temp", tempmes);
  Serial.print("Temperature °C: ");
  Serial.println(temp);

  double pres = tonne_unten.GetPres();
  char presmes[200];
  sprintf(presmes, "%2.0f", pres);
  Serial.print("Pressure [Pa]: ");
  client.publish("Regentonne/tonne_unten", presmes);

  Serial.println(pres);

  Serial.println("---");
  last_update = millis();
}

void read_voltage()
{
  int voltage = analogRead(BATTERY_PIN);
  char voltmes[200];
  sprintf(voltmes, "%2.0d", voltage);
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
    else
    {
      int val = std::atoi(cmnd);
      pumpe_anschalten();
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

  //Setup barometers
  tonne_unten.setI2Caddr(0x77);

  if (tonne_unten.connect() > 0)
  {
    Serial.println("Error connecting to tonne_unten barometer");
  }

  tonne_oben.setI2Caddr(0x76);
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

  //Setup OTA

  ArduinoOTA
      .onStart([]() {
        String type = "sketch";
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
        if (error == OTA_AUTH_ERROR)
          Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR)
          Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR)
          Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR)
          Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR)
          Serial.println("End Failed");
      });
}

void loop()
{
  ArduinoOTA.handle();

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