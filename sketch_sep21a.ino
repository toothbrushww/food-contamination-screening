#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_TCS34725.h>
#include <PWMServo.h>

// =====================================================
// I2C BUS
// =====================================================

#define I2C_SDA 18
#define I2C_SCL 19

#define TCS_ADDRESS  0x29
#define OLED_ADDRESS 0x3C


// =====================================================
// OLED
// =====================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1
);


// =====================================================
// TCS34725
// =====================================================

Adafruit_TCS34725 tcs(
  TCS34725_INTEGRATIONTIME_50MS,
  TCS34725_GAIN_4X
);


// =====================================================
// PINS
// =====================================================

#define TCS_LED_PIN    2
#define UV_RELAY_PIN   3
#define IR_SENSOR_PIN  4
#define SERVO_PIN      5

#define MOTOR_AIN1     6
#define MOTOR_AIN2     7
#define MOTOR_PWM      8
#define MOTOR_STBY     9

#define BUTTON_PIN     10

#define STATUS_LED     13

#define BPW34_PIN      A0


// =====================================================
// RELAY LOGIC
// =====================================================

// ACTIVE LOW relay
// LOW  = UV ON
// HIGH = UV OFF

#define RELAY_ON  LOW
#define RELAY_OFF HIGH


// =====================================================
// IR SENSOR
// =====================================================

#define IR_ACTIVE_LEVEL LOW

#define IR_REQUIRED_DETECTIONS 5
#define IR_SAMPLE_INTERVAL     20
#define IR_COOLDOWN_MS         1500


// =====================================================
// MOTOR
// =====================================================

#define MOTOR_SPEED 180
#define CONVEYOR_RUN_TIME 1000


// =====================================================
// SERVO
// =====================================================

#define SERVO_PASS_ANGLE     20
#define SERVO_FLAGGED_ANGLE 105
#define SERVO_HOLD_TIME      700


// =====================================================
// UV THRESHOLD
// =====================================================

// TESTING VALUE ONLY.
//
// This is NOT a validated contamination threshold.
// It is intentionally low so you can test the
// fluorescence response using riboflavin.
//
// After measuring your empty/background and reference
// samples, this should be recalibrated.

#define UV_FLAG_THRESHOLD 4


// =====================================================
// GLOBALS
// =====================================================

PWMServo sortingServo;


// =====================================================
// OBJECT DETECTION
// =====================================================

int activeCount = 0;

unsigned long lastIRSample = 0;
unsigned long lastObjectTime = 0;


// =====================================================
// DEVICE STATUS
// =====================================================

bool tcsDetected = false;
bool oledDetected = false;


// =====================================================
// RGB VALUES
// =====================================================

uint16_t rawR = 0;
uint16_t rawG = 0;
uint16_t rawB = 0;
uint16_t rawC = 0;

uint16_t darkR = 0;
uint16_t darkG = 0;
uint16_t darkB = 0;
uint16_t darkC = 0;

int correctedR = 0;
int correctedG = 0;
int correctedB = 0;
int correctedC = 0;


// =====================================================
// UV VALUES
// =====================================================

int uvRaw = 0;
int uvDark = 0;
int uvCorrected = 0;


// =====================================================
// RESULT
// =====================================================

bool objectFlagged = false;


// =====================================================
// STATE MACHINE
// =====================================================

enum SystemState
{
  IDLE,
  SCANNING,
  CLASSIFYING,
  ACTUATING
};

SystemState state = IDLE;


// =====================================================
// FUNCTION DECLARATIONS
// =====================================================

void scanI2CBus();

void uvOn();
void uvOff();

void motorStop();
void motorForward();
void motorReverse();

bool objectDetected();

void readColourSensor();
void readUVSensor();

void performScan();

void showOLED(
  const char *line1,
  const char *line2 = "",
  const char *line3 = ""
);

void showMeasurements();


// =====================================================
// I2C SCANNER
// =====================================================

void scanI2CBus()
{
  Serial.println();
  Serial.println("========================================");
  Serial.println("I2C SCANNER");
  Serial.println("SDA = 18");
  Serial.println("SCL = 19");
  Serial.println("========================================");

  int devicesFound = 0;

  for (uint8_t address = 1; address < 127; address++)
  {
    Wire.beginTransmission(address);

    uint8_t error = Wire.endTransmission();

    if (error == 0)
    {
      Serial.print("Device found at 0x");

      if (address < 16)
      {
        Serial.print("0");
      }

      Serial.println(address, HEX);

      devicesFound++;
    }
  }

  if (devicesFound == 0)
  {
    Serial.println("NO I2C DEVICES FOUND!");
  }
  else
  {
    Serial.print("Total devices found: ");
    Serial.println(devicesFound);
  }

  Serial.println();
  Serial.println("Expected:");
  Serial.println("TCS34725 -> 0x29");
  Serial.println("OLED     -> 0x3C");
  Serial.println("========================================");
}


