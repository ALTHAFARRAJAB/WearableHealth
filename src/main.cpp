#define BLYNK_TEMPLATE_ID   "TMPLGM5k3SqQC"
#define BLYNK_TEMPLATE_NAME "Wearable Health"
#define BLYNK_AUTH_TOKEN    "6trC2VcRRP__ddlTp3zKygQQlX_1dou_"
#define Blynk_PRINT Serial

#include <Arduino.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <MPU6050.h>

// =========================================================================
// FORWARD DECLARATIONS (Deklarasi fungsi agar terbaca oleh compiler C++)
// =========================================================================
void readAndProcessSensors();
void detectEvents();
void driveActuators(unsigned long currentMillis);
void publishToBlynk();

// WiFi & Blynk Credentials
char ssid[] = "Wokwi-GUEST";
char pass[] = "";

// Pin Mapping
const int pinBPProxy = 34;      // Potensiometer 1 (HR/BP)
const int pinGlucProxy = 35;    // Potensiometer 2 (Glukosa)
const int pinLedRed = 12;       // LED Indikator Alert/Emergency
const int pinLedGreen = 14;     // LED Indikator Normal
const int pinBuzzer = 27;       // Buzzer Alarm

MPU6050 mpu;

// State Enum
enum SystemState { IDLE, MONITOR, ALERT, EMERGENCY, RECOVERY };
SystemState currentState = MONITOR;

// Timing variables (Non-blocking millis)
unsigned long previousSensorMillis = 0;
const long sensorInterval = 500;       // Baca sensor tiap 500ms
unsigned long previousBlynkMillis = 0;
const long blynkInterval = 2000;       // Kirim ke Blynk tiap 2 detik

unsigned long alertStartTime = 0;      // Durasi konfirmasi Emergency
bool eventSentAlert = false;
bool eventSentEmergency = false;

// Buzzer & LED blinking variables
unsigned long previousBlinkMillis = 0;
bool ledState = false;

// Variabel Global Sensor
int bpValue = 0;
int glucValue = 0;
float pitch = 0;

void setup() {
  Serial.begin(115200);
  Wire.begin();
  
  pinMode(pinLedRed, OUTPUT);
  pinMode(pinLedGreen, OUTPUT);
  pinMode(pinBuzzer, OUTPUT);

  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("MPU6050 tidak terdeteksi, periksa wiring I2C!");
  }

  // Koneksi ke Blynk Cloud khusus region SGP1 (Singapura)
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass, "sgp1.blynk.cloud", 80);
  Serial.println("Sistem Wearable Kesehatan Dimulai...");
}

void loop() {
  Blynk.run();
  unsigned long currentMillis = millis();

  // Task 1: Pembacaan Sensor berkala (Non-blocking)
  if (currentMillis - previousSensorMillis >= sensorInterval) {
    previousSensorMillis = currentMillis;
    readAndProcessSensors();
  }

  // Task 2: Publish ke Blynk Cloud
  if (currentMillis - previousBlynkMillis >= blynkInterval) {
    previousBlynkMillis = currentMillis;
    publishToBlynk();
  }

  // Task 3: Kendalikan Aktuator
  driveActuators(currentMillis);
}

void readAndProcessSensors() {
  int rawBP = analogRead(pinBPProxy);
  bpValue = (bpValue * 0.7) + (rawBP * 0.3);

  int rawGluc = analogRead(pinGlucProxy);
  glucValue = (glucValue * 0.7) + (rawGluc * 0.3);

  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  pitch = atan2(ay, az) * 180.0 / PI;

  detectEvents();
}

void detectEvents() {
  bool conditionBP = (bpValue > 3000);       
  bool conditionGluc = (glucValue > 3200);
  bool conditionFall = (abs(pitch) > 60.0);    

  int activeFaults = 0;
  if (conditionBP) activeFaults++;
  if (conditionGluc) activeFaults++;
  if (conditionFall) activeFaults++;

  if (activeFaults >= 2) {
    if (currentState != EMERGENCY) {
      if (alertStartTime == 0) {
        alertStartTime = millis();
      } else if (millis() - alertStartTime >= 2000) { 
        currentState = EMERGENCY;
        if (!eventSentEmergency) {
          Blynk.logEvent("emergency", "Kombinasi parameter kritis terdeteksi (Jatuh + Vitals Abnormal)!");
          eventSentEmergency = true;
        }
      }
    }
  } else if (activeFaults == 1) {
    currentState = ALERT;
    alertStartTime = 0;
    if (!eventSentAlert) {
      Blynk.logEvent("alert", "Peringatan: Salah satu parameter melewati batas normal.");
      eventSentAlert = true;
    }
  } else {
    if (bpValue < 2700 && glucValue < 2900 && abs(pitch) < 30.0) {
      currentState = RECOVERY;
      alertStartTime = 0;
      eventSentAlert = false;
      eventSentEmergency = false;
    } else if (currentState != EMERGENCY && currentState != ALERT) {
      currentState = MONITOR;
    }
  }

  Serial.print("Millis: "); Serial.print(millis());
  Serial.print(" | BP_Proxy: "); Serial.print(bpValue);
  Serial.print(" | Gluc_Proxy: "); Serial.print(glucValue);
  Serial.print(" | Pitch: "); Serial.print(pitch);
  Serial.print(" | State: ");
  if (currentState == MONITOR) Serial.println("MONITOR (Normal)");
  else if (currentState == ALERT) Serial.println("ALERT (Warning)");
  else if (currentState == EMERGENCY) Serial.println("EMERGENCY (Critical!)");
  else if (currentState == RECOVERY) Serial.println("RECOVERY");
}

void driveActuators(unsigned long currentMillis) {
  if (currentState == MONITOR || currentState == RECOVERY) {
    digitalWrite(pinLedGreen, HIGH);
    digitalWrite(pinLedRed, LOW);
    noTone(pinBuzzer);
  } else if (currentState == ALERT) {
    digitalWrite(pinLedGreen, LOW);
    noTone(pinBuzzer);
    
    if (currentMillis - previousBlinkMillis >= 500) {
      previousBlinkMillis = currentMillis;
      ledState = !ledState;
      digitalWrite(pinLedRed, ledState);
    }
  } else if (currentState == EMERGENCY) {
    digitalWrite(pinLedGreen, LOW);
    
    if (currentMillis - previousBlinkMillis >= 150) {
      previousBlinkMillis = currentMillis;
      ledState = !ledState;
      digitalWrite(pinLedRed, ledState);
      
      if (ledState) {
        tone(pinBuzzer, 2000);
      } else {
        noTone(pinBuzzer);
      }
    }
  }
}

void publishToBlynk() {
  Blynk.virtualWrite(V0, bpValue);
  Blynk.virtualWrite(V1, glucValue);
  Blynk.virtualWrite(V2, pitch);
  
  if (currentState == MONITOR) Blynk.virtualWrite(V3, "NORMAL");
  else if (currentState == ALERT) Blynk.virtualWrite(V3, "ALERT");
  else if (currentState == EMERGENCY) Blynk.virtualWrite(V3, "EMERGENCY");
  else Blynk.virtualWrite(V3, "RECOVERY");
}