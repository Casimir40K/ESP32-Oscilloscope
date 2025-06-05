#include <WiFi.h>SAMPLES
#include <WebServer.h>
#include "chartjs_umd.h"

const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

const char* ap_ssid = "ESP32-Oscilloscope";
const char* ap_password = "12345678";

WebServer server(80);

#define NUM_CHANNELS 6
#define SAMPLES 100

const int analogPins[NUM_CHANNELS] = {34, 35, 32, 33, 36, 39};
int waveformData[NUM_CHANNELS][SAMPLES];

const int triggerPin = 27;
bool lastTriggerState = HIGH;
bool isSnapshotMode = false;  // Start in trigger mode by default
bool triggerPressed = false;

bool continuousMode = true;
unsigned long captureInterval = 50;  // Capture every 50 ms in continuous mode

bool wifiConnected = false;
bool apStarted = false;  // NEW: flag to avoid restarting AP repeatedly

unsigned long wifiConnectStart = 0;
const unsigned long wifiTimeout = 15000; // 15 seconds timeout

void captureWaveform();
void handleRoot();
void handleData();
void handleNotFound();
void startAP();
void connectWiFi();

void setup() {
  Serial.begin(115200);

  pinMode(triggerPin, INPUT_PULLUP);

  connectWiFi();


  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.on("/chart.js", HTTP_GET, [](){
  server.send_P(200, "application/javascript", chartJS);
  });
  server.on("/setMode", HTTP_GET, []() {
  if (server.hasArg("mode")) {
    String mode = server.arg("mode");
    isSnapshotMode = (mode == "snapshot");
    Serial.print("Mode changed to: ");
    Serial.println(isSnapshotMode ? "Snapshot" : "Continuous");
    server.send(200, "text/plain", "Mode set to " + mode);
  } else {
    server.send(400, "text/plain", "Missing 'mode' argument");
  }
});
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();


     // Handle Snapshot Mode
  if (isSnapshotMode) {
    if (digitalRead(triggerPin) == LOW && !triggerPressed) {
      triggerPressed = true;
      captureWaveform();
    }
    if (digitalRead(triggerPin) == HIGH) {
      triggerPressed = false;
    }
  }

  // Handle Continuous Mode
  else {
    static unsigned long lastCaptureTime = 0;
    unsigned long currentTime = millis();
    if (currentTime - lastCaptureTime > captureInterval) {
      lastCaptureTime = currentTime;
      captureWaveform();  // automatically capture at regular intervals
    }
  }

  

  if (!wifiConnected && !apStarted && (millis() - wifiConnectStart > wifiTimeout)) {
    Serial.println("\nWiFi connection timed out. Starting Access Point...");
    startAP();
    apStarted = true;  // Set flag so AP only starts once
  }
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  wifiConnectStart = millis();

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - wifiConnectStart > wifiTimeout) {
      Serial.println("\nTimeout reached.");
      wifiConnected = false;
      return;
    }
  }

  wifiConnected = true;
  Serial.println("\nConnected to WiFi!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void startAP() {
  wifiConnected = false;
  WiFi.softAP(ap_ssid, ap_password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP started with IP: ");
  Serial.println(IP);
}

void captureWaveform() {
  for (int sample = 0; sample < SAMPLES; sample++) {
    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
      waveformData[ch][sample] = analogRead(analogPins[ch]);
    }
    delay(1);
  }
  Serial.println("Waveform captured");
}


const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 Oscilloscope</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; background-color: #121212; color: #eee; }
    #legend { margin-top: 10px; font-size: 14px; }
    label { font-size: 16px; }
    button { margin-left: 10px; padding: 5px 10px; cursor: pointer; }
    #status { margin-top: 10px; }
  </style>
