// ============================================================
// TEENSY 4.1 - AI FOOD SCREENING CONTROLLER
// Jetson Nano <-> Teensy USB Serial
// ============================================================

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_TCS34725.h>
#include <PWMServo.h>

// ============================================================
// I2C
// ============================================================

#define I2C_SDA        18
#define I2C_SCL        19

#define TCS_ADDRESS    0x29
#define OLED_ADDRESS   0x3C

// ============================================================
// OLED
// ============================================================

#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1
);

// ============================================================
// TCS34725
// ============================================================

Adafruit_TCS34725 tcs(
  TCS34725_INTEGRATIONTIME_50MS,
  TCS34725_GAIN_4X
);

// ============================================================
// PINS
// ============================================================

#define TCS_LED_PIN     2
#define UV_RELAY_PIN    3
#define IR_SENSOR_PIN   4
#define SERVO_PIN       5

#define MOTOR_AIN1      6
#define MOTOR_AIN2      7
#define MOTOR_PWM       8
#define MOTOR_STBY      9

#define BUTTON_PIN      10
#define STATUS_LED      13

#define BPW34_PIN       A0

// ============================================================
// RELAY
// ============================================================

#define RELAY_ON        LOW
#define RELAY_OFF       HIGH

// ============================================================
// IR SENSOR
// ============================================================

#define IR_ACTIVE_LEVEL         LOW
#define IR_REQUIRED_DETECTIONS  5
#define IR_SAMPLE_INTERVAL      20
#define IR_COOLDOWN_MS          1500

// ============================================================
// SCAN
// ============================================================

#define SCAN_TIME_MS            700
#define JETSON_TIMEOUT_MS       10000

// ============================================================
// MOTOR
// ============================================================

#define MOTOR_SPEED             180
#define CONVEYOR_RUN_TIME       1000

// ============================================================
// SERVO
// ============================================================

#define SERVO_PASS_ANGLE        20
#define SERVO_REJECT_ANGLE      105
#define SERVO_HOLD_TIME         700

// ============================================================
// GLOBALS
// ============================================================

PWMServo sortingServo;

bool oledDetected = false;
bool tcsDetected = false;

int activeCount = 0;

unsigned long lastIRSample = 0;
unsigned long lastObjectTime = 0;
unsigned long scanStartTime = 0;
unsigned long resultWaitStart = 0;

bool resultReceived = false;
bool vegetableDetected = false;

char vegetableName[32] = "UNKNOWN";
int vegetableConfidence = 0;

// ============================================================
// STATE MACHINE
// ============================================================

enum SystemState
{
  IDLE,
  WAITING_FOR_JETSON,
  SCANNING,
  WAITING_FOR_RESULT,
  ACTUATING
};

SystemState state = IDLE;

// ============================================================
// FUNCTION DECLARATIONS
// ============================================================

bool objectDetected();
void processJetsonSerial();

void startScan();
void processScan();
void handleResult();

void displayMessage(
  const char *line1,
  const char *line2,
  const char *line3
);

void showWaitingScreen();
void showScanningScreen();
void showJetsonWaitingScreen();
void showResultScreen();
void showTimeoutScreen();

void colourLightOn();
void colourLightOff();

void uvOn();
void uvOff();

void motorStop();
void motorForward();

void sendMessage(const char *msg);
void resetForNextObject();

