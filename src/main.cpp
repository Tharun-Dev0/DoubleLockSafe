#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Keypad.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>

// ===== WiFi & ThingSpeak =====
const char* ssid = "";
const char* password = "";
const String writeAPIKey = "";
const String readAPIKey  = "";
const String channelID   = "";
const String updateURL   = "http://api.thingspeak.com/update";
const String readURL     = "http://api.thingspeak.com/channels/" + channelID + "/fields/1/last.json?api_key=" + readAPIKey;

// ===== Pins =====
#define DEPOSIT_BTN 19
#define WITHDRAW_BTN 18
#define CONTACT_PIN 4      // contact to GND when locked
#define IR_DOOR 34         // IR sensor (HIGH when door aligned)
#define GREEN_LED 5
#define RED_BUZZER 17

// ===== OLED =====

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ===== Keypad =====
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {12, 13, 14, 27};
byte colPins[COLS] = {26, 25, 33, 32};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ===== Variables =====
int balance = 0;
const String USER_PASSWORD = "1111";
const unsigned long USER_PERIOD = 12000UL;
const unsigned long ALERT_CONTINUOUS_TIMEOUT = 20000UL;
const unsigned long AUTO_LOCK_DELAY = 5000UL;
bool doorLocked = true;

// ===== Function Declarations =====
void fetchBalanceFromThingSpeak();
bool verifyPassword();
int getAmount(String prompt);
void updateThingSpeak(bool isDeposit=false, bool isWithdraw=false, bool alert=false);
bool isDoorClosed();
void continuousAlarmUntilClosed();
bool waitForDoorClosureWithAlerting(bool &alertSent);

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Smart Safe System Initializing ===");

  pinMode(DEPOSIT_BTN, INPUT_PULLUP);
  pinMode(WITHDRAW_BTN, INPUT_PULLUP);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_BUZZER, OUTPUT);
  pinMode(CONTACT_PIN, INPUT_PULLUP);
  pinMode(IR_DOOR, INPUT_PULLUP);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 failed");
    for(;;);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Connecting WiFi...");
  display.display();

  WiFi.begin(ssid, password);
  Serial.print("Connecting");
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("WiFi Connected!");
  display.display();
  delay(800);

  fetchBalanceFromThingSpeak();
  Serial.println("Fetched Balance: " + String(balance));
}

// ===== Loop =====
void loop() {
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Balance: " + String(balance));
  display.println("Press Deposit or Withdraw");
  display.display();

  // ---------- Withdraw ----------
  if (digitalRead(WITHDRAW_BTN) == LOW) {
    delay(200);
    if (verifyPassword()) {
      Serial.println("Withdraw Mode");
      display.clearDisplay();
      display.setCursor(0,0);
      display.println("Withdraw Mode");
      display.display();

      bool alertSent = false;
      bool closedBeforeAmount = waitForDoorClosureWithAlerting(alertSent);

      int amt = getAmount("Enter withdraw amt:");
      if (amt <= balance) {
        balance -= amt;
        Serial.println("Withdraw successful. New balance: " + String(balance));
        updateThingSpeak(false, true, alertSent);
      } else {
        display.clearDisplay();
        display.setCursor(0,0);
        display.println("Insufficient balance!");
        display.display();
        digitalWrite(RED_BUZZER, HIGH);
        delay(1000);
        digitalWrite(RED_BUZZER, LOW);
      }

      // wait 5s for auto lock, then verify closure
      Serial.println("Waiting 5s for door closure...");
      delay(AUTO_LOCK_DELAY);

      if(isDoorClosed()) {
        display.clearDisplay();
        display.setCursor(0,0);
        display.println("Door locked !!");
        display.display();
        digitalWrite(GREEN_LED, HIGH);
        delay(2000);
        digitalWrite(GREEN_LED, LOW);
      } else {
        Serial.println("Door still open -> continuous alert");
        continuousAlarmUntilClosed();
      }

      display.clearDisplay();
      display.setCursor(0,0);
      display.println("Done. Balance: " + String(balance));
      display.display();
      digitalWrite(GREEN_LED, HIGH);
      delay(2000);
      digitalWrite(GREEN_LED, LOW);
    }
  }

  // ---------- Deposit ----------
  if (digitalRead(DEPOSIT_BTN) == LOW) {
    delay(200);
    if (verifyPassword()) {
      Serial.println("Deposit Mode");
      display.clearDisplay();
      display.setCursor(0,0);
      display.println("Deposit Mode");
      display.display();

      bool alertSent = false;
      bool closedBeforeAmount = waitForDoorClosureWithAlerting(alertSent);

      int amt = getAmount("Enter deposit amt:");
      balance += amt;
      Serial.println("Deposit successful. New balance: " + String(balance));
      updateThingSpeak(true, false, alertSent);

      Serial.println("Waiting 5s for door closure...");
      delay(AUTO_LOCK_DELAY);

      if(isDoorClosed()) {
        display.clearDisplay();
        display.setCursor(0,0);
        display.println("Door locked !!");
        display.display();
        digitalWrite(GREEN_LED, HIGH);
        delay(2000);
        digitalWrite(GREEN_LED, LOW);
      } else {
        Serial.println("Door still open -> continuous alert");
        continuousAlarmUntilClosed();
      }

      display.clearDisplay();
      display.setCursor(0,0);
      display.println("Done. Balance: " + String(balance));
      display.display();
      digitalWrite(GREEN_LED, HIGH);
      delay(2000);
      digitalWrite(GREEN_LED, LOW);
    }
  }

  delay(100);
}

