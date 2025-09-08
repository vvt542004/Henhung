#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <Stepper.h>
#include <Keypad.h>
#include <Servo.h>
#include <Keypad_I2C.h>

// LCD
LiquidCrystal_I2C lcd(0x27, 20, 4);

// DHT11
const int DHTPIN = 13;
const int DHTTYPE = DHT11;
DHT dht(DHTPIN, DHTTYPE);

// MQ2
const int MQ2_PIN = A0;
const int GAS_THRESHOLD = 500;

// Flame Sensor
const int FLAME_SENSOR_PIN = 6;

// Water Sensor
const int WATER_SENSOR_PIN = 8;

// Buzzer
const int BUZZER_PIN = 7;

// Nút nhấn
const int BUTTON_PIN = 4;

// RTC
RTC_DS3231 rtc;

// Stepper Motor
const int stepsPerRevolution = 2048;
Stepper myStepper(stepsPerRevolution, 9, 11, 10, 12);
bool hasRotated = false;

// Servo
Servo myServo;
const int SERVO_PIN = 5;
//button buzzer
const int BUZZER_STOP_BUTTON_PIN = A1;  // nút dừng còi
bool buzzerManuallyStopped = false;     // cờ đánh dấu đã dừng còi bằng tay
unsigned long buzzerStopTimestamp = 0;
const unsigned long buzzerMuteDuration = 10000;  // 10 giây

// Keypad
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  { '1', '2', '3', 'A' },
  { '4', '5', '6', 'B' },
  { '7', '8', '9', 'C' },
  { '*', '0', '#', 'D' }
};
byte rowPins[ROWS] = { 0, 1, 2, 3 };  // P0 - P3
byte colPins[COLS] = { 4, 5, 6, 7 };  // P4 - P7
// Địa chỉ I2C của PCF8574 (mặc định thường là 0x20 hoặc 0x21)
const uint8_t PCF8574_ADDR = 0x20;
// Khởi tạo keypad I2C
Keypad_I2C keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS, PCF8574_ADDR);

// Mật khẩu
String inputPassword = "";
String currentPassword = "1234";

enum SystemState {
  DEFAULT_DISPLAY,
  ENTER_PASSWORD,
  PASSWORD_SUCCESS,
  PASSWORD_FAIL,
};
SystemState currentState = DEFAULT_DISPLAY;
bool unlocked = false;

// Timeout nhập mật khẩu
unsigned long passwordEntryStart = 0;
const unsigned long passwordTimeout = 30000;

// Custom char độ C
byte degree[8] = {
  0B01110,
  0B01010,
  0B01110,
  0B00000,
  0B00000,
  0B00000,
  0B00000,
  0B00000
};

// millis control
unsigned long previousTimeDisplay = 0;
unsigned long previousBuzzerToggle = 0;
unsigned long lastButtonPress = 0;
const unsigned long displayInterval = 1000;
const unsigned long buzzerBlinkInterval = 400;
const unsigned long debounceDelay = 50;
bool buzzerState = false;
void sendFullStatus();


// Hiển thị khi khóa
void showLockedScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("HE THONG DA KHOA");
  lcd.setCursor(0, 1);
  lcd.print("Nhiet do:       ");
  lcd.write(1);
  lcd.setCursor(0, 2);
  lcd.print("Do am:          %");
  lcd.setCursor(0, 3);
  lcd.print("Gas MQ2:          ");
}

// Hiển thị khi mở khóa
void showDefaultScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("HE THONG DA MO KHOA");
  lcd.setCursor(0, 1);
  lcd.print("Nhiet do:       ");
  lcd.write(1);
  lcd.setCursor(0, 2);
  lcd.print("Do am:          %");
  lcd.setCursor(0, 3);
  lcd.print("Gas MQ2:          ");
}