// ============================================================
// SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);

  delay(1500);

  Serial.println();
  Serial.println("========================================");
  Serial.println("AI FOOD SCREENING CONTROLLER");
  Serial.println("TEENSY 4.1");
  Serial.println("========================================");

  // ----------------------------------------------------------
  // PINS
  // ----------------------------------------------------------

  pinMode(TCS_LED_PIN, OUTPUT);
  pinMode(UV_RELAY_PIN, OUTPUT);

  pinMode(IR_SENSOR_PIN, INPUT_PULLUP);

  pinMode(MOTOR_AIN1, OUTPUT);
  pinMode(MOTOR_AIN2, OUTPUT);
  pinMode(MOTOR_PWM, OUTPUT);
  pinMode(MOTOR_STBY, OUTPUT);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  pinMode(STATUS_LED, OUTPUT);

  pinMode(BPW34_PIN, INPUT);

  // ----------------------------------------------------------
  // SAFE START
  // ----------------------------------------------------------

  colourLightOff();
  uvOff();
  motorStop();

  digitalWrite(STATUS_LED, LOW);

  // ----------------------------------------------------------
  // SERVO
  // ----------------------------------------------------------

  sortingServo.attach(SERVO_PIN);
  sortingServo.write(SERVO_PASS_ANGLE);

  delay(300);

  // ----------------------------------------------------------
  // I2C
  // ----------------------------------------------------------

  Wire.setSDA(I2C_SDA);
  Wire.setSCL(I2C_SCL);
  Wire.begin();
  Wire.setClock(100000);

  delay(300);

  // ----------------------------------------------------------
  // TCS34725
  // ----------------------------------------------------------

  Serial.println("Checking TCS34725...");

  tcsDetected = tcs.begin(
    TCS_ADDRESS,
    &Wire
  );

  if (tcsDetected)
  {
    Serial.println("TCS34725 detected at 0x29");
  }
  else
  {
    Serial.println("TCS34725 NOT detected");
  }

  // ----------------------------------------------------------
  // OLED
  // ----------------------------------------------------------

  Serial.println("Checking OLED...");

  oledDetected = display.begin(
    SSD1306_SWITCHCAPVCC,
    OLED_ADDRESS
  );

  if (oledDetected)
  {
    Serial.println("OLED detected at 0x3C");

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(2);
    display.setCursor(0, 8);
    display.println("READY");

    display.setTextSize(1);
    display.setCursor(0, 36);
    display.println("Jetson connected");

    display.display();

    delay(1500);
  }
  else
  {
    Serial.println("OLED NOT detected");
  }

  showWaitingScreen();

  Serial.println();
  Serial.println("SYSTEM READY");
  Serial.println("Waiting for object...");
  Serial.println();
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop()
{
  processJetsonSerial();

  // ==========================================================
  // IDLE
  // ==========================================================

  if (state == IDLE)
  {
    colourLightOff();
    uvOff();
    motorStop();

    // --------------------------------------------------------
    // IR OBJECT DETECTION
    // --------------------------------------------------------

    if (objectDetected())
    {
      Serial.println();
      Serial.println("----------------------------------------");
      Serial.println("OBJECT DETECTED");
      Serial.println("----------------------------------------");

      sendMessage("OBJECT_DETECTED");

      digitalWrite(STATUS_LED, HIGH);

      showJetsonWaitingScreen();

      resultWaitStart = millis();

      state = WAITING_FOR_JETSON;

      delay(100);
    }

    // --------------------------------------------------------
    // MANUAL BUTTON
    // --------------------------------------------------------

    if (digitalRead(BUTTON_PIN) == LOW)
    {
      delay(50);

      if (digitalRead(BUTTON_PIN) == LOW)
      {
        while (digitalRead(BUTTON_PIN) == LOW)
        {
          delay(10);
        }

        Serial.println("MANUAL SCAN TRIGGERED");

        sendMessage("OBJECT_DETECTED");

        digitalWrite(STATUS_LED, HIGH);

        showJetsonWaitingScreen();

        resultWaitStart = millis();

        state = WAITING_FOR_JETSON;

        delay(100);
      }
    }
  }

  // ==========================================================
  // WAITING FOR JETSON
  // ==========================================================

  if (state == WAITING_FOR_JETSON)
  {
    colourLightOff();
    uvOff();
    motorStop();

    if (millis() - resultWaitStart > JETSON_TIMEOUT_MS)
    {
      Serial.println("ERROR: Jetson timeout");

      showTimeoutScreen();

      delay(1500);

      resetForNextObject();
    }
  }

  // ==========================================================
  // SCANNING
  // ==========================================================

  if (state == SCANNING)
  {
    processScan();
  }

  // ==========================================================
  // WAITING FOR RESULT
  // ==========================================================

  if (state == WAITING_FOR_RESULT)
  {
    colourLightOff();
    uvOff();
    motorStop();

    if (resultReceived)
    {
      state = ACTUATING;
    }
    else if (millis() - resultWaitStart > JETSON_TIMEOUT_MS)
    {
      Serial.println("ERROR: ML result timeout");

      vegetableDetected = false;

      strcpy(
        vegetableName,
        "UNKNOWN"
      );

      vegetableConfidence = 0;

      state = ACTUATING;
    }
  }

  // ==========================================================
  // ACTUATING
  // ==========================================================

  if (state == ACTUATING)
  {
    handleResult();

    delay(1800);

    if (vegetableDetected)
    {
      Serial.println("Servo -> PASS position");

      sortingServo.write(
        SERVO_PASS_ANGLE
      );

      delay(300);
    }
    else
    {
      Serial.println("Servo -> REJECT position");

      sortingServo.write(
        SERVO_REJECT_ANGLE
      );

      delay(SERVO_HOLD_TIME);

      sortingServo.write(
        SERVO_PASS_ANGLE
      );
    }

    // --------------------------------------------------------
    // CONVEYOR
    // --------------------------------------------------------

    Serial.println("Running conveyor...");

    motorForward();

    delay(CONVEYOR_RUN_TIME);

    motorStop();

    colourLightOff();
    uvOff();

    digitalWrite(STATUS_LED, LOW);

    showWaitingScreen();

    resetForNextObject();

    delay(IR_COOLDOWN_MS);
  }
}

