#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <ESP32Servo.h>

#define LIGHT_LIVING   32
#define LIGHT_BEDROOM  33
#define LIGHT_KITCHEN  25
#define LIGHT_HALLWAY1  26
#define LIGHT_HALLWAY2  13

#define FAN_LIVING     27 //in1 r1
#define FAN_BEDROOM    14 //in2 r1
#define FAN_KITCHEN    12 //in1 r2

#define LCD_SDA 21
#define LCD_SCL 22

#define DHT_PIN        5
#define GAS_SENSOR     34
#define GAS_THRESHOLD  1500
#define BUZZER         23

#define DOOR_LIVING     19
#define DOOR_BEDROOM    18
#define DOOR_KITCHEN    17

#define CURTAN_LIVING 15
#define CURTAN_BEDROOM 16

Servo doorLiving, doorBedroom, doorKitchen;
Servo curtainLiving, curtainBedroom;

#define DHT_TYPE DHT11

DHT dht(DHT_PIN, DHT_TYPE);

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ================= TRẠNG THÁI THIẾT BỊ =================
bool livingLight = false, livingFan = false;
bool bedroomLight = false, bedroomFan = false;
bool kitchenLight = false, kitchenFan = false;
bool hallwayLight = false;
bool curtainOpen = false;
bool curtainMoving = false;
unsigned long curtainMoveStart = 0;
int curtainTarget = -1;

// ================= SETUP =================
void setup() {
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

  curtainLiving.setPeriodHertz(50);
  curtainLiving.attach(CURTAN_LIVING, 500, 2400);
  curtainBedroom.setPeriodHertz(50);
  curtainBedroom.attach(CURTAN_BEDROOM, 500, 2400);

  digitalWrite(LIGHT_LIVING, LOW);
  digitalWrite(LIGHT_BEDROOM, LOW);
  digitalWrite(LIGHT_KITCHEN, LOW);
  digitalWrite(LIGHT_HALLWAY1, LOW);
  digitalWrite(LIGHT_HALLWAY2, LOW);
  digitalWrite(FAN_LIVING, LOW);
  digitalWrite(FAN_BEDROOM, LOW);
  digitalWrite(FAN_KITCHEN, LOW);


  // Khởi động DHT và LCD
  dht.begin();
  lcd.init();
  lcd.backlight();

}

void openTheDoor(Servo &door, int pin) {
  door.attach(pin);
  door.write(30);
  delay(10);
  door.detach();
}

void closeTheDoor(Servo &door, int pin) {
  door.attach(pin);
  door.write(115);
  delay(10);
  door.detach();
}