// ===== Helper Functions =====

void fetchBalanceFromThingSpeak() {
  if(WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(readURL);
    int httpCode = http.GET();
    if(httpCode == 200) {
      String payload = http.getString();
      int pos = payload.indexOf("\"field1\":\"");
      if(pos > 0) {
        int start = pos + 10;
        int end = payload.indexOf("\"", start);
        if(end > start) balance = payload.substring(start, end).toInt();
      }
    }
    http.end();
  }
}

bool verifyPassword() {
  String entered = "";
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Enter Password (# to end)");
  display.display();
  Serial.println("Enter Password:");

  while(true) {
    char k = keypad.getKey();
    if(k) {
      if(k == '#') break;
      if(isdigit(k)) {
        entered += k;
        display.print("*");
        display.display();
      }
    }
  }

  if(entered == USER_PASSWORD) {
    Serial.println("Password OK");
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("Password OK");
    display.display();
    delay(500);
    digitalWrite(GREEN_LED, HIGH);
    delay(2000);
    digitalWrite(GREEN_LED, LOW);
    return true;
  } else {
    Serial.println("Wrong Password");
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("Wrong Password!");
    display.display();
    digitalWrite(RED_BUZZER, HIGH);
    delay(800);
    digitalWrite(RED_BUZZER, LOW);
    return false;
  }
}

int getAmount(String prompt) {
  String amtStr = "";
  display.clearDisplay();
  display.setCursor(0,0);
  display.println(prompt);
  display.println("(# to finish)");
  display.display();

  while(true) {
    char k = keypad.getKey();
    if(k) {
      if(isdigit(k)) {
        amtStr += k;
        display.print(k);
        display.display();
      } else if(k == '#') {
        if(amtStr.length() > 0) return amtStr.toInt();
      } else if(k == '*') {
        amtStr = "";
        display.clearDisplay();
        display.setCursor(0,0);
        display.println(prompt);
        display.println("(# to finish)");
        display.display();
      }
    }
  }
}

void updateThingSpeak(bool isDeposit, bool isWithdraw, bool alert) {
  if(WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = updateURL + "?api_key=" + writeAPIKey;
  url += "&field1=" + String(balance);
  url += "&field2=" + String(isDeposit ? 1 : 0);
  url += "&field3=" + String(isWithdraw ? 1 : 0);
  url += "&field4=" + String(alert ? 1 : 0);
  http.begin(url);
  int code = http.GET();
  Serial.println("ThingSpeak update: " + String(code));
  http.end();
}

bool isDoorClosed() {
  bool ir = (digitalRead(IR_DOOR) ==  LOW ? HIGH : LOW);
  bool contact = (digitalRead(CONTACT_PIN) == LOW ? HIGH : LOW); // active-low contact
  return (ir == HIGH && contact == HIGH);
}

bool waitForDoorClosureWithAlerting(bool &alertSent) {
  alertSent = false;
  unsigned long start = millis();

  while(millis() - start < USER_PERIOD) {
    if(isDoorClosed()) return true;
    delay(60);
  }

  display.clearDisplay();
  display.setCursor(0,0);
  display.println("?? Please close door!");
  display.display();
  digitalWrite(RED_BUZZER, HIGH);
  delay(500);
  digitalWrite(RED_BUZZER, LOW);

  unsigned long alertStart = millis();
  while(millis() - alertStart < ALERT_CONTINUOUS_TIMEOUT) {
    if(isDoorClosed()) {
      display.clearDisplay();
      display.setCursor(0,0);
      display.println("Door Closed !!");
      display.display();
      return true;
    }
    digitalWrite(RED_BUZZER, HIGH);
    delay(300);
    digitalWrite(RED_BUZZER, LOW);
    delay(700);
  }

  display.clearDisplay();
  display.setCursor(0,0);
  display.println("::? Door still open!");
  display.display();
  updateThingSpeak(false, false, true);
  alertSent = true;
  continuousAlarmUntilClosed();
  return false;
}

void continuousAlarmUntilClosed() {
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("?? Door open!");
  display.println("Close to stop alert");
  display.display();

  while(true) {
    if(isDoorClosed()) {
      display.clearDisplay();
      display.setCursor(0,0);
      display.println("Door Closed !!"); 
      display.display();
      digitalWrite(RED_BUZZER, LOW);
      digitalWrite(GREEN_LED, HIGH);
      delay(2000);
      digitalWrite(GREEN_LED, LOW);
      break;
    }
    digitalWrite(RED_BUZZER, HIGH);
    delay(350);
    digitalWrite(RED_BUZZER, LOW);
    delay(350);
  }
}
