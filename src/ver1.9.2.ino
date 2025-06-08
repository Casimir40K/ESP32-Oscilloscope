// ============================
//     Oscilloscope v2.2
//     Enhanced with Signal Generator
// ============================

#include <WiFi.h>
#include <WebServer.h>
//#include <Arduino.h>
#include <LittleFS.h>


// ----------------------------
//        CONFIGURATION
// ----------------------------
const char* ssid = "CasimirServers";
const char* password = "CasimirServers";
const char* ap_ssid = "ESP32-Oscilloscope";
const char* ap_password = "12345678";

#define NUM_CHANNELS 6
#define MAX_SAMPLES 500
#define LED_PIN 2
const int analogPins[NUM_CHANNELS] = { 34, 35, 32, 33, 36, 39 };
const int triggerPin = 27;
const int signalPin = 26;  // GPIO26 for signal generation

// ----------------------------
//    DATA STRUCTURES
// ----------------------------
struct SamplingConfig {
  int numSamples = 100;
  int sampleRateUs = 100;
  int channelDelayUs = 5;
  int captureIntervalMs = 50;
  int webUpdateMs = 500;
} samplingConfig;

struct SignalConfig {
  int waveformType = 2;    // 0=DC, 1=Square, 2=Sine, 3=Triangle, 4=PWM
  int amplitude = 255;     // 0-255
  int frequency = 1000;    // Hz
  int pulseWidthMs = 100;  // ms
  int dutyCycle = 50;      // %
  bool isEnabled = false;
  bool singlePulse = false;
  int dcOffset = 128;  // 0-255
} signalConfig;

struct Preset {
  const char* name;
  int samples;
  int sampleRate;
  int channelDelay;
  int captureInterval;
  int webUpdate;
};
const Preset presets[] = {
  { "High Speed", 100, 10, 1, 20, 200 },
  { "Balanced", 200, 100, 5, 50, 500 },
  { "High Resolution", 500, 1000, 10, 200, 1000 },
  { "Low Power", 50, 5000, 20, 1000, 2000 }
};
const int numPresets = sizeof(presets) / sizeof(presets[0]);

struct SignalPreset {
  const char* name;
  int waveform;
  int amplitude;
  int frequency;
  int dutyCycle;
  int dcOffset;
};
const SignalPreset signalPresets[] = {
  { "1kHz Square", 1, 255, 1000, 50, 128 },
  { "10kHz Sine", 2, 200, 10000, 50, 128 },
  { "PWM 25%", 4, 255, 1000, 25, 0 },
  { "Test Signal", 1, 128, 100, 50, 64 },
  { "DC 1.65V", 0, 128, 0, 0, 128 }
};
const int numSignalPresets = sizeof(signalPresets) / sizeof(signalPresets[0]);

// ----------------------------
//         GLOBALS
// ----------------------------
int waveformData[NUM_CHANNELS][MAX_SAMPLES];
bool wifiConnected = false;
bool apStarted = false;
bool isSnapshotMode = false;
bool triggerPressed = false;
unsigned long previousMillis = 0;
unsigned long lastWebUpdate = 0;
unsigned long lastCapture = 0;
unsigned long signalLastUpdate = 0;
volatile unsigned long pulseStartTime = 0;
volatile bool pulseActive = false;
volatile bool signalUpdateFlag = false;
//float signalPhase = 0.0;
const long blinkInterval = 500;

// Mutex for atomic signalConfig access
portMUX_TYPE signalMux = portMUX_INITIALIZER_UNLOCKED;

WebServer server(80);

// --- Helper for content types ---
String getContentType(String filename) {
  if (filename.endsWith(".html")) return "text/html";
  if (filename.endsWith(".css")) return "text/css";
  if (filename.endsWith(".js")) return "application/javascript";
  if (filename.endsWith(".json")) return "application/json";
  if (filename.endsWith(".ico")) return "image/x-icon";
  return "text/plain";
}

// --- Serve files from LittleFS ---
void handleFileRequest() {
  String path = server.uri();
  if (path == "/") path = "/index.html";
  if (!LittleFS.exists(path)) {
    server.send(404, "text/plain", "File Not Found: " + path);
    return;
  }
  File file = LittleFS.open(path, "r");
  server.streamFile(file, getContentType(path));
  file.close();
}

hw_timer_t* signalTimer = nullptr;