// ================= LOOP =================
void loop() {
  if(Serial.available() > 0){
    String str = Serial.readString();
    str.trim();
    str.toLowerCase();
    if(str == "batdenkhach"){
      digitalWrite(LIGHT_LIVING, HIGH);
      Serial.println("KHACH OK");
    }
    if(str == "batdenngu"){
      digitalWrite(LIGHT_BEDROOM, HIGH);
      Serial.println("NGU OK");
    }
    if(str == "batdenbep"){
      digitalWrite(LIGHT_KITCHEN, HIGH);
      Serial.println("BEP OK");
    }
    if(str == "bathanhlang"){
      digitalWrite(LIGHT_HALLWAY1, HIGH);
      digitalWrite(LIGHT_HALLWAY2, HIGH);
      Serial.println("HANH LANG OK");
    }
    if(str == "tatdenkhach"){
      digitalWrite(LIGHT_LIVING, LOW);
      Serial.println("TAT KHACH OK");
    }
    if(str == "tatdenngu"){
      digitalWrite(LIGHT_BEDROOM, LOW);
      Serial.println("TAT NGU OK");
    }
    if(str == "tatdenbep"){
      digitalWrite(LIGHT_KITCHEN, LOW);
      Serial.println("TAT BEP OK");
    }
    if(str == "tathanhlang"){
      digitalWrite(LIGHT_HALLWAY2, LOW);
      digitalWrite(LIGHT_HALLWAY1, LOW);
      Serial.println("TAT HANH LANG OK");
    }
    if(str == "tathetden"){
      digitalWrite(LIGHT_LIVING, LOW);
      digitalWrite(LIGHT_BEDROOM, LOW);
      digitalWrite(LIGHT_KITCHEN, LOW);
      digitalWrite(LIGHT_HALLWAY2, LOW);
      digitalWrite(LIGHT_HALLWAY1, LOW);
      Serial.println("TAT HET OK");
    }
    if(str == "bathetden"){
      digitalWrite(LIGHT_LIVING, HIGH);
      digitalWrite(LIGHT_BEDROOM, HIGH);
      digitalWrite(LIGHT_KITCHEN, HIGH);
      digitalWrite(LIGHT_HALLWAY2, HIGH);
      digitalWrite(LIGHT_HALLWAY1, HIGH);
      Serial.println("TAT HET OK");
    }
    if(str == "batquatngu"){
      digitalWrite(FAN_BEDROOM, HIGH);
      Serial.println("BAT QUAT NGU OK");
    }
    if(str == "batquatbep"){
      digitalWrite(FAN_KITCHEN, HIGH);
      Serial.println("BAT QUAT BEP OK");
    }
    if(str == "batquatkhach"){
      digitalWrite(FAN_LIVING, HIGH);
      Serial.println("BAT QUAT KHACH OK");
    }
    if(str == "tatquatngu"){
      digitalWrite(FAN_BEDROOM, LOW);
      Serial.println("TAT QUAT NGU OK");
    }
    if(str == "tatquatbep"){
      digitalWrite(FAN_KITCHEN, LOW);
      Serial.println("TAT QUAT BEP OK");
    }
    if(str == "tatquatkhach"){
      digitalWrite(FAN_LIVING, LOW);
      Serial.println("TAT QUAT KHACH OK");
    }
    if(str == "mocuaphongkhach"){
      openTheDoor(doorLiving, DOOR_LIVING);
      Serial.println("MO CUA PHONG KHACH OK");
    }
    if(str == "dongcuaphongkhach"){
      closeTheDoor(doorLiving, DOOR_LIVING);
      Serial.println("DONG CUA PHONG KHACH OK");
    }
    if(str == "mocuaphongngu"){
      openTheDoor(doorBedroom, DOOR_BEDROOM);
      Serial.println("MO CUA PHONG NGU OK");
    }
    if(str == "dongcuaphongngu"){
      closeTheDoor(doorBedroom, DOOR_BEDROOM);
      Serial.println("DONG CUA PHONG NGU OK");
    }
    if(str == "mocuaphongbep"){
      openTheDoor(doorKitchen, DOOR_KITCHEN);
      Serial.println("MO CUA PHONG BEP OK");
    }
    if(str == "dongcuaphongbep"){
      closeTheDoor(doorKitchen, DOOR_KITCHEN);
      Serial.println("DONG CUA PHONG BEP OK");
    }
  }

  // Read sensors every 5 seconds, show on LCD 
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if(isnan(h) || isnan(t)) {
    Serial.println("Failed to read from DHT sensor!");
  }
  int gasValue = analogRead(GAS_SENSOR);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.printf("T:%.1fC H:%.0f%%", t, h);
  lcd.setCursor(0, 1);
  lcd.printf("Gas:%d", gasValue);
  Serial.printf("Temperature: %.2f C, Humidity: %.2f %%, Gas: %d\n", t, h, gasValue);
  if(gasValue > GAS_THRESHOLD) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("GAS LEAK!");
    for(int i = 0; i <= 255; i++){
      float sinVal = sin(i * 3.14159 / 180);
      int toneVal = 1500 + (sinVal * 800);
      tone(BUZZER, toneVal);
      delay(5);
    }
  } else {
    noTone(BUZZER);
  }
  delay(1000);
}