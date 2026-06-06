// LIBRARIES
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>         
#include <DHT.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <Preferences.h>

// CẤU HÌNH HIVEMQ CLOUD
const char* mqtt_server = "d123b7d09f38491c940b762f943305ca.s1.eu.hivemq.cloud";
const int mqtt_port = 8883; 
const char* mqtt_user = "nghiadev295";
const char* mqtt_pass = "Nghiadev295"; 

// Topic MQTT
const char* topic_control = "pbl5/nhom4/control";
const char* topic_status  = "pbl5/nhom4/status"; 

// Có thể không cần
const char* be_url = "http://192.168.1.7:5000/api/sensor"; 

// PIN DEFINITIONS 
#define LIGHT_LIVING   32
#define LIGHT_BEDROOM  33
#define LIGHT_KITCHEN  25
#define LIGHT_HALLWAY1 26
#define LIGHT_HALLWAY2 13

#define FAN_LIVING     27 // IN1 Relay 1
#define FAN_BEDROOM    14 // IN2 Relay 1
#define FAN_KITCHEN    12 // IN2 Relay 2

#define LCD_SDA 21
#define LCD_SCL 22

#define DHT_PIN        5
#define DHT_TYPE DHT11
#define GAS_SENSOR     34
#define GAS_THRESHOLD  2000
#define BUZZER         23

#define DOOR_LIVING    15
#define DOOR_BEDROOM   18
#define DOOR_KITCHEN   17

#define RESET_BUTTON_PIN 16

// OBJECTS 
DHT dht(DHT_PIN, DHT_TYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo doorLiving, doorBedroom, doorKitchen;

WebServer server(80);
WebSocketsServer webSocket(81);

// MQTT khai báo Object bảo mật
WiFiClientSecure espClient; 
PubSubClient mqttClient(espClient);

// SAVE STATE (dùng cho khi ESP bị reset đột ngột, không mất trạng thái thiết bị)
Preferences preferences;
volatile bool needSave = false;
SemaphoreHandle_t saveMutex;

SemaphoreHandle_t stateMutex;

// DEVICE STATES 
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

// Khai báo hàm 
void connectToWiFi();
void resetWiFiAndRestart();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void controlLight(int pin, bool &stateVar, bool newState);
void controlFan(int pin, bool &stateVar, bool newState);
void openTheDoor(Servo &door, bool &stateVar, bool newState, int doorPin);
void closeTheDoor(Servo &door, bool &stateVar, bool newState, int doorPin);
void readSensors();
void updateLCD();
void sendSensorData();
void broadcastDeviceStatus();
void controlDevice(uint8_t * payload);
void saveStates();
void loadStates();
void restoreDeviceStates();
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
void handleControl();
void handleGetState();
void handleResetWiFi();
void handleRoot();
void TaskNetwork(void *pvParameters);
void TaskSensor(void *pvParameters);
void TaskLCD(void *pvParameters);
void TaskHTTP(void *pvParameters);
void TaskBroadcast(void *pvParameters);
void TaskButtonCheck(void *pvParameters);
void TaskSaveState(void *pvParameters);

// WIFI CONNECTION WITH WIFIMANAGER 
void connectToWiFi() {
    WiFiManager wm;
    // wm.setConfigPortalTimeout(180); 

    bool res = wm.autoConnect("SmartHome_AP", "12345678");

    if (!res) {
        Serial.println("Failed to connect WiFi or Timeout. Restarting...");
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        ESP.restart();
    }

    Serial.println("\nWiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("IP Gateway: ");
    Serial.println(WiFi.gatewayIP());
}

// RESET WIFI FUNCTION 
void resetWiFiAndRestart() {
    Serial.println("Resetting WiFi credentials and restarting...");
    saveStates();
    WiFiManager wm;
    wm.resetSettings();  
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP.restart();
}

// MQTT CALLBACK NHẬN LỆNH TỪ CLOUD
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    Serial.printf("MQTT NHẬN LỆNH TỪ CLOUD [%s]: ", topic);
    String message = "";
    for (int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    Serial.println(message);

    controlDevice((uint8_t *)message.c_str());
}

// DEVICE CONTROL FUNCTIONS 
void controlLight(int pin, bool &stateVar, bool newState) {
    if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
        if (stateVar != newState) {
            digitalWrite(pin, newState ? HIGH : LOW);
            stateVar = newState;
            stateChanged = true;
            needSave = true;
        }
        xSemaphoreGive(stateMutex);
    }
}

void controlFan(int pin, bool &stateVar, bool newState) {
    if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
        if (stateVar != newState) {
            digitalWrite(pin, newState ? HIGH : LOW);
            stateVar = newState;
            stateChanged = true;
            needSave = true;
        }
        xSemaphoreGive(stateMutex);
    }
}