void setup() {
  pinMode(BUZZER_STOP_BUTTON_PIN, INPUT_PULLUP);
  keypad.begin();
  lcd.init();
  lcd.backlight();
  lcd.createChar(1, degree);
  dht.begin();
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(FLAME_SENSOR_PIN, INPUT);
  pinMode(WATER_SENSOR_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(BUZZER_PIN, LOW);
  Serial.begin(9600);

  if (!rtc.begin()) {
    lcd.setCursor(0, 0);
    lcd.print("RTC khong hoat dong");
    while (1)
      ;
  }
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  myStepper.setSpeed(10);
  myServo.attach(SERVO_PIN);
  myServo.write(0);  // ban đầu khóa
  unlocked = false;
  showLockedScreen();
}

void loop() {
  char key = keypad.getKey();

  if (key) {
    handleKeypad(key);
  }

  if (currentState == ENTER_PASSWORD && (millis() - passwordEntryStart > passwordTimeout)) {
    currentState = DEFAULT_DISPLAY;
    inputPassword = "";
    showLockedScreen();
  }

  // ✅ Dù mở khóa hay không, vẫn đọc cảm biến và hiển thị
  if (currentState == DEFAULT_DISPLAY) {
    updateSensors();
  }
  processSerialCommand();
}
void processSerialCommand() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "open-door") {
      myServo.write(90);
      unlocked = true;
      lcd.setCursor(0, 0);
      lcd.print("Mo cua tu Web      ");
    } else if (cmd == "close-door") {
      myServo.write(0);
      unlocked = false;
      lcd.setCursor(0, 0);
      lcd.print("Dong cua tu Web    ");
    } else if (cmd == "open-canopy" && !hasRotated) {
      myStepper.step(stepsPerRevolution * 3 / 2);
      hasRotated = true;
      lcd.setCursor(0, 0);
      lcd.print("Mo mai tu Web ");
    } else if (cmd == "close-canopy" && hasRotated) {
      myStepper.step(-stepsPerRevolution * 3 / 2);
      hasRotated = false;
      lcd.setCursor(0, 0);
      lcd.print("Dong mai tu Web   ");
    }
  }
}


void handleKeypad(char key) {
  switch (currentState) {
    case DEFAULT_DISPLAY:
      if (key == '*') {
        currentState = ENTER_PASSWORD;
        inputPassword = "";
        passwordEntryStart = millis();
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Nhap mat khau :");
        lcd.setCursor(0, 1);
      } else if (key == 'A' && unlocked) {
        myServo.write(0);
        sendFullStatus();
        unlocked = false;
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Da dong cua!");
        sendFullStatus();
        delay(1500);
        showLockedScreen();
      }
      break;

    case ENTER_PASSWORD:
      passwordEntryStart = millis();
      if (key == '#') {
        if (inputPassword == currentPassword) {
          currentState = PASSWORD_SUCCESS;
          unlocked = true;
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Mo khoa thanh cong");
          myServo.write(90);
          sendFullStatus();
          delay(1500);
          showDefaultScreen();
          currentState = DEFAULT_DISPLAY;
          inputPassword = "";
        } else {
          currentState = PASSWORD_FAIL;
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Sai mat khau!");
          delay(1500);
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Nhap lai:");
          lcd.setCursor(0, 1);
          inputPassword = "";
          currentState = ENTER_PASSWORD;
          passwordEntryStart = millis();
        }
      } else {
        if (inputPassword.length() < 6) {
          inputPassword += key;
          lcd.setCursor(0, 1);
          for (int i = 0; i < inputPassword.length(); i++) lcd.print("*");
          for (int i = inputPassword.length(); i < 6; i++) lcd.print(" ");
        }
      }
      break;
  }
}

