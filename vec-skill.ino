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
// ใช้ volatile เพื่อบอก Compiler ว่าค่าเหล่านี้อาจเปลี่ยนโดย Task อื่นได้ตลอด
volatile bool systemActive = false;
volatile int currentLDRValue = 0;
volatile int currentMotorPercent = 0;

// --- การตั้งค่า PWM สำหรับ Motor (ESP32) ---
const int freq = 5000;
const int ledChannel = 0;
const int resolution = 8;

// ==========================================
// Task 1: ตรวจสอบปุ่มกด (Start / Emergency)
// ==========================================
void TaskButton(void *pvParameters) {
  pinMode(PIN_SW_START, INPUT_PULLUP);
  pinMode(PIN_SW_EMERGENCY, INPUT_PULLUP);

  for (;;) {
    // อ่านปุ่ม Emergency (Priority สูงสุด) -> กดแล้วหยุดทันที
    if (digitalRead(PIN_SW_EMERGENCY) == LOW) {
      systemActive = false;
      Serial.println("EMERGENCY STOP!");
    } 
    // อ่านปุ่ม Start -> กดแล้วเริ่มทำงาน
    else if (digitalRead(PIN_SW_START) == LOW) {
      if (!systemActive) {
        systemActive = true;
        Serial.println("System Started");
      }
    }
    
    vTaskDelay(100 / portTICK_PERIOD_MS); // เช็คปุ่มทุกๆ 100ms
  }
}

// ==========================================
// Task 2: ควบคุมแสงสว่าง (LDR -> Relay)
// ==========================================
void TaskLDR(void *pvParameters) {
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW); // ปิดไฟไว้ก่อน

  for (;;) {
    if (systemActive) {
      // 1.1 อ่านค่า LDR (0-4095)
      int rawValue = analogRead(PIN_LDR);
      
      // แปลงค่าให้อยู่ในช่วงที่เข้าใจง่าย (สมมติเทียบเคียง Lux แบบคร่าวๆ)
      // หมายเหตุ: การแปลงเป็น Lux จริงต้องสอบเทียบ ในที่นี้ใช้ค่า Raw ตามเงื่อนไข >=< 300
      currentLDRValue = rawValue; 

      // 1.2 & 1.3 เงื่อนไขควบคุมไฟ
      // หมายเหตุ: ESP32 ADC 12bit ค่าสูงสุดคือ 4095
      // ถ้าค่าที่อ่านได้ < 300 (มืด) -> ไฟติด
      if (currentLDRValue < 300) {
        digitalWrite(PIN_RELAY, HIGH); // Lamp ON
      } else {
        digitalWrite(PIN_RELAY, LOW);  // Lamp OFF
      }
    } else {
      // ถ้า System Inactive ให้ปิดไฟเสมอ
      digitalWrite(PIN_RELAY, LOW);
    }

    vTaskDelay(200 / portTICK_PERIOD_MS); // ทำงานทุก 200ms
  }
}

// ==========================================
// Task 3: ควบคุมมอเตอร์ (VR -> Motor)
// ==========================================
void TaskMotor(void *pvParameters) {
  // ตั้งค่า PWM
  ledcSetup(ledChannel, freq, resolution);
  ledcAttachPin(PIN_MOTOR, ledChannel);

  for (;;) {
    if (systemActive) {
      // 2.1 อ่านค่า VR (0-4095)
      int vrValue = analogRead(PIN_VR);

      // 2.2 ปรับความเร็ว Map 0-4095 เป็น PWM 0-255
      int pwmValue = map(vrValue, 0, 4095, 0, 255);
      
      // ควบคุมมอเตอร์
      ledcWrite(ledChannel, pwmValue);

      // 2.3 คำนวณ % เพื่อแสดงผล (0-100%)
      currentMotorPercent = map(vrValue, 0, 4095, 0, 100);

    } else {
      // ถ้า System Inactive ให้หยุดมอเตอร์
      ledcWrite(ledChannel, 0);
      currentMotorPercent = 0;
    }

    vTaskDelay(100 / portTICK_PERIOD_MS); // อัปเดตทุก 100ms
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
      // 1.4 แสดงค่า Lux
      lcd.setCursor(0, 0);
      lcd.print("Light :       "); // Clear old text
      lcd.setCursor(8, 0);
      lcd.print(currentLDRValue);
      lcd.print(" lux");

      // 2.3 แสดงค่า Motor %
      lcd.setCursor(0, 1);
      lcd.print("Speed :       "); // Clear old text
      lcd.setCursor(8, 1);
      lcd.print(currentMotorPercent);
      lcd.print(" %");
    } else {
      // แสดงสถานะหยุดทำงาน
      lcd.setCursor(0, 0);
      lcd.print("System: STOPPED ");
      lcd.setCursor(0, 1);
      lcd.print("Press Start...  ");
    }

    vTaskDelay(500 / portTICK_PERIOD_MS); // อัปเดตหน้าจอทุก 0.5 วินาที (ไม่ควรถี่เกินไปจอจะกระพริบ)
  }
}

// ==========================================
// Main Setup
// ==========================================
void setup() {
  Serial.begin(115200);

  // สร้าง Tasks
  xTaskCreate(TaskButton,  "Button Task",  2048, NULL, 2, NULL); // Priority สูงสุด (เช็คปุ่มต้องไว)
  xTaskCreate(TaskLDR,     "LDR Task",     2048, NULL, 1, NULL);
  xTaskCreate(TaskMotor,   "Motor Task",   2048, NULL, 1, NULL);
  xTaskCreate(TaskDisplay, "Display Task", 2048, NULL, 1, NULL); // Priority ต่ำสุดได้ เพราะแสดงผลช้าหน่อยได้
}

void loop() {
  // ใน FreeRTOS loop หลักปล่อยว่างได้เลย เพราะงานอยู่ใน Task หมดแล้ว
}