// ============================================================
// SERIAL PROCESSING
// ============================================================

void processJetsonSerial()
{
  while (Serial.available())
  {
    String cmd = Serial.readStringUntil('\n');

    cmd.trim();

    if (cmd.length() == 0)
      continue;

    Serial.print("JETSON -> ");
    Serial.println(cmd);

    // --------------------------------------------------------
    // START SCAN
    // --------------------------------------------------------

    if (cmd == "START_SCAN")
    {
      if (state == WAITING_FOR_JETSON)
      {
        startScan();
      }
    }

    // --------------------------------------------------------
    // VEGETABLE RESULT
    // --------------------------------------------------------

    else if (cmd.startsWith("RESULT:VEGETABLE:"))
    {
      // IMPORTANT:
      // "RESULT:VEGETABLE:" = 17 characters
      // ------------------------------------------------------

      String data = cmd.substring(17);

      int separator = data.lastIndexOf(':');

      if (separator > 0)
      {
        String name = data.substring(
          0,
          separator
        );

        String conf = data.substring(
          separator + 1
        );

        name.trim();
        conf.trim();

        vegetableDetected = true;

        name.toCharArray(
          vegetableName,
          sizeof(vegetableName)
        );

        vegetableConfidence = conf.toInt();
      }
      else
      {
        vegetableDetected = true;

        strcpy(
          vegetableName,
          "VEGETABLE"
        );

        vegetableConfidence = 0;
      }

      resultReceived = true;

      Serial.print("VEGETABLE = ");
      Serial.println(vegetableName);

      Serial.print("CONFIDENCE = ");
      Serial.println(vegetableConfidence);
    }

    // --------------------------------------------------------
    // NON-VEGETABLE
    // --------------------------------------------------------

    else if (cmd == "RESULT:NON_VEGETABLE")
    {
      vegetableDetected = false;

      strcpy(
        vegetableName,
        "UNKNOWN"
      );

      vegetableConfidence = 0;

      resultReceived = true;

      Serial.println(
        "RESULT = NON-VEGETABLE"
      );
    }
  }
}

// ============================================================
// START SCAN
// ============================================================

void startScan()
{
  Serial.println();
  Serial.println("JETSON REQUESTED SCAN");

  resultReceived = false;

  vegetableDetected = false;

  strcpy(
    vegetableName,
    "UNKNOWN"
  );

  vegetableConfidence = 0;

  state = SCANNING;

  scanStartTime = millis();

  showScanningScreen();

  // RGB/TCS illumination ON
  colourLightOn();

  Serial.println(
    "Colour illumination ON"
  );

  // UV ON
  uvOn();

  Serial.println(
    "UV ring ON"
  );

  sendMessage("SCAN_START");
}

// ============================================================
// SCAN PROCESS
// ============================================================

void processScan()
{
  if (
    millis() - scanStartTime
    >= SCAN_TIME_MS
  )
  {
    colourLightOff();

    Serial.println(
      "Colour illumination OFF"
    );

    uvOff();

    Serial.println(
      "UV ring OFF"
    );

    sendMessage("SCAN_DONE");

    resultWaitStart = millis();

    state = WAITING_FOR_RESULT;

    Serial.println(
      "Waiting for Jetson ML result..."
    );
  }
}

// ============================================================
// RESULT
// ============================================================

void handleResult()
{
  colourLightOff();
  uvOff();

  showResultScreen();

  Serial.println();
  Serial.println(
    "========================================"
  );

  if (vegetableDetected)
  {
    Serial.println(
      "ML RESULT: VEGETABLE"
    );

    Serial.print(
      "Vegetable: "
    );

    Serial.println(
      vegetableName
    );

    Serial.print(
      "Confidence: "
    );

    Serial.print(
      vegetableConfidence
    );

    Serial.println("%");
  }
  else
  {
    Serial.println(
      "ML RESULT: NON-VEGETABLE"
    );
  }

  Serial.println(
    "========================================"
  );
}

// ============================================================
// IR DETECTION
// ============================================================

bool objectDetected()
{
  unsigned long now = millis();

  if (
    now - lastIRSample
    < IR_SAMPLE_INTERVAL
  )
  {
    return false;
  }

  lastIRSample = now;

  int sensorState = digitalRead(
    IR_SENSOR_PIN
  );

  if (
    sensorState == IR_ACTIVE_LEVEL
  )
  {
    activeCount++;
  }
  else
  {
    activeCount = 0;
  }

  if (
    activeCount
    >= IR_REQUIRED_DETECTIONS
  )
  {
    activeCount = 0;

    if (
      millis() - lastObjectTime
      < IR_COOLDOWN_MS
    )
    {
      return false;
    }

    lastObjectTime = millis();

    return true;
  }

  return false;
}

