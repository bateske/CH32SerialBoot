// Measures how long the board takes to become useful after power-on, and
// proves the property that lets it be fast: enumeration runs entirely in the
// USB interrupt, so setup() does not have to wait for it.
//
// millis() is zero-based at the start of the application, so every number
// printed here is "milliseconds since the app got control" -- add ~8 ms of
// bootloader (CRC32 over the image, then the jump) for time since power-on.
//
// Expected, with a non-blocking USBSerial::begin():
//   setup() entry     ~2 ms      (was ~1105 ms: delay(100) + delay(1000))
//   panel work done   ~300 ms    (the simulated display init below)
//   enumerated        ~300-400 ms, and CONCURRENT -- it should land during the
//                                 panel work, not after it
//
// The result to look for is "enumerated during panel init: yes". That is the
// whole claim: the 1 s pad was not buying enumeration, it was just serialising
// it behind an idle CPU.

static uint32_t t_setup_entry;
static uint32_t t_panel_done;
static uint32_t t_enumerated;

// Stand-in for the ST7735 bring-up: a hardware reset settle plus the SLPOUT
// wait the panel actually mandates. Kept as a busy region on purpose -- the
// point is that USB completes underneath it without any cooperation.
static void simulated_panel_init() {
  delay(5);                      // hardware reset -> first command
  delay(120);                    // SLPOUT settle (datasheet minimum)
  delay(175);                    // init tables + first full-frame push
}

void setup() {
  t_setup_entry = millis();

  Serial.begin(115200);

  simulated_panel_init();
  t_panel_done = millis();

  // If enumeration already finished, it finished while the panel was being
  // driven -- which is the behaviour under test. Otherwise wait for it, and
  // the timestamp will show it landing late.
  while (!Serial.enumerated()) { }
  t_enumerated = millis();

  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  // Nothing is printed until a terminal actually opens the port, so the
  // numbers above stay honest -- they were all captured at boot.
  if (!Serial) {
    digitalWrite(LED_BUILTIN, (millis() / 500) & 1);
    return;
  }

  Serial.println(F("--- CHGame boot timing (ms since app start) ---"));
  Serial.print(F("setup() entry:            ")); Serial.println(t_setup_entry);
  Serial.print(F("panel init done:          ")); Serial.println(t_panel_done);
  Serial.print(F("USB enumerated:           ")); Serial.println(t_enumerated);
  Serial.print(F("enumerated during panel init: "));
  Serial.println(t_enumerated <= t_panel_done ? F("yes") : F("NO - it serialised"));
  Serial.println();
  delay(2000);
}
