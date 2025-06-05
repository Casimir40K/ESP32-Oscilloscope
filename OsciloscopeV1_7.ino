// ============================
//     Oscilloscope v2.1
//     Enhanced with Adjustable Timing
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
  #define MAX_SAMPLES 500  // Maximum buffer size
  #define LED_PIN 2
  const int analogPins[NUM_CHANNELS] = {34, 35, 32, 33, 36, 39};
  const int triggerPin = 27;

// ----------------------------
//    ADJUSTABLE PARAMETERS
// ----------------------------
  struct SamplingConfig {
    int numSamples = 100;           // Number of samples per capture (10-500)
    int sampleRateUs = 100;         // Microseconds between samples (10-10000)
    int channelDelayUs = 5;         // Microseconds between channel reads (1-100)
    int captureIntervalMs = 50;     // Milliseconds between captures in continuous mode (10-5000)
    int webUpdateMs = 500;          // Milliseconds between web updates (100-5000)
  } samplingConfig;

  // Presets for common use cases
  struct Preset {
    const char* name;
    int samples;
    int sampleRate;
    int channelDelay;
    int captureInterval;
    int webUpdate;
  };

  const Preset presets[] = {
    {"High Speed", 100, 10, 1, 20, 200},      // Fast sampling, low resolution
    {"Balanced", 200, 100, 5, 50, 500},       // Default balanced settings
    {"High Resolution", 500, 1000, 10, 200, 1000}, // Slower but more detailed
    {"Low Power", 50, 5000, 20, 1000, 2000}   // Power saving mode
  };
  const int numPresets = sizeof(presets) / sizeof(presets[0]);

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
        height: 50vh !important;
        min-height: 300px;
        max-height: 500px;
      }
      button { 
        margin: 5px; 
        padding: 8px 16px; 
        font-size: 14px; 
        background-color: #4CAF50;
        color: white;
        border: none;
        border-radius: 4px;
        cursor: pointer;
      }
      button:hover { background-color: #45a049; }
      button.preset { background-color: #2196F3; }
      button.preset:hover { background-color: #1976D2; }
      
      .controls {
        margin: 15px 0;
        display: flex;
        justify-content: center;
        gap: 8px;
        flex-wrap: wrap;
      }
      .status {
        margin: 10px;
        padding: 10px;
        background-color: #333;
        border-radius: 4px;
        display: flex;
        justify-content: center;
        gap: 20px;
        flex-wrap: wrap;
      }
      .settings {
        background-color: #2a2a2a;
        border: 1px solid #444;
        border-radius: 8px;
        padding: 15px;
        margin: 15px auto;
        max-width: 800px;
        text-align: left;
      }
      .setting-row {
        display: flex;
        align-items: center;
        margin: 8px 0;
        gap: 10px;
        flex-wrap: wrap;
      }
      .setting-row label {
        min-width: 120px;
        font-weight: bold;
      }
      .setting-row input {
        background-color: #444;
        border: 1px solid #666;
        color: white;
        padding: 4px 8px;
        border-radius: 4px;
        width: 80px;
      }
      .legend { 
        margin-top: 10px; 
        font-size: 13px;
      }
      .presets {
        margin: 10px 0;
      }
    </style>
  </head>
  <body>
    <h2>ESP32 Oscilloscope</h2>
    
    <div class="status">
      <span>Mode: <span id="modeStatus">Continuous</span></span>
      <span>Status: <span id="connectionStatus">Loading...</span></span>
      <span>Samples: <span id="currentSamples">100</span></span>
      <span>Rate: <span id="currentRate">100µs</span></span>
    </div>
    
    <div class="controls">
      <button onclick="toggleMode()">Toggle Mode</button>
      <button onclick="captureWaveform()">Manual Capture</button>
      <button onclick="clearChart()">Clear</button>
      <button onclick="toggleSettings()">Settings</button>
    </div>
    
    <div id="settingsPanel" class="settings" style="display: none;">
      <h3>Sampling Configuration</h3>
      
      <div class="presets">
        <strong>Quick Presets:</strong><br>
        <button class="preset" onclick="applyPreset(0)">High Speed</button>
        <button class="preset" onclick="applyPreset(1)">Balanced</button>
        <button class="preset" onclick="applyPreset(2)">High Resolution</button>
        <button class="preset" onclick="applyPreset(3)">Low Power</button>
      </div>
      
      <div class="setting-row">
        <label>Samples:</label>
        <input type="number" id="numSamples" min="10" max="500" value="100">
        <span>(10-500)</span>
      </div>
      
      <div class="setting-row">
        <label>Sample Rate:</label>
        <input type="number" id="sampleRate" min="10" max="10000" value="100">
        <span>µs (10-10000)</span>
      </div>
      
      <div class="setting-row">
        <label>Channel Delay:</label>
        <input type="number" id="channelDelay" min="1" max="100" value="5">
        <span>µs (1-100)</span>
      </div>
      
      <div class="setting-row">
        <label>Capture Interval:</label>
        <input type="number" id="captureInterval" min="10" max="5000" value="50">
        <span>ms (10-5000)</span>
      </div>
      
      <div class="setting-row">
        <label>Web Update:</label>
        <input type="number" id="webUpdate" min="100" max="5000" value="500">
        <span>ms (100-5000)</span>
      </div>
      
      <div class="setting-row">
        <button onclick="applySettings()">Apply Settings</button>
        <button onclick="resetToDefaults()">Reset Defaults</button>
      </div>
    </div>
    
    <canvas id="oscilloscope"></canvas>
    
    <div class="legend">
      <p><strong>Channels:</strong></p>
      <div style="display: flex; flex-wrap: wrap; justify-content: center; gap: 15px; margin-top: 8px;">
        <span style="color: #ff6384;">CH1: GPIO34</span>
        <span style="color: #36a2eb;">CH2: GPIO35</span>
        <span style="color: #ffce56;">CH3: GPIO32</span>
        <span style="color: #4bc0c0;">CH4: GPIO33</span>
        <span style="color: #9966ff;">CH5: GPIO36</span>
        <span style="color: #ff9f40;">CH6: GPIO39</span>
      </div>
    </div>
    
    <script src="/chart.js"></script>
    <script>
      let ctx = document.getElementById('oscilloscope').getContext('2d');
      let chart;
      let continuousMode = true;
      let isCapturing = false;
      let updateInterval;
      let currentSettings = {
        numSamples: 100,
        sampleRate: 100,
        channelDelay: 5,
        captureInterval: 50,
        webUpdate: 500
      };

      const channelColors = [
        '#ff6384', '#36a2eb', '#ffce56', '#4bc0c0', '#9966ff', '#ff9f40'
      ];

      const presets = [
        {name: "High Speed", samples: 100, sampleRate: 10, channelDelay: 1, captureInterval: 20, webUpdate: 200},
        {name: "Balanced", samples: 200, sampleRate: 100, channelDelay: 5, captureInterval: 50, webUpdate: 500},
        {name: "High Resolution", samples: 500, sampleRate: 1000, channelDelay: 10, captureInterval: 200, webUpdate: 1000},
        {name: "Low Power", samples: 50, sampleRate: 5000, channelDelay: 20, captureInterval: 1000, webUpdate: 2000}
      ];

      function initChart() {
        if (typeof Chart === 'undefined') {
          console.error('Chart.js not loaded!');
          document.getElementById('connectionStatus').textContent = 'Chart.js Load Error';
          return;
        }

        chart = new Chart(ctx, {
          type: 'line',
          data: {
            labels: Array.from({ length: currentSettings.numSamples }, (_, i) => i),
            datasets: channelColors.map((color, index) => ({
              label: `CH${index + 1}`,
              data: Array(currentSettings.numSamples).fill(0),
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
            interaction: { intersect: false, mode: 'index' },
            scales: {
              x: {
                title: { display: true, text: 'Sample Number', color: 'white' },
                grid: { color: '#444' },
                ticks: { color: 'white' }
              },
              y: {
                beginAtZero: true,
                max: 4095,
                title: { display: true, text: 'ADC Value (0-4095)', color: 'white' },
                grid: { color: '#444' },
                ticks: { color: 'white' }
              }
            },
            plugins: {
              legend: { labels: { color: 'white', usePointStyle: true } },
              tooltip: { backgroundColor: 'rgba(0,0,0,0.8)', titleColor: 'white', bodyColor: 'white' }
            }
          }
        });
        
        document.getElementById('connectionStatus').textContent = 'Connected';
        startUpdates();
      }

      function startUpdates() {
        if (updateInterval) clearInterval(updateInterval);
        updateInterval = setInterval(() => {
          if (continuousMode && !isCapturing) {
            fetchData();
          }
        }, currentSettings.webUpdate);
      }

      async function fetchData() {
        if (isCapturing) return;
        
        try {
          isCapturing = true;
          const response = await fetch('/data');
          if (!response.ok) throw new Error(`HTTP ${response.status}`);
          
          const json = await response.json();
          updateChart(json);
          if (document.getElementById('connectionStatus').textContent !== 'Connected') {
            document.getElementById('connectionStatus').textContent = 'Connected';
          }
        } catch (error) {
          console.error('Error:', error);
          document.getElementById('connectionStatus').textContent = 'Disconnected';
        } finally {
          isCapturing = false;
        }
      }

      function toggleMode() {
        continuousMode = !continuousMode;
        document.getElementById('modeStatus').textContent = continuousMode ? 'Continuous' : 'Snapshot';
        
        const mode = continuousMode ? 'continuous' : 'snapshot';
        fetch(`/setMode?mode=${mode}`)
          .then(response => response.ok && console.log(`Mode: ${mode}`))
          .catch(error => console.error('Error setting mode:', error));
      }

      function captureWaveform() {
        fetchData();
      }

      function clearChart() {
        if (chart) {
          chart.data.datasets.forEach(dataset => {
            dataset.data = Array(currentSettings.numSamples).fill(0);
          });
          chart.update('none');
        }
      }

      function toggleSettings() {
        const panel = document.getElementById('settingsPanel');
        panel.style.display = panel.style.display === 'none' ? 'block' : 'none';
      }

      function applyPreset(index) {
        const preset = presets[index];
        document.getElementById('numSamples').value = preset.samples;
        document.getElementById('sampleRate').value = preset.sampleRate;
        document.getElementById('channelDelay').value = preset.channelDelay;
        document.getElementById('captureInterval').value = preset.captureInterval;
        document.getElementById('webUpdate').value = preset.webUpdate;
        applySettings();
      }

      function resetToDefaults() {
        applyPreset(1); // Balanced preset
      }

      async function applySettings() {
        const settings = {
          numSamples: parseInt(document.getElementById('numSamples').value),
          sampleRate: parseInt(document.getElementById('sampleRate').value),
          channelDelay: parseInt(document.getElementById('channelDelay').value),
          captureInterval: parseInt(document.getElementById('captureInterval').value),
          webUpdate: parseInt(document.getElementById('webUpdate').value)
        };

        try {
          const response = await fetch('/setConfig', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(settings)
          });

          if (response.ok) {
            currentSettings = settings;
            updateStatusDisplay();
            
            // Update chart labels and data arrays
            chart.data.labels = Array.from({ length: settings.numSamples }, (_, i) => i);
            chart.data.datasets.forEach(dataset => {
              dataset.data = Array(settings.numSamples).fill(0);
            });
            chart.update();
            
            startUpdates(); // Restart with new timing
            console.log('Settings applied successfully');
          } else {
            console.error('Failed to apply settings');
          }
        } catch (error) {
          console.error('Error applying settings:', error);
        }
      }

      function updateStatusDisplay() {
        document.getElementById('currentSamples').textContent = currentSettings.numSamples;
        document.getElementById('currentRate').textContent = currentSettings.sampleRate + 'µs';
      }

      async function fetchCurrentConfig() {
        try {
          const response = await fetch('/getConfig');
          if (response.ok) {
            const config = await response.json();
            currentSettings = config;
            
            // Update UI inputs
            document.getElementById('numSamples').value = config.numSamples;
            document.getElementById('sampleRate').value = config.sampleRate;
            document.getElementById('channelDelay').value = config.channelDelay;
            document.getElementById('captureInterval').value = config.captureInterval;
            document.getElementById('webUpdate').value = config.webUpdate;
            
            updateStatusDisplay();
          }
        } catch (error) {
          console.error('Error fetching config:', error);
        }
      }

      function updateChart(data) {
        if (!chart || !data.channels || !Array.isArray(data.channels)) return;

        chart.data.datasets.forEach((dataset, i) => {
          if (i < data.channels.length && Array.isArray(data.channels[i])) {
            dataset.data = data.channels[i];
          }
        });
        
        chart.update('none');
      }

      window.addEventListener('load', function() {
        setTimeout(() => {
          fetchCurrentConfig().then(() => {
            initChart();
            setTimeout(() => fetchData(), 1000);
          });
        }, 100);
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
//       HTTP HANDLERS
// ----------------------------
  void handleRoot() {
    Serial.println("Serving root page");
    server.send_P(200, "text/html", htmlPage);
  }

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

  // Simple JSON integer extraction
  int extractJsonInt(String json, String key) {
    String searchStr = "\"" + key + "\":";
    int startPos = json.indexOf(searchStr);
    if (startPos == -1) return -1;
    
    startPos += searchStr.length();
    int endPos = json.indexOf(',', startPos);
    if (endPos == -1) endPos = json.indexOf('}', startPos);
    
    return json.substring(startPos, endPos).toInt();
  }

  void handleSetConfig() {
    if (server.hasArg("plain")) {
      String body = server.arg("plain");
      
      // Parse JSON manually (simple approach)
      int numSamples = extractJsonInt(body, "numSamples");
      int sampleRate = extractJsonInt(body, "sampleRate");
      int channelDelay = extractJsonInt(body, "channelDelay");
      int captureInterval = extractJsonInt(body, "captureInterval");
      int webUpdate = extractJsonInt(body, "webUpdate");
      
      // Validate and apply settings
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

  void handleChartJS() {
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
    server.on("/getConfig", HTTP_GET, handleGetConfig);
    server.on("/setConfig", HTTP_POST, handleSetConfig);
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
  }