</head>
<body>
  <h1>ESP32 Oscilloscope</h1>

  <label><input type="checkbox" id="modeToggle"> Continuous Mode</label>
  <button id="captureBtn">Capture</button>

  <canvas id="waveformChart" width="800" height="400" style="background:#222;"></canvas>

  <div id="legend">
    <strong>Channels:</strong><br>
    CH1: GPIO 34<br>
    CH2: GPIO 35<br>
    CH3: GPIO 32<br>
    CH4: GPIO 33<br>
    CH5: GPIO 36<br>
    CH6: GPIO 39
  </div>

  <div id="status"></div>

  <script src="/chart.js"></script>
  <script>
    const ctx = document.getElementById('waveformChart').getContext('2d');
    const chart = new Chart(ctx, {
      type: 'line',
      data: {
        labels: [],
        datasets: [
          { label: 'CH1', borderColor: 'rgba(255, 99, 132, 1)', backgroundColor:'rgba(255, 99, 132, 0.2)', data: [], fill: false, tension: 0.1 },
          { label: 'CH2', borderColor: 'rgba(54, 162, 235, 1)', backgroundColor:'rgba(54, 162, 235, 0.2)', data: [], fill: false, tension: 0.1 },
          { label: 'CH3', borderColor: 'rgba(255, 206, 86, 1)', backgroundColor:'rgba(255, 206, 86, 0.2)', data: [], fill: false, tension: 0.1 },
          { label: 'CH4', borderColor: 'rgba(75, 192, 192, 1)', backgroundColor:'rgba(75, 192, 192, 0.2)', data: [], fill: false, tension: 0.1 },
          { label: 'CH5', borderColor: 'rgba(153, 102, 255, 1)', backgroundColor:'rgba(153, 102, 255, 0.2)', data: [], fill: false, tension: 0.1 },
          { label: 'CH6', borderColor: 'rgba(255, 159, 64, 1)', backgroundColor:'rgba(255, 159, 64, 0.2)', data: [], fill: false, tension: 0.1 }
        ]
      },
      options: {
        responsive: false,
        animation: false,
        scales: {
          x: {
            title: { display: true, text: 'Sample' },
            grid: { color: '#444' },
            ticks: { color: '#ddd' }
          },
          y: {
            title: { display: true, text: 'ADC Value' },
            min: 0,
            max: 4095,
            grid: { color: '#444' },
            ticks: { color: '#ddd' }
          }
        },
        plugins: {
          legend: {
            labels: {
              color: '#eee'
            }
          }
        }
      }
    });

    let continuousMode = false;
    const statusEl = document.getElementById('status');
    const modeToggle = document.getElementById('modeToggle');
    const captureBtn = document.getElementById('captureBtn');

    modeToggle.addEventListener('change', () => {
      continuousMode = modeToggle.checked;
      if (continuousMode) {
        statusEl.textContent = "Continuous mode ON";
        startPolling();
      } else {
        statusEl.textContent = "Continuous mode OFF";
      }
    });



    captureBtn.addEventListener('click', () => {
      fetchData();
    });

    // Initial load: get waveform once
    fetchData();

    function fetchData() {
      fetch('/data')
        .then(response => response.json())
        .then(data => {
          chart.data.labels = Array.from({length: data[0].length}, (_, i) => i);
          chart.data.datasets.forEach((ds, i) => {
            ds.data = data[i];
          });
          chart.update();
        })
        .catch(() => statusEl.textContent = "Error fetching data");
    }

    function startPolling() {
      if (!continuousMode) return;
      fetchData();
      setTimeout(startPolling, 200);
    }
  </script>
</body>
</html>
  )rawliteral";

void handleRoot() {
  server.send_P(200, "text/html", htmlPage);
  
}

void handleData() {
  String json = "[";
  for (int ch = 0; ch < NUM_CHANNELS; ch++) {
    json += "[";
    for (int sample = 0; sample < SAMPLES; sample++) {
      json += waveformData[ch][sample];
      if (sample < SAMPLES - 1) json += ",";
    }
    json += "]";
    if (ch < NUM_CHANNELS - 1) json += ",";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not Found");
}