// Forward declarations
void updateSignalOutput();
void startSinglePulse();
void handleGetSignalConfig();
void connectWiFi();
void startAP();
void captureWaveform();
void handleRoot();
void handleData();
void handleGetConfig();
void handleSetConfig();
void handleChartJS();
void handleSetMode();
void handleSetSignalConfig();
void handleToggleSignal();
void handleSinglePulse();
void handleGetSignalStatus();
void handleGetSignalConfig();
void handleNotFound();
void setupServerRoutes();


const int SIGNAL_BUF_SIZE = 256;  // Adjust for your desired resolution
uint8_t signalBuf[SIGNAL_BUF_SIZE];
volatile uint16_t signalBufIndex = 0;

// ----------------------------
//      WIFI CONNECTION
// ----------------------------
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - startAttempt > 15000) {
      Serial.println("\nWiFi connection timeout reached.");
      wifiConnected = false;
      return;
    }
  }
  
  wifiConnected = true;
  Serial.print("\nWiFi Connected! IP: ");
  Serial.println(WiFi.localIP());
}

void startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  apStarted = true;
  Serial.println("Access Point started");
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
}

// ----------------------------
//     WAVEFORM CAPTURE
// ----------------------------
void captureWaveform() {
  for (int i = 0; i < samplingConfig.numSamples; i++) {
    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
      waveformData[ch][i] = analogRead(analogPins[ch]);
      if (samplingConfig.channelDelayUs > 0) {
        delayMicroseconds(samplingConfig.channelDelayUs);
      }
    }
    if (samplingConfig.sampleRateUs > 0 && i < samplingConfig.numSamples - 1) {
      delayMicroseconds(samplingConfig.sampleRateUs);
    }
  }
}

// ----------------------------
//   SIGNAL GENERATION LOGIC
// ----------------------------


void fillSignalBuffer() {
  // Copy current config for thread safety
  SignalConfig config = signalConfig;

  for (int i = 0; i < SIGNAL_BUF_SIZE; i++) {
    float t = (float)i / SIGNAL_BUF_SIZE; // 0...1
    int value = 0;

    switch(config.waveformType) {
      case 0: // DC
        value = config.dcOffset;
        break;
      case 1: // Square
        value = (t < 0.5) ? config.amplitude + config.dcOffset : config.dcOffset;
        break;
      case 2: // Sine
        value = config.dcOffset + int((config.amplitude / 2.0) * (sin(2 * PI * t) + 1.0));
        break;
      case 3: // Triangle
        if (t < 0.5)
          value = config.dcOffset + int(config.amplitude * (2 * t));
        else
          value = config.dcOffset + int(config.amplitude * (2 * (1 - t)));
        break;
      case 4: // PWM
        value = (t < config.dutyCycle / 100.0) ? config.amplitude + config.dcOffset : config.dcOffset;
        break;
      default:
        value = config.dcOffset;
    }
    signalBuf[i] = constrain(value, 0, 255);
  }
  signalBufIndex = 0; // Reset index each time we refill
}

void updateSignalOutput() {
  if (!signalConfig.isEnabled) {
    dacWrite(signalPin, 0);
    return;
  }
  uint8_t value = signalBuf[signalBufIndex];
  dacWrite(signalPin, value);
  signalBufIndex = (signalBufIndex + 1) % SIGNAL_BUF_SIZE;
}

// ----------------------------
//       JSON UTILITIES
// ----------------------------
int extractJsonInt(String json, String key) {
  String searchStr = "\"" + key + "\":";
  int startPos = json.indexOf(searchStr);
  if (startPos == -1) return -1;
  startPos += searchStr.length();
  int endPos = json.indexOf(',', startPos);
  if (endPos == -1) endPos = json.indexOf('}', startPos);
  return json.substring(startPos, endPos).toInt();
}
bool extractJsonBool(String json, String key) {
  String searchStr = "\"" + key + "\":";
  int startPos = json.indexOf(searchStr);
  if (startPos == -1) return false;
  startPos += searchStr.length();
  int endPos = json.indexOf(',', startPos);
  if (endPos == -1) endPos = json.indexOf('}', startPos);
  String val = json.substring(startPos, endPos);
  val.trim();
  return (val == "true" || val == "1");
}

// ----------------------------
//       HTTP HANDLERS
// ----------------------------
/* void handleRoot() {
Serial.println("Serving root page");
server.send_P(200, "text/html", htmlPage);
} */