void openTheDoor(Servo &door, bool &stateVar, bool newState, int doorPin) {
    bool needUpdate = false;
    if (xSemaphoreTake(stateMutex, portMAX_DELAY)) {
        if (stateVar != newState) {
            stateVar = newState;
            stateChanged = true;
            needUpdate = true;
            needSave = true;
        }
        xSemaphoreGive(stateMutex);
    }
    if (needUpdate) {
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
            needSave = true;
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

// SENSOR READING 
void readSensors() {
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    int gas = analogRead(GAS_SENSOR);

    if (!isnan(h) && !isnan(t)) {
        temperature = t;
        humidity = h;
    }
    
    gasValue = (gasValue + gas) / 2;
    sensorUpdated = true;

    Serial.printf("Temperature: %.2f °C, Humidity: %.2f %%, Gas: %d\n", temperature, humidity, gasValue);

    if (gasValue > GAS_THRESHOLD) {
        tone(BUZZER, 500);
        if (!kitchenFan) {
            controlFan(FAN_KITCHEN, kitchenFan, true);
            kitchenFanGas = true;
        }
    } else {
        noTone(BUZZER);
        if (kitchenFan && kitchenFanGas && gasValue < 1500) {
            controlFan(FAN_KITCHEN, kitchenFan, false);
            kitchenFanGas = false;
        }
    }
}

// LCD UPDATE 
void updateLCD() {
    static unsigned long lastLCD = 0;
    if (millis() - lastLCD > 1000) {
        lcd.clear();
        if (gasValue > GAS_THRESHOLD) {
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

// SEND DATA TO LOCAL BACKEND HTTP 
// Chỉ chạy được nếu ESP chung LAN với 192.168.1.7
void sendSensorData() {
    if (WiFi.status() != WL_CONNECTED) 
        return;

    HTTPClient http;
    http.begin(be_url);
    http.setTimeout(2000);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<200> doc;
    doc["temperature"] = temperature;
    doc["humidity"] = humidity;
    doc["gas"] = gasValue;

    String json;
    serializeJson(doc, json);

    int httpResponseCode = http.POST(json);
    http.end();
}

// WEBSOCKET & CLOUD MQTT BROADCAST
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
    
    // Gửi ra mạng nội bộ
    webSocket.broadcastTXT(jsonString);

    // Gửi lên HiveMQ Cloud
    if (mqttClient.connected()) {
        mqttClient.publish(topic_status, jsonString.c_str());
    }
}

// CONTROL DEVICE VIA WEBSOCKET OR MQTT
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

    if (device == "light") {
        if (room == "living") 
            controlLight(LIGHT_LIVING, livingLight, state);
        else if (room == "bedroom") 
            controlLight(LIGHT_BEDROOM, bedroomLight, state);
        else if (room == "kitchen") 
            controlLight(LIGHT_KITCHEN, kitchenLight, state);
        else if (room == "hallway") {
            controlLight(LIGHT_HALLWAY1, hallwayLight, state);
            digitalWrite(LIGHT_HALLWAY2, state);
        }
    } else if (device == "fan") {
        if (room == "living") 
            controlFan(FAN_LIVING, livingFan, state);
        else if (room == "bedroom") 
            controlFan(FAN_BEDROOM, bedroomFan, state);
        else if (room == "kitchen") 
            controlFan(FAN_KITCHEN, kitchenFan, state);
    } else if (device == "door") {
        if (room == "living") {
            if (state == HIGH) openTheDoor(doorLiving, livingDoorOpen, true, DOOR_LIVING);
            else closeTheDoor(doorLiving, livingDoorOpen, false, DOOR_LIVING);
        } else if (room == "bedroom") {
            if (state == HIGH) openTheDoor(doorBedroom, bedroomDoorOpen, true, DOOR_BEDROOM);
            else closeTheDoor(doorBedroom, bedroomDoorOpen, false, DOOR_BEDROOM);
        } else if (room == "kitchen") {
            if (state == HIGH) openTheDoor(doorKitchen, kitchenDoorOpen, true, DOOR_KITCHEN);
            else closeTheDoor(doorKitchen, kitchenDoorOpen, false, DOOR_KITCHEN);
        }
    }
}

// SAVE STATE
void saveStates() {
   bool livLight, livFan, livDoor, bedLight, bedFan, bebDoor, kitLight, kitFan, kitDoor, hallLight;
   if(xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE){
        livLight = livingLight;
        livFan = livingFan;
        livDoor = livingDoorOpen;
        bedLight = bedroomLight;
        bedFan = bedroomFan;
        bebDoor = bedroomDoorOpen;
        kitLight = kitchenLight;
        kitFan = kitchenFan;
        kitDoor = kitchenDoorOpen;
        hallLight = hallwayLight;
        xSemaphoreGive(stateMutex);
   } else {
        Serial.println("Failed to take stateMutex for saving states");
        return;
   }

   if(xSemaphoreTake(saveMutex, portMAX_DELAY) == pdTRUE){
        preferences.begin("smart_home", false);
        preferences.putBool("livLight", livLight);
        preferences.putBool("livFan", livFan);
        preferences.putBool("livDoor", livDoor);
        preferences.putBool("bedLight", bedLight);
        preferences.putBool("bedFan", bedFan);
        preferences.putBool("bebDoor", bebDoor);
        preferences.putBool("kitLight", kitLight);
        preferences.putBool("kitFan", kitFan);
        preferences.putBool("kitDoor", kitDoor);
        preferences.putBool("hallLight", hallLight);
        preferences.end();
        xSemaphoreGive(saveMutex);
   } else {
        Serial.println("Failed to take saveMutex for saving states");
   }
} 

void loadStates(){
    preferences.begin("smart_home", true);
    livingLight = preferences.getBool("livLight", false);
    livingFan = preferences.getBool("livFan", false);
    livingDoorOpen = preferences.getBool("livDoor", false);
    bedroomLight = preferences.getBool("bedLight", false);
    bedroomFan = preferences.getBool("bedFan", false);
    bedroomDoorOpen = preferences.getBool("bebDoor", false);
    kitchenLight = preferences.getBool("kitLight", false);
    kitchenFan = preferences.getBool("kitFan", false);
    kitchenDoorOpen = preferences.getBool("kitDoor", false);
    hallwayLight = preferences.getBool("hallLight", false);
    preferences.end();

}

void restoreDeviceStates(){
    digitalWrite(LIGHT_LIVING, livingLight);
    digitalWrite(LIGHT_BEDROOM, bedroomLight);
    digitalWrite(LIGHT_KITCHEN, kitchenLight);

    digitalWrite(LIGHT_HALLWAY1, hallwayLight);
    digitalWrite(LIGHT_HALLWAY2, hallwayLight);

    digitalWrite(FAN_LIVING, livingFan);
    digitalWrite(FAN_BEDROOM, bedroomFan);
    digitalWrite(FAN_KITCHEN, kitchenFan);

    doorLiving.write(
        livingDoorOpen ? 30 : 115
    );

    doorBedroom.write(
        bedroomDoorOpen ? 30 : 115
    );

    doorKitchen.write(
        kitchenDoorOpen ? 30 : 115
    );

    vTaskDelay(500 / portTICK_PERIOD_MS);

    doorLiving.detach();
    doorBedroom.detach();
    doorKitchen.detach();
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch (type) {
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

// HTTP HANDLERS
void handleControl() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (!server.hasArg("room") || !server.hasArg("device") || !server.hasArg("action")) {
        server.send(400, "text/plain", "Missing parameters");
        return;
    }
    
    String action = server.arg("action"); action.toUpperCase();
    String json_cmd = "{\"room\":\"" + server.arg("room") + "\",\"device\":\"" + server.arg("device") + "\",\"action\":\"" + action + "\"}";
    controlDevice((uint8_t*)json_cmd.c_str());
    server.send(200, "text/plain", "Device control successful");
}

void handleGetState() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"status\":\"ok\"}"); // Giữ nguyên rút gọn
}

void handleResetWiFi() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "Resetting WiFi credentials...");
    vTaskDelay(500 / portTICK_PERIOD_MS);
    resetWiFiAndRestart();
}

