#include <Arduino.h>
// Pin definitions
const int IR_SENSOR_PIN = 34;   // IR sensor connected to GPIO 24
const int SWITCH_PIN    = 4;    // Switch connected to GPIO 4 (D4)

void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32 IR Sensor and Switch Test");

  // Configure pins as inputs
  pinMode(IR_SENSOR_PIN, INPUT);
  pinMode(SWITCH_PIN, INPUT_PULLUP); // Use internal pull-up (assuming switch to GND)
}

void loop() {
  // Read sensor and switch states
  int irValue = digitalRead(IR_SENSOR_PIN);
  int switchValue = digitalRead(SWITCH_PIN);

  // Print states to Serial Monitor
  Serial.print("IR Sensor: ");
  Serial.print(irValue == HIGH ? "HIGH" : "LOW");
  Serial.print(" | Switch: ");
  Serial.println(switchValue == LOW ? "PRESSED" : "RELEASED");

  delay(500); // update every 0.5 sec
}
