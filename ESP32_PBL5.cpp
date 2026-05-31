// Libraries
#include <WiFi.h>
#include <DHT.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

// cd /d D:\mosquitto
// mosquitto.exe -c mosquitto.conf -v

// Network Configuration
// 192.168.1.7
// const char* ssid = "DHBK P411 N5 5G";
// const char* password = "192837465";
const char* ssid = "DHBK P411 N5";
const char* password = "192837465";

// const char* ssid = "VP KTX DHBK 2.4G";
// const char* password = "0913402314";

// 172.20.10.7
// const char* ssid = "VyNghiax";
// const char* password = "123456789";

// 172.20.10.2
// const char* ssid = "Dyland";
// const char* password = "123456798";

const char* mqtt_server = "192.168.1.7";
const int mqtt_port = 1883;
const char* be_url = "http://192.168.1.7:5000/api/sensor";

// Identify The Pins (GPIO)
#define LIGHT_LIVING   32
#define LIGHT_BEDROOM  33
#define LIGHT_KITCHEN  25
#define LIGHT_HALLWAY1  26
#define LIGHT_HALLWAY2  13


#define FAN_LIVING     27 // IN1 Relay 1
#define FAN_BEDROOM    14 // IN2 Relay 1
#define FAN_KITCHEN    12 // IN1 Relay 2

#define LCD_SDA 21
#define LCD_SCL 22

#define DHT_PIN        5
#define DHT_TYPE DHT11
#define GAS_SENSOR     34
#define GAS_THRESHOLD  2000
#define BUZZER         23

#define DOOR_LIVING     19
#define DOOR_BEDROOM    18
#define DOOR_KITCHEN    17


DHT dht(DHT_PIN, DHT_TYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

Servo doorLiving, doorBedroom, doorKitchen;

SemaphoreHandle_t stateMutex;

// Device Status
bool livingLight = false, livingFan = false;
bool bedroomLight = false, bedroomFan = false;
bool kitchenLight = false, kitchenFan = false, kitchenFanGas = false;
bool hallwayLight = false;
bool livingDoorOpen = false, bedroomDoorOpen = false, kitchenDoorOpen = false;
bool stateChanged = false;
volatile bool sensorUpdated = false;

float temperature = 0, humidity = 0;
int gasValue = 0;
unsigned long lastSensorRead = 0;
const long sensorInterval = 1000; 
float lastTemp = 0;
float lastHum = 0;
int lastGas = 0;

// Server
WebServer server(80);
WebSocketsServer webSocket(81);
WiFiClient espClient;
PubSubClient mqtt(espClient);

void connectToWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    Serial.print(".");
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi");
  }
}