// =====================================================
// SETUP
// =====================================================

void setup()
{
  Serial.begin(115200);

  delay(1500);

  Serial.println();
  Serial.println("========================================");
  Serial.println(" FOOD CONTAMINATION SCREENING SYSTEM");
  Serial.println("========================================");


  // ===================================================
  // PIN SETUP
  // ===================================================

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


  // ===================================================
  // INITIAL SAFETY
  // ===================================================

  // UV OFF
  digitalWrite(UV_RELAY_PIN, RELAY_OFF);

  // TCS illumination OFF
  digitalWrite(TCS_LED_PIN, LOW);

  // Motor OFF
  analogWrite(MOTOR_PWM, 0);

  digitalWrite(MOTOR_AIN1, LOW);
  digitalWrite(MOTOR_AIN2, LOW);

  // Driver enabled
  digitalWrite(MOTOR_STBY, HIGH);

  // Status LED OFF
  digitalWrite(STATUS_LED, LOW);


  // ===================================================
  // SERVO
  // ===================================================

  sortingServo.attach(SERVO_PIN);

  sortingServo.write(SERVO_PASS_ANGLE);

  delay(300);


  // ===================================================
  // START I2C
  // ===================================================

  Serial.println();
  Serial.println("Starting I2C...");

  Wire.setSDA(I2C_SDA);
  Wire.setSCL(I2C_SCL);

  Wire.begin();

  Wire.setClock(100000);

  delay(300);


  // ===================================================
  // I2C SCAN
  // ===================================================

  scanI2CBus();


  // ===================================================
  // TCS34725
  // ===================================================

  Serial.println();
  Serial.println("Checking TCS colour sensor...");

  tcsDetected = tcs.begin(TCS_ADDRESS, &Wire);

  if (tcsDetected)
  {
    Serial.println("TCS34725 detected at 0x29");
  }
  else
  {
    Serial.println("ERROR: TCS34725 NOT detected!");
  }


  // ===================================================
  // OLED
  // ===================================================

  Serial.println();
  Serial.println("Checking OLED...");

  oledDetected = display.begin(
    SSD1306_SWITCHCAPVCC,
    OLED_ADDRESS
  );

  if (oledDetected)
  {
    Serial.println("OLED detected at 0x3C");


    // -------------------------------------------------
    // Startup display
    // -------------------------------------------------

    display.clearDisplay();

    display.dim(false);

    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(2);

    display.setCursor(0, 0);
    display.println("SYSTEM");

    display.setCursor(0, 20);
    display.println("START");

    display.setTextSize(1);

    display.setCursor(0, 48);
    display.println("I2C 18/19");

    display.display();

    delay(2000);


    // -------------------------------------------------
    // Device status
    // -------------------------------------------------

    display.clearDisplay();

    display.setTextSize(1);

    display.setCursor(0, 0);
    display.println("FOOD SCREENING");

    display.setCursor(0, 15);

    if (tcsDetected)
      display.println("TCS34725: OK");
    else
      display.println("TCS34725: ERROR");

    display.setCursor(0, 30);
    display.println("OLED: OK");

    display.setCursor(0, 45);
    display.println("Waiting for item...");

    display.display();
  }
  else
  {
    Serial.println("ERROR: OLED NOT detected!");
  }


  // ===================================================
  // FINAL SAFETY
  // ===================================================

  uvOff();

  digitalWrite(TCS_LED_PIN, LOW);

  motorStop();


  Serial.println();
  Serial.println("========================================");
  Serial.println("SYSTEM READY");
  Serial.println("========================================");
  Serial.println("Waiting for object...");
  Serial.println();
}


// =====================================================
// MAIN LOOP
// =====================================================

