#include <Arduino.h>
#include <LiquidCrystal_I2C.h>

// --- กำหนด Hardware Pin ---
#define PIN_LDR 34
#define PIN_VR 35
#define PIN_RELAY 25
#define PIN_MOTOR 26
#define PIN_SW_START 18
#define PIN_SW_EMERGENCY 19

// --- ตั้งค่า LCD (Address 0x27 หรือ 0x3F แล้วแต่รุ่น) ---
LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- ตัวแปร Global (Shared Resources) ---
volatile bool systemActive = false;
volatile int currentLDRValue = 0;
volatile int currentMotorPercent = 0;

// --- การตั้งค่า PWM สำหรับ Motor ---
const int freq = 5000;
const int resolution = 8;
// หมายเหตุ: ESP32 V3.0+ ไม่ต้องกำหนด Channel เองแล้ว

// ==========================================
// Task 1: ตรวจสอบปุ่มกด (Start / Emergency)
// ==========================================
void TaskButton(void *pvParameters) {
  pinMode(PIN_SW_START, INPUT_PULLUP);
  pinMode(PIN_SW_EMERGENCY, INPUT_PULLUP);

  for (;;) {
    if (digitalRead(PIN_SW_EMERGENCY) == LOW) {
      systemActive = false;
      Serial.println("EMERGENCY STOP!");
    } 
    else if (digitalRead(PIN_SW_START) == LOW) {
      if (!systemActive) {
        systemActive = true;
        Serial.println("System Started");
      }
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// ==========================================
// Task 2: ควบคุมแสงสว่าง (LDR -> Relay)
// ==========================================
void TaskLDR(void *pvParameters) {
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);

  for (;;) {
    if (systemActive) {
      int rawValue = analogRead(PIN_LDR);
      currentLDRValue = rawValue; 

      if (currentLDRValue < 300) {
        digitalWrite(PIN_RELAY, HIGH); // Lamp ON
      } else {
        digitalWrite(PIN_RELAY, LOW);  // Lamp OFF
      }
    } else {
      digitalWrite(PIN_RELAY, LOW);
    }
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}

// ==========================================
// Task 3: ควบคุมมอเตอร์ (VR -> Motor) (แก้ไขแล้ว)
// ==========================================
void TaskMotor(void *pvParameters) {
  // ESP32 V3.0 API: Attach Pin, Freq, Resolution
  ledcAttach(PIN_MOTOR, freq, resolution);

  for (;;) {
    if (systemActive) {
      int vrValue = analogRead(PIN_VR);
      int pwmValue = map(vrValue, 0, 4095, 0, 255);
      
      // ESP32 V3.0 API: Write to Pin directly
      ledcWrite(PIN_MOTOR, pwmValue);

      currentMotorPercent = map(vrValue, 0, 4095, 0, 100);

    } else {
      ledcWrite(PIN_MOTOR, 0);
      currentMotorPercent = 0;
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// ==========================================
// Task 4: แสดงผล (LCD Display)
// ==========================================
void TaskDisplay(void *pvParameters) {
  lcd.init();
  lcd.backlight();
  lcd.clear();

  for (;;) {
    if (systemActive) {
      lcd.setCursor(0, 0);
      lcd.print("Light :       ");
      lcd.setCursor(8, 0);
      lcd.print(currentLDRValue);
      lcd.print(" lux");

      lcd.setCursor(0, 1);
      lcd.print("Speed :       ");
      lcd.setCursor(8, 1);
      lcd.print(currentMotorPercent);
      lcd.print(" %");
    } else {
      lcd.setCursor(0, 0);
      lcd.print("System: STOPPED ");
      lcd.setCursor(0, 1);
      lcd.print("Press Start...  ");
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

// ==========================================
// Main Setup
// ==========================================
void setup() {
  Serial.begin(115200);

  xTaskCreate(TaskButton,  "Button Task",  2048, NULL, 2, NULL);
  xTaskCreate(TaskLDR,     "LDR Task",     2048, NULL, 1, NULL);
  xTaskCreate(TaskMotor,   "Motor Task",   2048, NULL, 1, NULL);
  xTaskCreate(TaskDisplay, "Display Task", 2048, NULL, 1, NULL);
}

void loop() {
  // Empty loop
}