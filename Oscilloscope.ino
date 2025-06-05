// ============================
//     Oscilloscope v2.0
//     Fixed for Local Chart.js
// ============================

#include <WiFi.h>
#include <WebServer.h>
#include "chartjs_umd.h"

// ----------------------------
//        CONFIGURATION
// ----------------------------
const char* ssid = "CasimirServer";
const char* password = "CasimirServer";
const char* ap_ssid = "ESP32-Oscilloscope";
const char* ap_password = "12345678";

#define NUM_CHANNELS 6
#define SAMPLES 100
#define LED_PIN 2
const int analogPins[NUM_CHANNELS] = {34, 35, 32, 33, 36, 39};
const int triggerPin = 27;

unsigned long captureInterval = 50;  // ms

// ----------------------------
//         GLOBALS
// ----------------------------
int waveformData[NUM_CHANNELS][SAMPLES];
bool wifiConnected = false;
bool apStarted = false;
bool isSnapshotMode = false;
bool triggerPressed = false;
unsigned long previousMillis = 0;
const long blinkInterval = 500;

WebServer server(80);

// ----------------------------
//      HTML Page String 
// ----------------------------
const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 Oscilloscope</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { 
      font-family: Arial; 
      background-color: #1e1e1e; 
      color: white; 
      margin: 0; 
      padding: 20px; 
      text-align: center; 
    }
    canvas { 
      background: #2e2e2e; 
      border: 1px solid #444; 
      margin: 20px auto; 
      display: block;
      width: 95% !important;
      max-width: 1200px;
      height: 60vh !important;
      min-height: 400px;
      max-height: 600px;
    }
    button { 
      margin: 10px; 
      padding: 10px 20px; 
      font-size: 16px; 
      background-color: #4CAF50;
      color: white;
      border: none;
      border-radius: 4px;
      cursor: pointer;
    }
    button:hover {
      background-color: #45a049;
    }
    .legend { 
      margin-top: 10px; 
      font-size: 14px;
    }
    .status {
      margin: 10px;
      padding: 10px;
      background-color: #333;
      border-radius: 4px;
    }
    .controls {
      margin: 20px 0;
      display: flex;
      justify-content: center;
      gap: 10px;
      flex-wrap: wrap;
    }
  </style>
</head>
<body>
  <h2>ESP32 Oscilloscope</h2>
  <div class="status">
    <span>Mode: <span id="modeStatus">Continuous</span></span>
    <span style="margin-left: 20px;">Status: <span id="connectionStatus">Loading...</span></span>
  </div>
  
  <div class="controls">
    <button onclick="toggleMode()">Toggle Mode</button>
    <button onclick="captureWaveform()">Manual Capture</button>
    <button onclick="clearChart()">Clear</button>
  </div>
  
  <canvas id="oscilloscope"></canvas>
  
  <div class="legend">
    <p><strong>Channels:</strong></p>
    <div style="display: flex; flex-wrap: wrap; justify-content: center; gap: 20px; margin-top: 10px;">
      <span style="color: #ff6384;">CH1: GPIO34</span>
      <span style="color: #36a2eb;">CH2: GPIO35</span>
      <span style="color: #ffce56;">CH3: GPIO32</span>
      <span style="color: #4bc0c0;">CH4: GPIO33</span>
      <span style="color: #9966ff;">CH5: GPIO36</span>
      <span style="color: #ff9f40;">CH6: GPIO39</span>
    </div>
  </div>
  
  <!-- Load Chart.js from local server BEFORE using it -->
  <script src="/chart.js"></script>
  <script>
    let ctx = document.getElementById('oscilloscope').getContext('2d');
    let chart;
    let continuousMode = true;
    let isCapturing = false;

    // Chart colors for each channel
    const channelColors = [
      '#ff6384', // CH1 - Red
      '#36a2eb', // CH2 - Blue  
      '#ffce56', // CH3 - Yellow
      '#4bc0c0', // CH4 - Teal
      '#9966ff', // CH5 - Purple
      '#ff9f40'  // CH6 - Orange
    ];

    // Initialize chart after Chart.js loads
    function initChart() {
      console.log('Initializing chart...');
      
      if (typeof Chart === 'undefined') {
        console.error('Chart.js not loaded!');
        document.getElementById('connectionStatus').textContent = 'Chart.js Load Error';
        return;
      }

      chart = new Chart(ctx, {
        type: 'line',
        data: {
          labels: Array.from({ length: 100 }, (_, i) => i),
          datasets: channelColors.map((color, index) => ({
            label: `CH${index + 1}`,
            data: Array(100).fill(0),
            borderColor: color,
            backgroundColor: color + '20',
            fill: false,
            tension: 0.1,
            pointRadius: 0,
            borderWidth: 2
          }))
        },
        options: {
          animation: false,
          responsive: true,
          maintainAspectRatio: false,
          interaction: {
            intersect: false,
            mode: 'index'
          },
          scales: {
            x: {
              title: {
                display: true,
                text: 'Sample Number',
                color: 'white'
              },
              grid: {
                color: '#444'
              },
              ticks: {
                color: 'white'
              }
            },
            y: {
              beginAtZero: true,
              max: 4095,
              title: {
                display: true,
                text: 'ADC Value (0-4095)',
                color: 'white'
              },
              grid: {
                color: '#444'
              },
              ticks: {
                color: 'white'
              }
            }
          },
          plugins: {
            legend: {
              labels: {
                color: 'white',
                usePointStyle: true
              }
            },
            tooltip: {
              backgroundColor: 'rgba(0,0,0,0.8)',
              titleColor: 'white',
              bodyColor: 'white'
            }
          }
        }
      });
      
      console.log('Chart initialized successfully');
      document.getElementById('connectionStatus').textContent = 'Connected';
    }

    async function fetchData() {
      if (isCapturing) return;
      
      try {
        isCapturing = true;
        
        const response = await fetch('/data');
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
        
        const json = await response.json();
        console.log('Data received:', json);
        updateChart(json);
        
        // Only update status if it's not already connected
        if (document.getElementById('connectionStatus').textContent !== 'Connected') {
          document.getElementById('connectionStatus').textContent = 'Connected';
        }
        
      } catch (error) {
        console.error('Error fetching data:', error);
        document.getElementById('connectionStatus').textContent = 'Disconnected';
      } finally {
        isCapturing = false;
      }
    }

    function toggleMode() {
      continuousMode = !continuousMode;
      document.getElementById('modeStatus').textContent = continuousMode ? 'Continuous' : 'Snapshot';
      
      // Send mode to ESP32
      const mode = continuousMode ? 'continuous' : 'snapshot';
      fetch(`/setMode?mode=${mode}`)
        .then(response => {
          if (response.ok) {
            console.log(`Mode set to: ${mode}`);
          }
        })
        .catch(error => console.error('Error setting mode:', error));
    }

    function captureWaveform() {
      console.log('Manual capture triggered');
      fetchData();
    }

    function clearChart() {
      if (chart) {
        chart.data.datasets.forEach(dataset => {
          dataset.data = Array(100).fill(0);
        });
        chart.update('none');
      }
    }

    function updateChart(data) {
      if (!chart) {
        console.error('Chart not initialized');
        return;
      }
      
      if (!data.channels || !Array.isArray(data.channels)) {
        console.error('Invalid data format received:', data);
        return;
      }

      // Update chart data
      chart.data.datasets.forEach((dataset, i) => {
        if (i < data.channels.length && Array.isArray(data.channels[i])) {
          dataset.data = data.channels[i];
        }
      });
      
      chart.update('none'); // Update without animation for better performance
    }

    // Initialize everything when page loads
    window.addEventListener('load', function() {
      console.log('Page loaded, initializing...');
      
      // Small delay to ensure Chart.js is fully loaded
      setTimeout(() => {
        initChart();
        
        // Start continuous updates after chart is ready
        if (chart) {
          setInterval(() => {
            if (continuousMode && !isCapturing) {
              fetchData();
            }
          }, 500);
          
          // Initial data fetch
          setTimeout(() => fetchData(), 1000);
        }
      }, 100);
    });

    // Handle errors
    window.addEventListener('error', function(e) {
      console.error('JavaScript error:', e.error);
      document.getElementById('connectionStatus').textContent = 'Script Error';
    });
  </script>