void loop()
{
  // ===================================================
  // IDLE
  // ===================================================

  if (state == IDLE)
  {
    // UV MUST ALWAYS BE OFF IN IDLE
    uvOff();

    // TCS light OFF
    digitalWrite(TCS_LED_PIN, LOW);

    // Conveyor OFF
    motorStop();


    // -------------------------------------------------
    // AUTOMATIC OBJECT DETECTION
    // -------------------------------------------------

    if (objectDetected())
    {
      Serial.println();
      Serial.println("----------------------------------------");
      Serial.println("VALID OBJECT DETECTED");
      Serial.println("----------------------------------------");

      lastObjectTime = millis();

      digitalWrite(STATUS_LED, HIGH);

      state = SCANNING;

      delay(100);
    }


    // -------------------------------------------------
    // MANUAL BUTTON
    // -------------------------------------------------

    if (digitalRead(BUTTON_PIN) == LOW)
    {
      delay(50);

      if (digitalRead(BUTTON_PIN) == LOW)
      {
        Serial.println();
        Serial.println("----------------------------------------");
        Serial.println("MANUAL SCAN");
        Serial.println("----------------------------------------");

        while (digitalRead(BUTTON_PIN) == LOW)
        {
          delay(10);
        }

        digitalWrite(STATUS_LED, HIGH);

        state = SCANNING;

        delay(100);
      }
    }
  }


  // ===================================================
  // SCANNING
  // ===================================================

  if (state == SCANNING)
  {
    performScan();

    state = CLASSIFYING;
  }


  // ===================================================
  // CLASSIFICATION
  // ===================================================

  if (state == CLASSIFYING)
  {
    Serial.println();
    Serial.println("========================================");
    Serial.println("CLASSIFICATION");
    Serial.println("========================================");

    Serial.print("UV corrected = ");
    Serial.println(uvCorrected);

    Serial.print("UV threshold = ");
    Serial.println(UV_FLAG_THRESHOLD);


    if (uvCorrected >= UV_FLAG_THRESHOLD)
    {
      objectFlagged = true;

      Serial.println("RESULT: FLAGGED");
    }
    else
    {
      objectFlagged = false;

      Serial.println("RESULT: PASS");
    }

    Serial.println("========================================");


    // -------------------------------------------------
    // SHOW RESULT
    // -------------------------------------------------

    showMeasurements();

    delay(1500);

    state = ACTUATING;
  }


  // ===================================================
  // ACTUATING
  // ===================================================

  if (state == ACTUATING)
  {
    // -------------------------------------------------
    // UV MUST BE OFF
    // -------------------------------------------------

    uvOff();


    // -------------------------------------------------
    // SERVO
    // -------------------------------------------------

    if (objectFlagged)
    {
      Serial.println("Servo -> FLAGGED");

      sortingServo.write(SERVO_FLAGGED_ANGLE);

      delay(SERVO_HOLD_TIME);

      sortingServo.write(SERVO_PASS_ANGLE);
    }
    else
    {
      Serial.println("Servo -> PASS");

      sortingServo.write(SERVO_PASS_ANGLE);

      delay(300);
    }


    // -------------------------------------------------
    // CONVEYOR
    // -------------------------------------------------

    Serial.println("Running conveyor...");

    motorForward();

    delay(CONVEYOR_RUN_TIME);

    motorStop();


    // -------------------------------------------------
    // FINAL SAFETY
    // -------------------------------------------------

    uvOff();

    digitalWrite(TCS_LED_PIN, LOW);

    motorStop();

    digitalWrite(STATUS_LED, LOW);


    // -------------------------------------------------
    // RETURN TO IDLE
    // -------------------------------------------------

    Serial.println();
    Serial.println("Object processed.");
    Serial.println("Waiting for next object...");
    Serial.println();

    state = IDLE;

    delay(IR_COOLDOWN_MS);
  }
}


// =====================================================
// OBJECT DETECTION
// =====================================================

bool objectDetected()
{
  unsigned long now = millis();

  if (now - lastIRSample < IR_SAMPLE_INTERVAL)
  {
    return false;
  }

  lastIRSample = now;


  int sensorState = digitalRead(IR_SENSOR_PIN);


  Serial.print("IR = ");

  if (sensorState == IR_ACTIVE_LEVEL)
  {
    Serial.println("ACTIVE");

    activeCount++;
  }
  else
  {
    Serial.println("CLEAR");

    activeCount = 0;
  }


  // ---------------------------------------------------
  // Require 5 consecutive active readings
  // ---------------------------------------------------

  if (activeCount >= IR_REQUIRED_DETECTIONS)
  {
    activeCount = 0;

    if (millis() - lastObjectTime < IR_COOLDOWN_MS)
    {
      return false;
    }

    return true;
  }

  return false;
}


// =====================================================
// COMPLETE SCAN
// =====================================================