// ============================================================
// SERIAL MESSAGE
// ============================================================

void sendMessage(
  const char *msg
)
{
  Serial.println(msg);
}

// ============================================================
// OLED GENERIC
// ============================================================

void displayMessage(
  const char *line1,
  const char *line2,
  const char *line3
)
{
  if (!oledDetected)
    return;

  display.clearDisplay();

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setTextSize(2);

  display.setCursor(0, 0);

  display.println(line1);

  display.setTextSize(1);

  display.setCursor(0, 28);

  display.println(line2);

  display.setCursor(0, 44);

  display.println(line3);

  display.display();
}

// ============================================================
// WAITING SCREEN
// ============================================================

void showWaitingScreen()
{
  if (!oledDetected)
    return;

  display.clearDisplay();

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setTextSize(2);

  display.setCursor(0, 8);

  display.println("READY");

  display.setTextSize(1);

  display.setCursor(0, 36);

  display.println(
    "Waiting for item..."
  );

  display.display();
}

// ============================================================
// JETSON WAITING
// ============================================================

void showJetsonWaitingScreen()
{
  if (!oledDetected)
    return;

  display.clearDisplay();

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setTextSize(2);

  display.setCursor(0, 0);

  display.println("DETECTED");

  display.setTextSize(1);

  display.setCursor(0, 30);

  display.println(
    "Waiting for AI..."
  );

  display.display();
}

// ============================================================
// SCANNING SCREEN
// ============================================================

void showScanningScreen()
{
  if (!oledDetected)
    return;

  display.clearDisplay();

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setTextSize(2);

  display.setCursor(0, 4);

  display.println("SCANNING");

  display.setTextSize(1);

  display.setCursor(0, 34);

  display.println("Camera + ML");

  display.display();
}

// ============================================================
// RESULT SCREEN
// ============================================================

void showResultScreen()
{
  if (!oledDetected)
    return;

  display.clearDisplay();

  display.setTextColor(
    SSD1306_WHITE
  );

  if (vegetableDetected)
  {
    display.setTextSize(2);

    display.setCursor(0, 0);

    display.println(
      "VEGETABLE"
    );

    display.setTextSize(1);

    display.setCursor(0, 27);

    display.println(
      vegetableName
    );

    display.setCursor(0, 42);

    display.print(
      "Confidence: "
    );

    display.print(
      vegetableConfidence
    );

    display.println("%");
  }
  else
  {
    display.drawTriangle(
      16, 3,
      0, 38,
      32, 38,
      SSD1306_WHITE
    );

    display.fillRect(
      14,
      14,
      4,
      14,
      SSD1306_WHITE
    );

    display.fillCircle(
      16,
      33,
      2,
      SSD1306_WHITE
    );

    display.setTextSize(2);

    display.setCursor(38, 2);

    display.println(
      "UNKNOWN"
    );

    display.setTextSize(1);

    display.setCursor(38, 30);

    display.println(
      "NON-VEGETABLE"
    );
  }

  display.display();
}

// ============================================================
// TIMEOUT
// ============================================================

void showTimeoutScreen()
{
  if (!oledDetected)
    return;

  display.clearDisplay();

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setTextSize(2);

  display.setCursor(0, 5);

  display.println("ERROR");

  display.setTextSize(1);

  display.setCursor(0, 34);

  display.println(
    "Jetson timeout"
  );

  display.display();
}

// ============================================================
// TCS LIGHT
// ============================================================

void colourLightOn()
{
  digitalWrite(
    TCS_LED_PIN,
    HIGH
  );
}

void colourLightOff()
{
  digitalWrite(
    TCS_LED_PIN,
    LOW
  );
}

// ==============================================================
// >>> INCOMPLETE: PASTE THE REST OF YOUR FILE STARTING HERE <<<
//
// The following functions are DECLARED above (and called
// throughout the state machine) but their bodies were not
// included in what was pasted into chat. The sketch will not
// compile until these are added:
//
//   void uvOn()             -> should set UV_RELAY_PIN to
//                               RELAY_ON (LOW, per your relay
//                               definitions above)
//   void uvOff()            -> should set UV_RELAY_PIN to
//                               RELAY_OFF (HIGH)
//   void motorStop()        -> should set MOTOR_STBY LOW
//                               (disable driver) and/or PWM to 0
//   void motorForward()     -> should set MOTOR_STBY HIGH,
//                               AIN1/AIN2 for forward direction,
//                               PWMA to MOTOR_SPEED
//   void resetForNextObject() -> should reset state back to
//                               IDLE and clear flags
//                               (resultReceived, vegetableDetected,
//                               vegetableName, vegetableConfidence)
//
// Paste your original file's remaining content over this comment
// block, or send it to me and I will merge it in and re-upload
// the completed file.
// ==============================================================