void handleData() {
  captureWaveform();
  String json = "{\"channels\":[";
  for (int ch = 0; ch < NUM_CHANNELS; ch++) {
    json += "[";
    for (int i = 0; i < samplingConfig.numSamples; i++) {
      json += String(waveformData[ch][i]);
      if (i < samplingConfig.numSamples - 1) json += ",";
    }
    json += "]";
    if (ch < NUM_CHANNELS - 1) json += ",";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleGetConfig() {
  String json = "{";
  json += "\"numSamples\":" + String(samplingConfig.numSamples) + ",";
  json += "\"sampleRate\":" + String(samplingConfig.sampleRateUs) + ",";
  json += "\"channelDelay\":" + String(samplingConfig.channelDelayUs) + ",";
  json += "\"captureInterval\":" + String(samplingConfig.captureIntervalMs) + ",";
  json += "\"webUpdate\":" + String(samplingConfig.webUpdateMs);
  json += "}";
  server.send(200, "application/json", json);
}

void handleSetConfig() {
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    int numSamples = extractJsonInt(body, "numSamples");
    int sampleRate = extractJsonInt(body, "sampleRate");
    int channelDelay = extractJsonInt(body, "channelDelay");
    int captureInterval = extractJsonInt(body, "captureInterval");
    int webUpdate = extractJsonInt(body, "webUpdate");
    
    if (numSamples >= 10 && numSamples <= MAX_SAMPLES) {
      samplingConfig.numSamples = numSamples;
    }
    if (sampleRate >= 10 && sampleRate <= 10000) {
      samplingConfig.sampleRateUs = sampleRate;
    }
    if (channelDelay >= 1 && channelDelay <= 100) {
      samplingConfig.channelDelayUs = channelDelay;
    }
    if (captureInterval >= 10 && captureInterval <= 5000) {
      samplingConfig.captureIntervalMs = captureInterval;
    }
    if (webUpdate >= 100 && webUpdate <= 5000) {
      samplingConfig.webUpdateMs = webUpdate;
    }
    
    Serial.println("Configuration updated:");
    Serial.println("  Samples: " + String(samplingConfig.numSamples));
    Serial.println("  Sample Rate: " + String(samplingConfig.sampleRateUs) + "µs");
    Serial.println("  Channel Delay: " + String(samplingConfig.channelDelayUs) + "µs");
    Serial.println("  Capture Interval: " + String(samplingConfig.captureIntervalMs) + "ms");
    Serial.println("  Web Update: " + String(samplingConfig.webUpdateMs) + "ms");
    
    server.send(200, "text/plain", "Configuration updated");
  } else {
    server.send(400, "text/plain", "Invalid request body");
  }
}

/* void handleChartJS() {
server.send_P(200, "application/javascript", chartJS);
} */

void handleSetMode() {
  if (server.hasArg("mode")) {
    String mode = server.arg("mode");
    isSnapshotMode = (mode == "snapshot");
    Serial.println("Mode set to: " + mode);
    server.send(200, "text/plain", "Mode set to " + mode);
  } else {
    server.send(400, "text/plain", "Missing 'mode' argument");
  }
}

// --------- Signal Generator Endpoints ----------
void handleSetSignalConfig() {
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    int waveformType = extractJsonInt(body, "waveformType");
    int amplitude = extractJsonInt(body, "amplitude");
    int frequency = extractJsonInt(body, "frequency");
    int dutyCycle = extractJsonInt(body, "dutyCycle");
    int dcOffset = extractJsonInt(body, "dcOffset");
    int pulseWidthMs = extractJsonInt(body, "pulseWidthMs");
    
    // REST handler safety: Validate all values before updating signalConfig
    if (waveformType < 0 || waveformType > 4)
    return server.send(400, "text/plain", "Invalid waveformType");
    if (amplitude < 0 || amplitude > 255)
    return server.send(400, "text/plain", "Invalid amplitude");
    if (frequency < 1 || frequency > 50000)
    return server.send(400, "text/plain", "Invalid frequency");
    if (dutyCycle < 0 || dutyCycle > 100)
    return server.send(400, "text/plain", "Invalid dutyCycle");
    if (dcOffset < 0 || dcOffset > 255)
    return server.send(400, "text/plain", "Invalid dcOffset");
    if (pulseWidthMs < 1 || pulseWidthMs > 10000)
    return server.send(400, "text/plain", "Invalid pulseWidthMs");
    
    portENTER_CRITICAL(&signalMux);
    signalConfig.waveformType = waveformType;
    signalConfig.amplitude = amplitude;
    signalConfig.frequency = frequency;
    signalConfig.dutyCycle = dutyCycle;
    signalConfig.dcOffset = dcOffset;
    signalConfig.pulseWidthMs = pulseWidthMs;
    portEXIT_CRITICAL(&signalMux);
    
    Serial.println("Signal generator configuration updated:");
    Serial.println("  Waveform: " + String(signalConfig.waveformType));
    Serial.println("  Amplitude: " + String(signalConfig.amplitude));
    Serial.println("  Frequency: " + String(signalConfig.frequency) + " Hz");
    Serial.println("  Duty Cycle: " + String(signalConfig.dutyCycle) + " %");
    Serial.println("  DC Offset: " + String(signalConfig.dcOffset));
    Serial.println("  Pulse Width: " + String(signalConfig.pulseWidthMs) + " ms");
    
    server.send(200, "text/plain", "Signal configuration updated");
  } else {
    server.send(400, "text/plain", "Invalid request body");
  }

  fillSignalBuffer();
  int interval_us = 1000000 / (signalConfig.frequency * SIGNAL_BUF_SIZE);
  if (interval_us < 1) interval_us = 1;
  timerAlarmWrite(signalTimer, interval_us, true); 

}