void reconnectMQTT() {
  static unsigned long lastAttempt = 0;

  if (mqtt.connected()) return;

  if (millis() - lastAttempt < 5000) return;
  lastAttempt = millis();

  Serial.print("Connecting to MQTT...");
  String clientId = "ESP32-" + String(random(0xffff), HEX);

  if (mqtt.connect(clientId.c_str())) {
    Serial.println("connected");
    mqtt.subscribe("home/+/+");
  } else {
    Serial.print("failed, rc=");
    Serial.println(mqtt.state());
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++)
    message += (char)payload[i];
  Serial.printf("Message arrived: [%s] %s\n", topic, message.c_str());

  String t = String(topic);
  int firstSlash = t.indexOf('/');
  int secondSlash = t.indexOf('/', firstSlash + 1);
  if(firstSlash == -1 || secondSlash == -1) {
    Serial.println("Invalid topic format");
    return;
  }
  String room = t.substring(firstSlash + 1, secondSlash);
  String device = t.substring(secondSlash + 1);
  message.toUpperCase();
  bool state = (message == "ON") ? HIGH : LOW;

  if(device == "light") {
    if(room == "living") controlLight(LIGHT_LIVING, livingLight, state);
    else if(room == "bedroom") controlLight(LIGHT_BEDROOM, bedroomLight, state);
    else if(room == "kitchen") controlLight(LIGHT_KITCHEN, kitchenLight, state);
    else if(room == "hallway") {
      controlLight(LIGHT_HALLWAY1, hallwayLight, state);
      hallwayLight = hallwayLight ? false : true;
      controlLight(LIGHT_HALLWAY2, hallwayLight, state);
    }
  } else if(device == "fan") {
    if(room == "living") controlFan(FAN_LIVING, livingFan, state);
    else if(room == "bedroom") controlFan(FAN_BEDROOM, bedroomFan, state);
    else if(room == "kitchen") controlFan(FAN_KITCHEN, kitchenFan, state);
  } else if(device == "door"){
    if(room == "living"){
      if(state == HIGH) openTheDoor(doorLiving, livingDoorOpen, true, DOOR_LIVING);
      else closeTheDoor(doorLiving, livingDoorOpen, false, DOOR_LIVING);
    }
    else if(room == "bedroom"){
      if(state == HIGH) openTheDoor(doorBedroom, bedroomDoorOpen, true, DOOR_BEDROOM);
      else closeTheDoor(doorBedroom, bedroomDoorOpen, false, DOOR_BEDROOM);
    }
    else if(room == "kitchen"){
      if(state == HIGH) openTheDoor(doorKitchen, kitchenDoorOpen, true, DOOR_KITCHEN);
      else closeTheDoor(doorKitchen, kitchenDoorOpen, false, DOOR_KITCHEN);
    }
  } 
  else {
    Serial.println("Unknown device type");
    return;
  }
}

void controlLight(int pin, bool &stateVar, bool newState) {
  if(xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
    if(stateVar != newState) {
      digitalWrite(pin, newState ? HIGH : LOW);
      stateVar = newState;
      stateChanged = true;
    }
    xSemaphoreGive(stateMutex);
  }
}

void controlFan(int pin, bool &stateVar, bool newState) {
  if(xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
    if(stateVar != newState) {
      digitalWrite(pin, newState ? HIGH : LOW);
      stateVar = newState;
      stateChanged = true;
    }
    xSemaphoreGive(stateMutex);
  }
}

void openTheDoor(Servo &door, bool &stateVar, bool newState, int doorPin) {
  bool needUpdate = false;

  if(xSemaphoreTake(stateMutex, portMAX_DELAY)) {
    if(stateVar != newState) {
      stateVar = newState;
      stateChanged = true;
      needUpdate = true;
    }
    xSemaphoreGive(stateMutex);
  }

  if(needUpdate) {
    door.attach(doorPin);
    door.write(30); 
    vTaskDelay(10 / portTICK_PERIOD_MS);
    door.detach();
  }
}

void closeTheDoor(Servo &door, bool &stateVar, bool newState, int doorPin) {
  bool needUpdate = false;

  if (xSemaphoreTake(stateMutex, portMAX_DELAY)) {
    if (stateVar != newState) {
      stateVar = newState;
      stateChanged = true;
      needUpdate = true;
    }
    xSemaphoreGive(stateMutex);
  }

  if (needUpdate) {
    door.attach(doorPin);
    door.write(115);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    door.detach();
  }
}

void updateLCD() {
  static unsigned long lastLCD = 0;

  if(millis() - lastLCD > 1000) {

    lcd.clear();

    if(gasValue > GAS_THRESHOLD) {
      lcd.setCursor(0, 0);
      lcd.print("GAS LEAK !!!");
      lcd.setCursor(0, 1);
      lcd.print("CHECK NOW !!!");
    } else {
      lcd.setCursor(0, 0);
      lcd.print("T:");
      lcd.print(temperature, 1);
      lcd.print((char)223);
      lcd.print("C H:");
      lcd.print(humidity, 0);
      lcd.print("%");

      lcd.setCursor(0, 1);
      lcd.print("Gas:");
      lcd.print(gasValue);
    }

    lastLCD = millis();
  }
}