void handleRoot() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "ESP32 Smart Home - HiveMQ Cloud Enabled");
}

// [MQTT] FREERTOS NETWORK TASK 
void TaskNetwork(void *pvParameters) {
    static unsigned long lastWifiTry = 0;
    static unsigned long lastMqttTry = 0;

    while (true) {
        // KIỂM TRA MẠNG VÀ TỰ KẾT NỐI LẠI NGẦM
        if (WiFi.status() != WL_CONNECTED) {
            if (millis() - lastWifiTry > 10000) {
                Serial.println("Mất mạng! Đang thử kết nối lại...");
                WiFi.disconnect();
                WiFi.reconnect(); 
                lastWifiTry = millis();
            }
        } 
        // NẾU CÓ MẠNG THÌ DUY TRÌ HIVEMQ CLOUD
        else {
            if (!mqttClient.connected()) {
                if (millis() - lastMqttTry > 5000) {
                    Serial.print("Đang kết nối tới HiveMQ Cloud...");
                    
                    // Tạo ID Client ngẫu nhiên để tránh đụng độ
                    String clientId = "ESP32-SmartHome-";
                    clientId += String(random(0xffff), HEX);
                    
                    if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
                        Serial.println(" Thành Công!");
                        mqttClient.subscribe(topic_control); // Lắng nghe App gửi xuống
                        broadcastDeviceStatus();
                    } else {
                        Serial.print(" Lỗi rc=");
                        Serial.println(mqttClient.state());
                    }
                    lastMqttTry = millis();
                }
            } else {
                mqttClient.loop(); // Lắng nghe gói tin từ Cloud
            }
        }
        
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
        if (sensorUpdated) 
            updateLCD(); 
        vTaskDelay(200 / portTICK_PERIOD_MS); 
    }
}