void handleToggleSignal() {
  portENTER_CRITICAL(&signalMux);
  signalConfig.isEnabled = !signalConfig.isEnabled;
  signalConfig.singlePulse = false;
  portEXIT_CRITICAL(&signalMux);
  
  if (!signalConfig.isEnabled) {
    dacWrite(signalPin, 0);
    Serial.println("Signal generator disabled.");
  } else {
    Serial.println("Signal generator enabled.");
    Serial.println("  Waveform: " + String(signalConfig.waveformType));
    Serial.println("  Amplitude: " + String(signalConfig.amplitude));
    Serial.println("  Frequency: " + String(signalConfig.frequency) + " Hz");
    Serial.println("  Duty Cycle: " + String(signalConfig.dutyCycle) + " %");
    Serial.println("  DC Offset: " + String(signalConfig.dcOffset));
    Serial.println("  Pulse Width: " + String(signalConfig.pulseWidthMs) + " ms");
  }
  String json = "{\"enabled\":" + String(signalConfig.isEnabled ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void startSinglePulse() {
  portENTER_CRITICAL(&signalMux);
  signalConfig.singlePulse = true;
  signalConfig.isEnabled = true;
  portEXIT_CRITICAL(&signalMux);
  pulseStartTime = millis();
  pulseActive = true;
}

void handleSinglePulse() {
  startSinglePulse();
  Serial.println("Single pulse triggered:");
  Serial.println("  Amplitude: " + String(signalConfig.amplitude));
  Serial.println("  Pulse Width: " + String(signalConfig.pulseWidthMs) + " ms");
  server.send(200, "text/plain", "Single pulse started");
}

void handleGetSignalStatus() {
  String json = "{";
  json += "\"enabled\":" + String(signalConfig.isEnabled ? "true" : "false") + ",";
  json += "\"waveformType\":" + String(signalConfig.waveformType) + ",";
  json += "\"amplitude\":" + String(signalConfig.amplitude) + ",";
  json += "\"frequency\":" + String(signalConfig.frequency) + ",";
  json += "\"dutyCycle\":" + String(signalConfig.dutyCycle) + ",";
  json += "\"dcOffset\":" + String(signalConfig.dcOffset);
  json += "}";
  server.send(200, "application/json", json);
}

void handleGetSignalConfig() {
  String json = "{";
  json += "\"waveformType\":" + String(signalConfig.waveformType) + ",";
  json += "\"amplitude\":" + String(signalConfig.amplitude) + ",";
  json += "\"frequency\":" + String(signalConfig.frequency) + ",";
  json += "\"dutyCycle\":" + String(signalConfig.dutyCycle) + ",";
  json += "\"dcOffset\":" + String(signalConfig.dcOffset) + ",";
  json += "\"pulseWidthMs\":" + String(signalConfig.pulseWidthMs) + ",";
  json += "\"isEnabled\":" + String(signalConfig.isEnabled ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void handleNotFound() {
  Serial.println("404: " + server.uri());
  server.send(404, "text/plain", "404: Not Found");
}

// ----------------------------
//      SERVER ROUTES
// ----------------------------
void setupServerRoutes() {
  server.on("/", HTTP_GET, handleFileRequest);
  server.on("/index.html", HTTP_GET, handleFileRequest);
  server.on("/style.css", HTTP_GET, handleFileRequest);
  server.on("/chart.js", HTTP_GET, handleFileRequest);
  server.on("/app.js", HTTP_GET, handleFileRequest);
  
  server.on("/data", HTTP_GET, handleData);
  server.on("/getConfig", HTTP_GET, handleGetConfig);
  server.on("/setConfig", HTTP_POST, handleSetConfig);
  server.on("/setMode", HTTP_GET, handleSetMode);
  
  // Signal generator endpoints
  server.on("/setSignalConfig", HTTP_POST, handleSetSignalConfig);
  server.on("/getSignalConfig", HTTP_GET, handleGetSignalConfig);
  server.on("/toggleSignal", HTTP_POST, handleToggleSignal);
  server.on("/singlePulse", HTTP_POST, handleSinglePulse);
  server.on("/getSignalStatus", HTTP_GET, handleGetSignalStatus);
  
  server.onNotFound(handleFileRequest);  // fallback
}

// ----------------------------
//     SIGNAL TIMER ISR HOOK
// ----------------------------
void IRAM_ATTR onSignalTimer() {
  signalUpdateFlag = true; // Only set a flag!
}

// ----------------------------
//            SETUP
// ----------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 Oscilloscope & Signal Generator Starting...");
  
  pinMode(LED_PIN, OUTPUT);
  pinMode(signalPin, OUTPUT);
  
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed");
    return;
  }
  
  // Initialize waveform data
  for (int ch = 0; ch < NUM_CHANNELS; ch++) {
    for (int i = 0; i < MAX_SAMPLES; i++) {
      waveformData[ch][i] = 0;
    }
  }
  
  connectWiFi();
  if (!wifiConnected) {
    Serial.println("Starting Access Point mode...");
    startAP();
  }
  
  setupServerRoutes();
  server.begin();
  Serial.println("Web server started");
  
  captureWaveform();
  
  Serial.println("Setup complete!");
  Serial.println("Current sampling configuration:");
  Serial.println("  Samples: " + String(samplingConfig.numSamples));
  Serial.println("  Sample Rate: " + String(samplingConfig.sampleRateUs) + "µs");
  Serial.println("  Channel Delay: " + String(samplingConfig.channelDelayUs) + "µs");
  Serial.println("  Capture Interval: " + String(samplingConfig.captureIntervalMs) + "ms");
  
  if (wifiConnected) {
    Serial.println("Connect to: http://" + WiFi.localIP().toString());
  } else {
    Serial.println("Connect to: http://" + WiFi.softAPIP().toString());
  }
  
  // Setup signal generation timer
signalTimer = timerBegin(0, 80, true); // timer 0, prescaler 80 for 1us ticks
timerAttachInterrupt(signalTimer, &onSignalTimer, true);

// Compute timer interval from frequency and buffer size
int interval_us = 1000000 / (signalConfig.frequency * SIGNAL_BUF_SIZE);
if (interval_us < 1) interval_us = 1; // Prevent zero-interval
timerAlarmWrite(signalTimer, interval_us, true);
timerAlarmEnable(signalTimer);
}

// ----------------------------
//             LOOP
// ----------------------------
void loop() {
  server.handleClient();
  
  // Scope mode logic
  if (isSnapshotMode) {
    pinMode(triggerPin, INPUT_PULLUP);  // allow button use if not generating
    if (digitalRead(triggerPin) == LOW && !triggerPressed) {
      triggerPressed = true;
      captureWaveform();
      Serial.println("Trigger activated - waveform captured");
    }
    if (digitalRead(triggerPin) == HIGH) {
      triggerPressed = false;
    }
  } else {
    if (millis() - lastCapture >= samplingConfig.captureIntervalMs) {
      lastCapture = millis();
      captureWaveform();
    }
  }
  
  // Network Indicator LED
  if (apStarted) {
    digitalWrite(LED_PIN, HIGH);
  } else if (wifiConnected) {
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= blinkInterval) {
      previousMillis = currentMillis;
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
  } else {
    digitalWrite(LED_PIN, LOW);
  }
  
  // Debug print once per second
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 1000) {
    lastDebug = millis();
    Serial.println("--- DEBUG ---");
    Serial.print("signalConfig: ");
    Serial.print("isEnabled=");
    Serial.print(signalConfig.isEnabled);
    Serial.print(", singlePulse=");
    Serial.print(signalConfig.singlePulse);
    Serial.print(", amplitude=");
    Serial.print(signalConfig.amplitude);
    Serial.print(", frequency=");
    Serial.print(signalConfig.frequency);
    Serial.print(", waveformType=");
    Serial.println(signalConfig.waveformType);
    Serial.print("pulseActive=");
    Serial.print(pulseActive);
    Serial.print(", pulseStartTime=");
    Serial.println(pulseStartTime);
    Serial.println("--------------");
  }
  
  if (signalUpdateFlag) {
    signalUpdateFlag = false;
    updateSignalOutput(); // Do heavy work OUTSIDE ISR
  }
}