void readSensors() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int gas = analogRead(GAS_SENSOR);

  if (!isnan(h) && !isnan(t)) {
    temperature = t;
    humidity = h;
  }

  gasValue = gas;
  sensorUpdated = true;
  Serial.printf("Temperature: %.2f °C, Humidity: %.2f %%, Gas: %d\n", temperature, humidity, gasValue);


  if (gasValue > GAS_THRESHOLD) {
    tone(BUZZER, 500);

    if (!kitchenFan) {
      controlFan(FAN_KITCHEN, kitchenFan, true);
      kitchenFanGas = true;
      if(mqtt.connected()) {
        mqtt.publish("home/kitchen/fan", "ON");
      }
    }
  } else {
    noTone(BUZZER);

    if (kitchenFan && kitchenFanGas && gasValue < 1500) {
      controlFan(FAN_KITCHEN, kitchenFan, false);
      kitchenFanGas = false;
      if(mqtt.connected()){
        mqtt.publish("home/kitchen/fan", "OFF");
      }
    }
  }
}

void sendSensorData() {
  StaticJsonDocument<512> doc;
  doc["temperature"] = temperature;
  doc["humidity"] = humidity;
  doc["gas"] = gasValue;

  if(WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, cannot send data to backend");
    return;
  }

  HTTPClient http;
  http.begin(be_url);
  http.setTimeout(5000);
  http.addHeader("Content-Type", "application/json");

  String jsonString;
  serializeJson(doc, jsonString);

  int httpResponseCode = http.POST(jsonString);
  if(httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Response from backend: " + response);
  } else {
    Serial.println("Error sending data to backend: " + String(httpResponseCode));
  }
  http.end();
}

void broadcastDeviceStatus() {
  StaticJsonDocument<512> doc;
  doc["living"]["light"] = livingLight;
  doc["living"]["fan"] = livingFan;
  doc["bedroom"]["light"] = bedroomLight;
  doc["bedroom"]["fan"] = bedroomFan;
  doc["kitchen"]["light"] = kitchenLight;
  doc["kitchen"]["fan"] = kitchenFan;
  doc["hallway"]["light"] = hallwayLight;
  doc["living"]["door"] = livingDoorOpen;
  doc["bedroom"]["door"] = bedroomDoorOpen;
  doc["kitchen"]["door"] = kitchenDoorOpen;
  doc["sensors"]["temperature"] = temperature;
  doc["sensors"]["humidity"] = humidity;
  doc["sensors"]["gas"] = gasValue;

  String jsonString;
  serializeJson(doc, jsonString);
  webSocket.broadcastTXT(jsonString);
}

// Web Socket Event Handler
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("Client %u disconnected\n", num);
      break;
    case WStype_CONNECTED:
      Serial.printf("Client %u connected\n", num);
      break;
    case WStype_TEXT:
      Serial.printf("Client %u sent: %s\n", num, payload);
      controlDevice(payload);
      break;
  }
}

void controlDevice(uint8_t * payload) {
  StaticJsonDocument<256> doc;
  String jsonString = (char*)payload; 
  DeserializationError error = deserializeJson(doc, jsonString);
  if (error) {
    Serial.println("Invalid JSON received");
    return;
  }

  String room = doc["room"];
  String device = doc["device"];
  String action = doc["action"];
  action.toUpperCase();
  bool state = (action == "ON") ? HIGH : LOW;

  if(device == "light") {
    if(room == "living") controlLight(LIGHT_LIVING, livingLight, state);
    else if(room == "bedroom") controlLight(LIGHT_BEDROOM, bedroomLight, state);
    else if(room == "kitchen") controlLight(LIGHT_KITCHEN, kitchenLight, state);
    else if(room == "hallway") {
      controlLight(LIGHT_HALLWAY1, hallwayLight, state);
      hallwayLight = hallwayLight ? false : true;
      controlLight(LIGHT_HALLWAY2, hallwayLight, state);
    }
  } else if(device == "fan") {
    if(room == "living") controlFan(FAN_LIVING, livingFan, state);
    else if(room == "bedroom") controlFan(FAN_BEDROOM, bedroomFan, state);
    else if(room == "kitchen") controlFan(FAN_KITCHEN, kitchenFan, state);
  } else if(device == "door"){
    if(room == "living"){
      if(state == HIGH) openTheDoor(doorLiving, livingDoorOpen, true, DOOR_LIVING);
      else closeTheDoor(doorLiving, livingDoorOpen, false, DOOR_LIVING);
    }
    else if(room == "bedroom"){
      if(state == HIGH) openTheDoor(doorBedroom, bedroomDoorOpen, true, DOOR_BEDROOM);
      else closeTheDoor(doorBedroom, bedroomDoorOpen, false, DOOR_BEDROOM);
    }
    else if(room == "kitchen"){
      if(state == HIGH) openTheDoor(doorKitchen, kitchenDoorOpen, true, DOOR_KITCHEN);
      else closeTheDoor(doorKitchen, kitchenDoorOpen, false, DOOR_KITCHEN);
    }
  } else {
    Serial.println("Unknown device type");
    return;
  }

  if(mqtt.connected()) {
    String topic = "home/" + room + "/" + device;
    String message = state ? "ON" : "OFF";
    mqtt.publish(topic.c_str(), message.c_str());
  }
}