</body>
</html>
)rawliteral";

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
  for (int i = 0; i < SAMPLES; i++) {
    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
      waveformData[ch][i] = analogRead(analogPins[ch]);
      delayMicroseconds(10);
    }
    delayMicroseconds(50);
  }
}

// ----------------------------
//       HTTP HANDLERS
// ----------------------------
void handleRoot() {
  Serial.println("Serving root page");
  server.send_P(200, "text/html", htmlPage);
}

void handleData() {
  // Always capture fresh data when requested
  captureWaveform();
  Serial.println("Fresh data captured for request");
  
  // Build JSON response
  String json = "{\"channels\":[";
  for (int ch = 0; ch < NUM_CHANNELS; ch++) {
    json += "[";
    for (int i = 0; i < SAMPLES; i++) {
      json += String(waveformData[ch][i]);
      if (i < SAMPLES - 1) json += ",";
    }
    json += "]";
    if (ch < NUM_CHANNELS - 1) json += ",";
  }
  json += "]}";
  
  server.send(200, "application/json", json);
}

void handleChartJS() {
  // Serve the Chart.js library from the header file
  // The variable name depends on how you converted it - common names are:
  // chartjs_umd, chart_js, chartJS, or similar
  server.send_P(200, "application/javascript", chartJS);
}

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

void handleNotFound() {
  Serial.println("404: " + server.uri());
  server.send(404, "text/plain", "404: Not Found");
}

// ----------------------------
//      SERVER ROUTES
// ----------------------------
void setupServerRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.on("/chart.js", HTTP_GET, handleChartJS);
  server.on("/setMode", HTTP_GET, handleSetMode);
  server.onNotFound(handleNotFound);
}

// ----------------------------
//            SETUP
// ----------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 Oscilloscope Starting...");
  
  pinMode(triggerPin, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  
  // Initialize waveform data
  for (int ch = 0; ch < NUM_CHANNELS; ch++) {
    for (int i = 0; i < SAMPLES; i++) {
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
  if (wifiConnected) {
    Serial.println("Connect to: http://" + WiFi.localIP().toString());
  } else {
    Serial.println("Connect to: http://" + WiFi.softAPIP().toString());
  }
}

// ----------------------------
//             LOOP
// ----------------------------
void loop() {
  server.handleClient();

  if (isSnapshotMode) {
    if (digitalRead(triggerPin) == LOW && !triggerPressed) {
      triggerPressed = true;
      captureWaveform();
      Serial.println("Trigger activated - waveform captured");
    }
    if (digitalRead(triggerPin) == HIGH) {
      triggerPressed = false;
    }
  } else {
    static unsigned long lastCapture = 0;
    if (millis() - lastCapture >= captureInterval) {
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
}