void performScan()
{
  Serial.println();
  Serial.println("========================================");
  Serial.println("STARTING SCAN");
  Serial.println("========================================");


  // ---------------------------------------------------
  // SAFETY
  // ---------------------------------------------------

  uvOff();

  digitalWrite(TCS_LED_PIN, LOW);

  motorStop();


  // ---------------------------------------------------
  // RGB
  // ---------------------------------------------------

  showOLED(
    "SCANNING",
    "RGB SENSOR",
    "Please wait..."
  );

  readColourSensor();


  // ---------------------------------------------------
  // UV
  // ---------------------------------------------------

  showOLED(
    "SCANNING",
    "UV SENSOR",
    "Please wait..."
  );

  readUVSensor();


  Serial.println();
  Serial.println("SCAN COMPLETE");
}


// =====================================================
// COLOUR SENSOR
// =====================================================

void readColourSensor()
{
  Serial.println();
  Serial.println("Reading TCS34725...");


  if (!tcsDetected)
  {
    Serial.println("TCS34725 unavailable.");
    return;
  }


  // ---------------------------------------------------
  // DARK READING
  // ---------------------------------------------------

  digitalWrite(TCS_LED_PIN, LOW);

  delay(100);

  tcs.getRawData(
    &darkR,
    &darkG,
    &darkB,
    &darkC
  );


  Serial.println("Dark reading:");

  Serial.print("R = ");
  Serial.println(darkR);

  Serial.print("G = ");
  Serial.println(darkG);

  Serial.print("B = ");
  Serial.println(darkB);

  Serial.print("C = ");
  Serial.println(darkC);


  // ---------------------------------------------------
  // LIGHT ON
  // ---------------------------------------------------

  digitalWrite(TCS_LED_PIN, HIGH);

  delay(150);


  tcs.getRawData(
    &rawR,
    &rawG,
    &rawB,
    &rawC
  );


  // ---------------------------------------------------
  // LIGHT OFF
  // ---------------------------------------------------

  digitalWrite(TCS_LED_PIN, LOW);


  // ---------------------------------------------------
  // DARK SUBTRACTION
  // ---------------------------------------------------

  correctedR = (int)rawR - (int)darkR;
  correctedG = (int)rawG - (int)darkG;
  correctedB = (int)rawB - (int)darkB;
  correctedC = (int)rawC - (int)darkC;


  if (correctedR < 0)
    correctedR = 0;

  if (correctedG < 0)
    correctedG = 0;

  if (correctedB < 0)
    correctedB = 0;

  if (correctedC < 0)
    correctedC = 0;


  // ---------------------------------------------------
  // SERIAL
  // ---------------------------------------------------

  Serial.println();
  Serial.println("RGB VALUES");

  Serial.print("R = ");
  Serial.println(correctedR);

  Serial.print("G = ");
  Serial.println(correctedG);

  Serial.print("B = ");
  Serial.println(correctedB);

  Serial.print("C = ");
  Serial.println(correctedC);
}


// =====================================================
// UV SENSOR
// =====================================================

void readUVSensor()
{
  Serial.println();
  Serial.println("Starting UV measurement...");


  // ---------------------------------------------------
  // UV OFF
  // ---------------------------------------------------

  uvOff();

  delay(300);


  // ---------------------------------------------------
  // DARK / BACKGROUND AVERAGE
  // ---------------------------------------------------

  long darkSum = 0;

  for (int i = 0; i < 50; i++)
  {
    darkSum += analogRead(BPW34_PIN);

    delay(2);
  }

  uvDark = darkSum / 50;


  Serial.print("UV dark = ");
  Serial.println(uvDark);


  // ---------------------------------------------------
  // UV ON
  // ---------------------------------------------------

  Serial.println("UV ON");

  uvOn();

  delay(500);


  // ---------------------------------------------------
  // UV AVERAGE
  // ---------------------------------------------------

  long uvSum = 0;

  for (int i = 0; i < 50; i++)
  {
    uvSum += analogRead(BPW34_PIN);

    delay(2);
  }

  uvRaw = uvSum / 50;


  // ---------------------------------------------------
  // UV OFF IMMEDIATELY
  // ---------------------------------------------------

  uvOff();

  Serial.println("UV OFF");


  // ---------------------------------------------------
  // CORRECTED SIGNAL
  // ---------------------------------------------------

  uvCorrected = uvRaw - uvDark;


  if (uvCorrected < 0)
  {
    uvCorrected = 0;
  }


  // ---------------------------------------------------
  // SERIAL
  // ---------------------------------------------------

  Serial.println();
  Serial.println("UV RESULTS");

  Serial.print("UV dark      = ");
  Serial.println(uvDark);

  Serial.print("UV raw       = ");
  Serial.println(uvRaw);

  Serial.print("UV corrected = ");
  Serial.println(uvCorrected);

  Serial.print("UV threshold = ");
  Serial.println(UV_FLAG_THRESHOLD);
}


