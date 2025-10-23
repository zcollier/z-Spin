#include <Mouse.h>
#include <Joystick.h>

//==================== Pins (Pro Micro) ====================
const byte encoderPinA = 2;    // interrupt-capable
const byte encoderPinB = 3;    // interrupt-capable

// D-pad as GamePad hat switch
const byte dpadUpPin    = 9;
const byte dpadDownPin  = 10;
const byte dpadLeftPin  = A0; // Pro Micro analog pin used as digital
const byte dpadRightPin = A1; // Pro Micro analog pin used as digital

// GamePad buttons 1, 2, 3, and 4
const byte gpBtnPins[4] = {4, 5, 6, 8};  // maps to GamePad buttons 1, 2, 3, 4

// Mouse buttons
const byte mouseLeftPin  = A2; // Pro Micro analog pin used as digital
const byte mouseRightPin = A3; // Pro Micro analog pin used as digital

// Encoder axis mode toggle + external LED (LED ON = Y-mode)
const byte modeBtnPin   = 7;    // press to toggle X <-> Y
const byte modeLedPin   = 16;   // use external LED on pin 16 -> resistor -> LED -> GND

//==================== GamePad (Joystick lib) ====================
// 4 buttons, 1 hat switch; all analog axes disabled  since mouse handles pointer movement
Joystick_ Joystick(
  JOYSTICK_DEFAULT_REPORT_ID,
  JOYSTICK_TYPE_GAMEPAD,
  4,     // buttons (index 0..3 -> shown as 1..4 in most tools)
  1,     // hat switches
  false, // X
  false, // Y
  false, // Z
  false, // Rx
  false, // Ry
  false, // Rz
  false, // rudder
  false, // throttle
  false, // accelerator
  false, // brake
  false  // steering
);

//==================== Encoder state ====================
volatile long encoderPos = 0;   // ISR-updated
long lastReportedPos = 0;       // what we've already sent to host
bool encoderYMode = false;      // false = X (default), true = Y
bool modeBtnPrev = false;
uint32_t lastToggleMs = 0;
const uint16_t TOGGLE_DEBOUNCE_MS = 200;

//==================== D-pad state ====================
int prevHat = -1;               // -1 = centered
const uint8_t DPAD_POLL_MS = 5;
uint32_t lastDpadPoll = 0;

//==================== GamePad buttons & Mouse buttons ====================
bool gpPrev[4] = {false, false, false, false};
bool mLeftPrev = false, mRightPrev = false;

//==================== Helpers ====================
static inline int8_t clipInt8(long v){
  if (v > 127) return 127;
  if (v < -127) return -127;
  return (int8_t)v;
}


void doEncoderA() {
  bool A = digitalRead(encoderPinA);
  bool B = digitalRead(encoderPinB);
  encoderPos += (A == B) ? +1 : -1;
}
void doEncoderB() {
  bool A = digitalRead(encoderPinA);
  bool B = digitalRead(encoderPinB);
  encoderPos += (A != B) ? +1 : -1;
}

void setup() {
  pinMode(encoderPinA, INPUT_PULLUP);
  pinMode(encoderPinB, INPUT_PULLUP);

  pinMode(dpadUpPin,    INPUT_PULLUP);
  pinMode(dpadDownPin,  INPUT_PULLUP);
  pinMode(dpadLeftPin,  INPUT_PULLUP);
  pinMode(dpadRightPin, INPUT_PULLUP);

  for (uint8_t i = 0; i < 4; i++) pinMode(gpBtnPins[i], INPUT_PULLUP);

  pinMode(mouseLeftPin,  INPUT_PULLUP);
  pinMode(mouseRightPin, INPUT_PULLUP);

  pinMode(modeBtnPin, INPUT_PULLUP);
  pinMode(modeLedPin, OUTPUT);
  digitalWrite(modeLedPin, LOW);     // LED OFF in X-mode

  attachInterrupt(digitalPinToInterrupt(encoderPinA), doEncoderA, CHANGE);
  attachInterrupt(digitalPinToInterrupt(encoderPinB), doEncoderB, CHANGE);

  Mouse.begin();         // USB Mouse for pointer movement + left and right Mouse Buttons
  Joystick.begin(false); // USB GamePad; manual sendState to reduce traffic
}

void loop() {
  const uint32_t now = millis();

  // --- Mode toggle (encoder X<->Y), LED indicates Y-mode ---
  bool modeDown = (digitalRead(modeBtnPin) == LOW);
  if (modeDown && !modeBtnPrev && (now - lastToggleMs) > TOGGLE_DEBOUNCE_MS) {
    encoderYMode = !encoderYMode;
    digitalWrite(modeLedPin, encoderYMode ? HIGH : LOW); // LED ON in Y-mode
    lastToggleMs = now;
  }
  modeBtnPrev = modeDown;

  // --- Encoder -> mouse X or Y ---
  noInterrupts();
  long pos = encoderPos;
  interrupts();
  long delta = pos - lastReportedPos;
  if (delta != 0) {
    int8_t step = clipInt8(delta);
    if (encoderYMode) {
      Mouse.move(0, step, 0);  // Y axis (positive = down)
    } else {
      Mouse.move(step, 0, 0);  // X axis
    }
    lastReportedPos += step;
  }

  // --- D-pad -> GamePad hat switch ---
  if ((uint8_t)(now - lastDpadPoll) >= DPAD_POLL_MS) {
    lastDpadPoll = now;
    const bool up    = (digitalRead(dpadUpPin)    == LOW);
    const bool down  = (digitalRead(dpadDownPin)  == LOW);
    const bool left  = (digitalRead(dpadLeftPin)  == LOW);
    const bool right = (digitalRead(dpadRightPin) == LOW);

    int hat = -1; // centered
    if (up && right)        hat = 45;
    else if (down && right) hat = 135;
    else if (down && left)  hat = 225;
    else if (up && left)    hat = 315;
    else if (up)            hat = 0;
    else if (right)         hat = 90;
    else if (down)          hat = 180;
    else if (left)          hat = 270;

    if (hat != prevHat) {
      Joystick.setHatSwitch(0, hat);
      Joystick.sendState();
      prevHat = hat;
    }
  }

  // --- GamePad buttons 1, 2, 3, and 4 ---
  bool anyGpChanged = false;
  for (uint8_t i = 0; i < 4; i++) {
    bool down = (digitalRead(gpBtnPins[i]) == LOW);
    if (down != gpPrev[i]) {
      Joystick.setButton(i, down);   // index 0..3 -> Buttons 1..4
      gpPrev[i] = down;
      anyGpChanged = true;
    }
  }
  if (anyGpChanged) {
    Joystick.sendState();
  }

  // --- Mouse Left / Right buttons ---
  bool lDown = (digitalRead(mouseLeftPin)  == LOW);
  bool rDown = (digitalRead(mouseRightPin) == LOW);

  if (lDown && !mLeftPrev)      Mouse.press(MOUSE_LEFT);
  else if (!lDown && mLeftPrev) Mouse.release(MOUSE_LEFT);
  mLeftPrev = lDown;

  if (rDown && !mRightPrev)      Mouse.press(MOUSE_RIGHT);
  else if (!rDown && mRightPrev) Mouse.release(MOUSE_RIGHT);
  mRightPrev = rDown;
}