void updateSensors() {
  unsigned long currentMillis = millis();
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int gasValue = analogRead(MQ2_PIN);
  int flameState = digitalRead(FLAME_SENSOR_PIN);
  int waterState = digitalRead(WATER_SENSOR_PIN);
  int buttonState = digitalRead(BUTTON_PIN);
  bool stopButtonPressed = digitalRead(BUZZER_STOP_BUTTON_PIN) == LOW;

  if (!isnan(t)) {
    lcd.setCursor(10, 1);
    lcd.print("   ");
    lcd.setCursor(10, 1);
    lcd.print(round(t));
  }
  if (!isnan(h)) {
    lcd.setCursor(10, 2);
    lcd.print("   ");
    lcd.setCursor(10, 2);
    lcd.print(round(h));
  }
  lcd.setCursor(10, 3);
  lcd.print("     ");
  lcd.setCursor(10, 3);
  lcd.print(gasValue);

  bool warningActive = false;

  // Nếu nhấn nút dừng còi → dừng còi
  if (stopButtonPressed) {
    buzzerStopTimestamp = currentMillis;
    buzzerManuallyStopped = true;
    digitalWrite(BUZZER_PIN, LOW);
    buzzerState = false;
  }

  // Nếu trong thời gian tạm dừng → không cho còi kêu
  bool inMutePeriod = buzzerManuallyStopped && (currentMillis - buzzerStopTimestamp <= buzzerMuteDuration);

  if (inMutePeriod) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerState = false;
  } else {
    buzzerManuallyStopped = false;  // Hết thời gian tạm ngừng → reset

    if (gasValue > GAS_THRESHOLD && flameState == LOW) {
      lcd.setCursor(0, 0);
      lcd.print("CANH BAO GAS+LUA    ");
      digitalWrite(BUZZER_PIN, HIGH);
      buzzerState = true;
      warningActive = true;
    } else if (flameState == LOW) {
      lcd.setCursor(0, 0);
      lcd.print("CANH BAO LUA        ");
      digitalWrite(BUZZER_PIN, HIGH);
      buzzerState = true;
      warningActive = true;
    } else if (gasValue > GAS_THRESHOLD) {
      lcd.setCursor(0, 0);
      lcd.print("CANH BAO KHI GAS    ");
      if (currentMillis - previousBuzzerToggle >= buzzerBlinkInterval) {
        previousBuzzerToggle = currentMillis;
        buzzerState = !buzzerState;
        digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
      }
      warningActive = true;
    } else {
      digitalWrite(BUZZER_PIN, LOW);
      buzzerState = false;
    }
  }

  // Nếu hết cảnh báo → reset trạng thái dừng còi bằng tay
  if (gasValue <= GAS_THRESHOLD && flameState == HIGH) {
    buzzerManuallyStopped = false;
  }
  static bool isRaining = false;
  if (waterState == LOW) {
    if (!isRaining) {
      isRaining = true;
      if (hasRotated) {
        lcd.setCursor(0, 0);
        lcd.print("MUA - DONG MAI CHE ");
        myStepper.step(-stepsPerRevolution * 3 / 2);
        hasRotated = false;
        sendFullStatus();
      }
    }
    lcd.setCursor(0, 0);
    lcd.print("PHAT HIEN MUA!     ");
    warningActive = true;
  } else {
    if (isRaining) {
      isRaining = false;
      lcd.setCursor(0, 0);
      lcd.print("KHONG MUA          ");
    }
    warningActive = false;
    if (buttonState == LOW && (millis() - lastButtonPress > debounceDelay)) {
      if (!hasRotated) {
        lcd.setCursor(0, 0);
        lcd.print("MO MAI CHE         ");
        myStepper.step(stepsPerRevolution * 3 / 2);
        hasRotated = true;
        sendFullStatus();  // 🛠 Gửi trạng thái mới
      } else {
        lcd.setCursor(0, 0);
        lcd.print("DONG MAI CHE       ");
        myStepper.step(-stepsPerRevolution * 3 / 2);
        hasRotated = false;
        sendFullStatus();  // 🛠 Gửi trạng thái mới
      }

      lastButtonPress = millis();
    }
  }

  if (!warningActive && (currentMillis - previousTimeDisplay >= displayInterval)) {
    previousTimeDisplay = currentMillis;
    DateTime now = rtc.now();
    char buf[21];
    sprintf(buf, "%02d:%02d:%02d %02d/%02d/%04d",
            now.hour(), now.minute(), now.second(),
            now.day(), now.month(), now.year());
    lcd.setCursor(0, 0);
    lcd.print(buf);
    for (int i = strlen(buf); i < 20; i++) lcd.print(" ");
  }

  // ===== Gửi dữ liệu về Web thông qua Serial =====
  DateTime now = rtc.now();
  char timeStr[25];
  sprintf(timeStr, "%02d:%02d:%02d %02d/%02d/%04d",
          now.hour(), now.minute(), now.second(),
          now.day(), now.month(), now.year());

  sendFullStatus();
}

void sendFullStatus() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  int gasValue = analogRead(MQ2_PIN);
  int flameState = digitalRead(FLAME_SENSOR_PIN);
  int waterState = digitalRead(WATER_SENSOR_PIN);

  DateTime now = rtc.now();
  char timeStr[25];
  sprintf(timeStr, "%02d:%02d:%02d %02d/%02d/%04d",
          now.hour(), now.minute(), now.second(),
          now.day(), now.month(), now.year());

  Serial.print("{");
  Serial.print("\"temp\":");
  Serial.print(round(t));
  Serial.print(",");
  Serial.print("\"hum\":");
  Serial.print(round(h));
  Serial.print(",");
  Serial.print("\"gas\":");
  Serial.print(gasValue);
  Serial.print(",");
  Serial.print("\"flame\":");
  Serial.print(flameState == LOW ? "\"Phát hiện\"" : "\"Không\"");
  Serial.print(",");
  Serial.print("\"rain\":");
  Serial.print(waterState == LOW ? "\"Có\"" : "\"Không\"");
  Serial.print(",");
  Serial.print("\"door\":\"");
  Serial.print(unlocked ? "Đang mở" : "Đang đóng");
  Serial.print("\",");
  Serial.print("\"canopy\":\"");
  Serial.print(hasRotated ? "Đang mở" : "Đang đóng");
  Serial.print("\",");
  Serial.print("\"time\":\"");
  Serial.print(timeStr);
  Serial.println("\"}");
}