// =====================================================
// UV ON
// =====================================================

void uvOn()
{
  digitalWrite(UV_RELAY_PIN, RELAY_ON);

  delay(50);
}


// =====================================================
// UV OFF
// =====================================================

void uvOff()
{
  digitalWrite(UV_RELAY_PIN, RELAY_OFF);
}


// =====================================================
// MOTOR STOP
// =====================================================

void motorStop()
{
  analogWrite(MOTOR_PWM, 0);

  digitalWrite(MOTOR_AIN1, LOW);
  digitalWrite(MOTOR_AIN2, LOW);

  digitalWrite(MOTOR_STBY, HIGH);
}


// =====================================================
// MOTOR FORWARD
// =====================================================

void motorForward()
{
  digitalWrite(MOTOR_STBY, HIGH);

  digitalWrite(MOTOR_AIN1, HIGH);
  digitalWrite(MOTOR_AIN2, LOW);

  analogWrite(MOTOR_PWM, MOTOR_SPEED);
}


// =====================================================
// MOTOR REVERSE
// =====================================================

void motorReverse()
{
  digitalWrite(MOTOR_STBY, HIGH);

  digitalWrite(MOTOR_AIN1, LOW);
  digitalWrite(MOTOR_AIN2, HIGH);

  analogWrite(MOTOR_PWM, MOTOR_SPEED);
}


// =====================================================
// OLED MESSAGE
// =====================================================

void showOLED(
  const char *line1,
  const char *line2,
  const char *line3
)
{
  if (!oledDetected)
  {
    return;
  }


  display.clearDisplay();

  display.dim(false);

  display.setTextColor(SSD1306_WHITE);


  // ---------------------------------------------------
  // LINE 1
  // ---------------------------------------------------

  display.setTextSize(2);

  display.setCursor(0, 0);

  display.println(line1);


  // ---------------------------------------------------
  // LINE 2
  // ---------------------------------------------------

  display.setTextSize(1);

  display.setCursor(0, 28);

  display.println(line2);


  // ---------------------------------------------------
  // LINE 3
  // ---------------------------------------------------

  display.setCursor(0, 44);

  display.println(line3);


  display.display();
}


// =====================================================
// OLED MEASUREMENTS + WARNING SYMBOL
// =====================================================

void showMeasurements()
{
  if (!oledDetected)
  {
    return;
  }


  display.clearDisplay();

  display.dim(false);

  display.setTextColor(SSD1306_WHITE);


  // =================================================
  // PASS SCREEN
  // =================================================

  if (!objectFlagged)
  {
    display.setTextSize(1);


    // RGB
    display.setCursor(0, 0);

    display.print("R:");
    display.print(correctedR);

    display.print(" G:");
    display.print(correctedG);


    display.setCursor(0, 12);

    display.print("B:");
    display.print(correctedB);

    display.print(" C:");
    display.print(correctedC);


    // UV
    display.setCursor(0, 26);

    display.print("UV:");
    display.print(uvCorrected);


    // PASS
    display.setTextSize(2);

    display.setCursor(0, 43);

    display.println("PASS");
  }


  // =================================================
  // FLAGGED SCREEN
  // =================================================

  else
  {
    // ------------------------------------------------
    // WARNING TRIANGLE
    // ------------------------------------------------

    display.drawTriangle(
      16, 6,
      0, 45,
      32, 45,
      SSD1306_WHITE
    );


    // ------------------------------------------------
    // EXCLAMATION MARK
    // ------------------------------------------------

    display.fillRect(
      14, 18,
      4, 16,
      SSD1306_WHITE
    );


    // ------------------------------------------------
    // DOT
    // ------------------------------------------------

    display.fillCircle(
      16,
      39,
      2,
      SSD1306_WHITE
    );


    // ------------------------------------------------
    // WARNING TEXT
    // ------------------------------------------------

    display.setTextSize(2);

    display.setCursor(40, 3);

    display.println("WARNING");


    display.setCursor(40, 27);

    display.println("FLAGGED");


    // ------------------------------------------------
    // UV VALUE
    // ------------------------------------------------

    display.setTextSize(1);

    display.setCursor(40, 51);

    display.print("UV:");
    display.print(uvCorrected);
  }


  // --------------------------------------------------
  // SEND TO SCREEN
  // --------------------------------------------------

  display.display();
}