void handleControl(){
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if(!server.hasArg("room") || !server.hasArg("device") || !server.hasArg("action")) {
    server.send(400, "text/plain", "Missing room, device or state parameter");
    return;
  }

  String room = server.arg("room");
  String device = server.arg("device");
  String action = server.arg("action");
  action.toUpperCase();
  bool state = (action == "ON") ? HIGH : LOW;

  if(device == "light") {
    if(room == "living") controlLight(LIGHT_LIVING, livingLight, state);
    else if(room == "bedroom") controlLight(LIGHT_BEDROOM, bedroomLight, state);
    else if(room == "kitchen") controlLight(LIGHT_KITCHEN, kitchenLight, state);
    else if(room == "hallway") {
      controlLight(LIGHT_HALLWAY1, hallwayLight, state);
      hallwayLight = hallwayLight ? false : true;
      controlLight(LIGHT_HALLWAY2, hallwayLight, state);
    }
  } else if(device == "fan") {
    if(room == "living") controlFan(FAN_LIVING, livingFan, state);
    else if(room == "bedroom") controlFan(FAN_BEDROOM, bedroomFan, state);
    else if(room == "kitchen") controlFan(FAN_KITCHEN, kitchenFan, state);
  } else if(device == "door"){
    if(room == "living"){
      if(state == HIGH) openTheDoor(doorLiving, livingDoorOpen, true, DOOR_LIVING);
      else closeTheDoor(doorLiving, livingDoorOpen, false, DOOR_LIVING);
    }
    else if(room == "bedroom"){
      if(state == HIGH) openTheDoor(doorBedroom, bedroomDoorOpen, true, DOOR_BEDROOM);
      else closeTheDoor(doorBedroom, bedroomDoorOpen, false, DOOR_BEDROOM);
    }
    else if(room == "kitchen"){
      if(state == HIGH) openTheDoor(doorKitchen, kitchenDoorOpen, true, DOOR_KITCHEN);
      else closeTheDoor(doorKitchen, kitchenDoorOpen, false, DOOR_KITCHEN);
    }
  } else {
    server.send(400, "text/plain", "Unknown device type");
    return;
  } 
  server.send(200, "text/plain", "Device control successful");
}

void handleGetState(){
  server.sendHeader("Access-Control-Allow-Origin", "*");
  StaticJsonDocument<512> doc;
  doc["living"]["light"] = livingLight;
  doc["living"]["fan"] = livingFan;
  doc["bedroom"]["light"] = bedroomLight;
  doc["bedroom"]["fan"] = bedroomFan;
  doc["kitchen"]["light"] = kitchenLight;
  doc["kitchen"]["fan"] = kitchenFan;
  doc["hallway"]["light"] = hallwayLight;
  doc["living"]["door"] = livingDoorOpen;
  doc["bedroom"]["door"] = bedroomDoorOpen;
  doc["kitchen"]["door"] = kitchenDoorOpen;
  doc["sensors"]["temperature"] = temperature;
  doc["sensors"]["humidity"] = humidity;
  doc["sensors"]["gas"] = gasValue;

  String jsonString;
  serializeJson(doc, jsonString);
  server.send(200, "application/json", jsonString);
}

