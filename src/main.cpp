#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <MS5xxx.h>
#include <auth.h>
#include <PubSubClient.h>
#include <string.h>

char ssid[] = SSID;          //  network SSID (supplied in auth.h)
char pass[] = WIFI_PASSWORD; // network password

#define IRRIGATION_SWITCH_PIN 17
#define IRRIGATION_SWITCH_CHANNEL 2
#define IRRIGATION_PUMP_PIN 32
#define TANK_PUMP_PIN 33
#define IRRIGATION_PUMP_CHANNEL 0
#define TANK_PUMP_CHANNEL 1
#define BATTERY_PIN 35
#define MASTER_SENSOR_PIN 26

const char *mqtt_server = "192.168.178.97";

//Baro stuff
MS5xxx tonne_unten(&Wire);
MS5xxx tonne_oben(&Wire);

// setting PWM properties
const int pwm_freq = 5000;
const int pwm_resolution = 8;

WiFiClient espClient;
PubSubClient client(espClient);

// convert raw measured value to Volt * Measured: 12.18V at around 2465
double toVolt = 12.18 / 2465;

/**
* @brief Connect to WIFI
*/
void connectToNetwork()
{
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED)
  {
    WiFi.begin(ssid, pass);
    delay(1000);
    Serial.println("Establishing connection to WiFi..");
  }

  Serial.println("Connected to network");
}

/**
* @brief Reconnect with MQTT service
*/
void reconnect()
{
  // Loop until we're reconnected
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP32-Regentonne";
    // Attempt to connect
    if (client.connect(clientId.c_str(), NULL, NULL, 0, 0, 0, 0, 0)) //Persistent session
    {
      Serial.println("connected");
      // Once connected, publish an announcement...
      //client.publish("Regentonne/log", "hello world");
      // ... and resubscribe
      client.subscribe("Regentonne/cmnd/pump", 1);
      client.subscribe("Regentonne/cmnd/water", 1);
      client.subscribe("Regentonne/cmnd", 1);
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
/**
 * @brief activate irrigation
 * 
 */
void wasser_anschalten(int override)
{
  if (override >= 0 && override <= 255)
  {
    Serial.print("Wasser ANGESCHALTET Mit Schalterwert: ");
    Serial.println(override);
    client.publish("Regentonne/water", "WATER ON");
    ledcWrite(IRRIGATION_PUMP_CHANNEL, 252);
    ledcWrite(IRRIGATION_SWITCH_CHANNEL, override);
  }
  else
  {
    Serial.println("Wasser ANGESCHALTET mit Schalterwert 252");
    client.publish("Regentonne/water", "WATER ON");
    ledcWrite(IRRIGATION_PUMP_CHANNEL, 252);
    ledcWrite(IRRIGATION_SWITCH_CHANNEL, 252);
  }
}

/**
 * @brief disable irrigation
 * 
 */
void wasser_ausschalten()
{
  Serial.println("Wasser AUSGESCHALTET---------------------------------");
  client.publish("Regentonne/water", "WATER OFF");
  ledcWrite(IRRIGATION_PUMP_CHANNEL, 0);
  ledcWrite(IRRIGATION_SWITCH_CHANNEL, 0);
}

/**
 * @brief Read Barometer and publish values to MQTT
 * 
 */
void read_baro()
{
  digitalWrite(MASTER_SENSOR_PIN, HIGH);
  delay(100);
  //Tonne Unten
  Serial.println("Untere Tonne:");
  tonne_unten.ReadProm();
  tonne_unten.Readout();

  double temp = tonne_unten.GetTemp() / 100;
  char tempmes[200];
  sprintf(tempmes, "%.2f", temp);
  Serial.print("Temperature °C: ");
  Serial.println(temp);
  client.publish("Regentonne/tonne_unten_temp", tempmes);

  char presmes[200];
  double pres = tonne_unten.GetPres();
  sprintf(presmes, "%.0f", pres);
  client.publish("Regentonne/tonne_unten/pascal", presmes);
  pres = pres - 194564.0; //Subtract calibrated value (at BME = 950hPa)
  pres = pres / 9806.65;  //Convert to water meter
  sprintf(presmes, "%.2f", pres);
  Serial.print("Pressure [Pa]: ");
  Serial.println(pres);
  client.publish("Regentonne/tonne_unten", presmes);

  //Tonne Oben
  Serial.println("Obere Tonne:");
  tonne_oben.ReadProm();
  tonne_oben.Readout();

  temp = tonne_oben.GetTemp() / 100;
  sprintf(tempmes, "%.2f", temp);
  Serial.print("Temperature °C: ");
  Serial.println(temp);
  client.publish("Regentonne/tonne_oben_temp", tempmes);

  pres = tonne_oben.GetPres();
  sprintf(presmes, "%.0f", pres);
  client.publish("Regentonne/tonne_oben/pascal", presmes);
  pres = pres - 190040;  //Subtract calibrated value (at BME = 950hPa)
  pres = pres / 9806.65; //Convert to water meter
  sprintf(presmes, "%.2f", pres);
  Serial.print("Pressure [Pa]: ");
  Serial.println(pres);
  client.publish("Regentonne/tonne_oben", presmes);

  Serial.println("---");
  digitalWrite(MASTER_SENSOR_PIN, LOW);
}

/**
* @brief Read battery voltage and update MQTT
* 
* ESP has 4096 levels of analog Read
* We dampen the battery signal so the esp is not damaged
* 
* 
*/
void read_voltage()
{
  int measuredAnalog = analogRead(BATTERY_PIN);

  double voltage = measuredAnalog * toVolt;

  char voltmes[200];
  sprintf(voltmes, "%.2f", voltage);
  Serial.print("Voltage: ");
  client.publish("Regentonne/battery", voltmes);
  Serial.println(voltmes);
}
/**
 * @brief puts ESP into deepsleep if battery is under certain Voltage
 * 
 */
void batteryFailsafe()
{
  int measuredAnalog = 0;
  double voltage;

  for (int i = 0; i < 5; i++)
  {
    measuredAnalog += analogRead(BATTERY_PIN);
    delay(10);
  }
  measuredAnalog = measuredAnalog / 5;
  voltage = measuredAnalog * toVolt;
  if (voltage < 9.5)
  {
    Serial.print("Measured Battery Voltage ");
    Serial.print(voltage);
    Serial.println("V < 9.5V");
    Serial.println("Sleeping for 2 hours!");
    digitalWrite(MASTER_SENSOR_PIN, LOW); //Turn off sensor master pin
    pumpe_ausschalten();                  //Turn off pump
    wasser_ausschalten();                 //Turn off irrigation
    ESP.deepSleep(1000 * 60 * 60 * 2);    //sleep for 2 hours
  }
}

/**
* @brief Callback function for MQTT client
*/
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
    if (strncmp("ping", cmnd, length) == 0)
      client.publish("Regentonne/log", "pong");
  }
}

void setup()
{
  //Check if battery has enough voltage to proceed
  batteryFailsafe();

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

  //Setup MQTT
  //TODO: Set behaviour for failed connection
  connectToNetwork();
  client.setServer(mqtt_server, 1883);
  reconnect();
  client.setCallback(callback);
}

void loop()
{
  //Check if battery has enough voltage to proceed
  batteryFailsafe();

  //MQTT-Handler
  if (!client.connected())
  {
    reconnect();
  }
  client.loop();

  //Barometer readout
  read_baro();
  read_voltage();

  delay(500);
}