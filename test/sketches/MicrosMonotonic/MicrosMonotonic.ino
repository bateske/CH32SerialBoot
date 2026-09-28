// Checks that micros() never runs backwards and stays in step with millis().
//
// Regression test for getCurrentMicros() in cores/arduino/ch32/clock.c. The
// CH32X035 SysTick counts UP from 0 to CMP and auto-reloads, but the original
// STM32-derived code computed the sub-millisecond part as if the counter ran
// DOWN, so micros() decreased inside every millisecond and jumped ~2 ms
// forward at each tick. That fails phase 1 below with tens of thousands of
// backwards steps per second. Phase 2 covers the fix's other half: a tick
// that lands while interrupts are off must still be counted.
//
// Healthy output:
//   phase 1  backwards steps: 0        largest step: a few us (more if USB ran)
//            micros-vs-millis drift within [-1, 1] ms
//   phase 2  ordering faults: 0        worst 300 us window error: < 10 us

static const uint32_t WINDOW_MS = 2000;

static void phase1() {
  uint32_t start_ms = millis();
  uint32_t start_us = micros();
  uint32_t prev = start_us;
  uint32_t samples = 0, backwards = 0, worst_back = 0, largest_step = 0;
  int32_t drift_min = 0, drift_max = 0;

  while (millis() - start_ms < WINDOW_MS) {
    uint32_t ms  = millis();
    uint32_t now = micros();
    samples++;
    int32_t step = (int32_t)(now - prev);       // wrap-safe
    if (step < 0) {
      backwards++;
      if ((uint32_t)-step > worst_back) worst_back = (uint32_t)-step;
    } else if ((uint32_t)step > largest_step) {
      largest_step = (uint32_t)step;
    }
    prev = now;
    // Both clocks derive from the same tick, so they must agree to the ms.
    int32_t drift = (int32_t)((now - start_us) / 1000) - (int32_t)(ms - start_ms);
    if (drift < drift_min) drift_min = drift;
    if (drift > drift_max) drift_max = drift;
  }

  Serial.print(F("phase 1: ")); Serial.print(samples); Serial.println(F(" samples"));
  Serial.print(F("  backwards steps:        ")); Serial.println(backwards);
  Serial.print(F("  worst backwards step:   ")); Serial.print(worst_back); Serial.println(F(" us"));
  Serial.print(F("  largest forward step:   ")); Serial.print(largest_step); Serial.println(F(" us"));
  Serial.print(F("  micros-vs-millis drift: [")); Serial.print(drift_min);
  Serial.print(F(", ")); Serial.print(drift_max); Serial.println(F("] ms"));
}

static void phase2() {
  uint32_t faults = 0, worst_err = 0;
  // 400 windows of ~300 us, each shifted 137 us against the tick, so plenty
  // of them straddle a millisecond boundary with the SysTick IRQ held off.
  for (int i = 0; i < 400; i++) {
    noInterrupts();
    uint32_t a = micros();
    delayMicroseconds(300);
    uint32_t b = micros();
    interrupts();
    uint32_t c = micros();
    if ((int32_t)(b - a) < 0 || (int32_t)(c - b) < 0) faults++;
    uint32_t w = b - a;
    uint32_t err = (w > 300) ? (w - 300) : (300 - w);
    if (err > worst_err) worst_err = err;
    delayMicroseconds(137);
  }
  Serial.println(F("phase 2: 400 windows with interrupts off"));
  Serial.print(F("  ordering faults:        ")); Serial.println(faults);
  Serial.print(F("  worst 300 us error:     ")); Serial.print(worst_err); Serial.println(F(" us"));
}

void setup() {
  Serial.begin();
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  if (!Serial) {                      // blink until a terminal opens the port
    digitalWrite(LED_BUILTIN, (millis() / 500) & 1);
    return;
  }
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.println(F("--- CHGame micros() monotonicity ---"));
  Serial.print(F("F_CPU ")); Serial.print(F_CPU); Serial.print(F("  SysTick CMP "));
  Serial.println((uint32_t)SysTick->CMP);
  phase1();
  phase2();
  Serial.println();
  digitalWrite(LED_BUILTIN, LOW);
  delay(3000);
}
