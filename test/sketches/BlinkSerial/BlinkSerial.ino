// Exercises the CHGame board the way a user would: Serial is the native USB
// CDC port (no Serial.begin baud rate matters over USB), the LED is
// LED_BUILTIN, and the buttons are active-low with internal pull-ups.
void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PIN_BTN_A, INPUT_PULLUP);
}

void loop() {
  static uint32_t n = 0;
  digitalWrite(LED_BUILTIN, HIGH);
  delay(100);
  digitalWrite(LED_BUILTIN, LOW);
  delay(900);
  Serial.print("tick ");
  Serial.print(n++);
  Serial.print("  A=");
  Serial.println(digitalRead(PIN_BTN_A) ? "up" : "DOWN");
}