void handleRoot(){
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "Welcome to ESP32 Smart Home API!");
}

void setup() {
  Wire.begin(LCD_SDA, LCD_SCL);
  lcd.init();
  lcd.backlight();
  Serial.begin(115200);
  pinMode(LIGHT_LIVING, OUTPUT);
  pinMode(LIGHT_BEDROOM, OUTPUT);
  pinMode(LIGHT_KITCHEN, OUTPUT);
  pinMode(LIGHT_HALLWAY1, OUTPUT);
  pinMode(LIGHT_HALLWAY2, OUTPUT);
  pinMode(FAN_LIVING, OUTPUT);
  pinMode(FAN_BEDROOM, OUTPUT);
  pinMode(FAN_KITCHEN, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  doorLiving.setPeriodHertz(50);
  doorLiving.attach(DOOR_LIVING, 500, 2400);
  doorBedroom.setPeriodHertz(50);
  doorBedroom.attach(DOOR_BEDROOM, 500, 2400);
  doorKitchen.setPeriodHertz(50);
  doorKitchen.attach(DOOR_KITCHEN, 500, 2400);

  stateMutex = xSemaphoreCreateMutex();

  digitalWrite(LIGHT_LIVING, LOW);
  digitalWrite(LIGHT_BEDROOM, LOW);
  digitalWrite(LIGHT_KITCHEN, LOW);
  digitalWrite(LIGHT_HALLWAY1, LOW);
  digitalWrite(LIGHT_HALLWAY2, LOW);
  digitalWrite(FAN_LIVING, LOW);
  digitalWrite(FAN_BEDROOM, LOW);
  digitalWrite(FAN_KITCHEN, LOW);

  // Connect to WiFi
  connectToWiFi();

  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setCallback(mqttCallback);

  server.on("/", handleRoot);
  server.on("/control", handleControl);
  server.on("/state", handleGetState);

  server.begin();
  Serial.println("HTTP server started!");

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket server started!");

  xTaskCreatePinnedToCore(
    TaskNetwork,
    "TaskNetwork",
    10000,
    NULL,
    1,
    NULL,
    0  
  );

  xTaskCreatePinnedToCore(
    TaskSensor,
    "TaskSensor",
    5000,
    NULL,
    1,
    NULL,
    1  
  );

  xTaskCreatePinnedToCore(
    TaskLCD,
    "TaskLCD",
    3000,
    NULL,
    1,
    NULL,
    1  
  );
  xTaskCreatePinnedToCore(
    TaskHTTP,
    "TaskHTTP",
    8000,
    NULL,
    1,
    NULL,
    1
  );
  xTaskCreatePinnedToCore(
    TaskBroadcast,
    "TaskBroadcast",
    4000,
    NULL,
    1,
    NULL,
    1
  );
}

void loop() {
  // Using FreeRTOS tasks
}


// Multi-threaded tasks
void TaskNetwork(void *pvParameters) {
  while (true) {
    if (WiFi.status() != WL_CONNECTED) {
      static unsigned long lastWifiTry = 0;
      if (millis() - lastWifiTry > 10000) {
        connectToWiFi();
        lastWifiTry = millis();
      }
    }

    if (!mqtt.connected()) {
      reconnectMQTT();
    }

    mqtt.loop();
    webSocket.loop();
    server.handleClient();

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void TaskSensor(void *pvParameters) {
  while (true) {
    readSensors();
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

void TaskLCD(void *pvParameters) {
  while (true) {
    if(sensorUpdated) {
      updateLCD();
    }
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}

void TaskHTTP(void *pvParameters){
  while(true){
    if(WiFi.status() == WL_CONNECTED) {
      sendSensorData();
    }
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}

void TaskBroadcast(void *pvParameters) {
  while(true) {
    if(sensorUpdated || stateChanged) {
      broadcastDeviceStatus();

      sensorUpdated = false;  
      stateChanged = false;
    }
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}