void TaskHTTP(void *pvParameters) {
    while (true) {
        if (WiFi.status() == WL_CONNECTED) 
            sendSensorData();
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void TaskBroadcast(void *pvParameters) {
    while (true) {
        if (sensorUpdated || stateChanged) {
            broadcastDeviceStatus();
            sensorUpdated = false;
            stateChanged = false;
        }
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}

void TaskButtonCheck(void *pvParameters) {
    pinMode(RESET_BUTTON_PIN, INPUT_PULLUP); 

    unsigned long lastPressTime = 0;
    const unsigned long holdTime = 3000;

    while (true) {
        // Serial.print("Button reset WiFi: ");
        // Serial.println(digitalRead(RESET_BUTTON_PIN));
        if (digitalRead(RESET_BUTTON_PIN) == LOW) {
            // Debounce delay
            vTaskDelay(50 / portTICK_PERIOD_MS);
            if (lastPressTime == 0) {
                lastPressTime = millis();
            } else if (millis() - lastPressTime >= holdTime) {
                Serial.println("Reset button held for 3 seconds. Resetting WiFi...");
                resetWiFiAndRestart();
            }
        } else {
            lastPressTime = 0;
        }
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

void TaskSaveState(void *pvParameters)
{
    static unsigned long lastSave = 0;
    while (true)
    {
        if (needSave)
        {
            if (millis() - lastSave > 500)
            {
                saveStates();
                needSave = false;
                lastSave = millis();
                Serial.println("States saved");
            }
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

// SETUP
void setup() {
    Serial.begin(115200);
    Wire.begin(LCD_SDA, LCD_SCL);
    lcd.init(); 
    lcd.backlight();
    
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
    saveMutex = xSemaphoreCreateMutex();

    loadStates();

    restoreDeviceStates();

    connectToWiFi();

    // Bỏ qua xác thực chứng chỉ (mất an toàn)
    espClient.setInsecure(); 
    
    // Khởi tạo MQTT với thông số của HiveMQ
    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(1024);

    server.on("/", handleRoot);
    server.on("/control", handleControl);
    server.on("/state", handleGetState);
    server.on("/resetwifi", handleResetWiFi);
    server.begin();
    Serial.println("HTTP server started");

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    // [QUAN TRỌNG] Tăng Stack của TaskNetwork lên 15000 vì kết nối TLS rất tốn RAM
    xTaskCreatePinnedToCore(
        TaskNetwork, 
        "TaskNetwork", 
        15000, 
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
    xTaskCreatePinnedToCore(
        TaskButtonCheck, 
        "TaskButton", 
        2000, 
        NULL, 
        1, 
        NULL, 
        1
    );
    xTaskCreatePinnedToCore(
        TaskSaveState,
        "TaskSaveState",
        4000,
        NULL,
        1,
        NULL,
        1
    );
}

void loop() {
    // Phiên bản này đã thêm tính năng
    // lưu trạng thái thiết bị vào bộ nhớ flash 
    // tránh khi ESP32 bị reset hay mất điện đột ngột
    // sau khi trở lại thì sẽ khôi phục lại trạng thái cũ
}