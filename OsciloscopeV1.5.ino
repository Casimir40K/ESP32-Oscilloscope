// ============================
//     Oscilloscope v2.0
//     Refactored Sketch
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

  volatile unsigned int captureInterval = 2;  // Default 2 ms between samples

// ----------------------------
//         GLOBALS
// ----------------------------
  int waveformData[NUM_CHANNELS][SAMPLES];
  bool wifiConnected = false;
  bool apStarted = false;
  bool isSnapshotMode = false;
  bool triggerPressed = false;

  bool channelEnabled[6] = { true, true, true, true, true, true };  // Default: all channels enabled


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
      body { font-family: Arial; background-color: #1e1e1e; color: white; margin: 0; padding: 0; text-align: center; }
      canvas { background: #2e2e2e; border: 1px solid #444; margin-top: 20px; }
      button { margin: 10px; padding: 10px 20px; font-size: 16px; }
      .legend { margin-top: 10px; }
      #channel-controls {
        margin-top: 10px;
        padding: 10px;
        background: #eee;
        border-radius: 8px;
      }
      #channel-controls label {
        margin-right: 10px;
      }
    </style>
  </head>

    <label for="samplingSpeed">Sampling Speed (ms):</label>
    <input type="number" id="samplingSpeed" value="50" min="1" step="1">
    <button onclick="updateSpeed()">Set Speed</button>

    <script>
      function updateSpeed() {
        const speed = document.getElementById("samplingSpeed").value;
        fetch(`/setSpeed?value=${speed}`)
          .then(response => response.text())
          .then(text => {
            console.log("Speed response:", text);
          })
          .catch(error => {
            console.error("Error setting speed:", error);
          });
      }
    </script>

  <body>
    <h2>ESP32 Oscilloscope</h2>
    <button onclick="toggleMode()">Toggle Mode</button>
    <button onclick="captureWaveform()">Capture</button>
    <div id="channel-controls">
    <label><input type="checkbox" id="ch0" checked> CH0</label>
    <label><input type="checkbox" id="ch1" checked> CH1</label>
    <label><input type="checkbox" id="ch2" checked> CH2</label>
    <label><input type="checkbox" id="ch3" checked> CH3</label>
    <label><input type="checkbox" id="ch4" checked> CH4</label>
    <label><input type="checkbox" id="ch5" checked> CH5</label>
    <button onclick="updateChannels()">Apply</button>
    </div>
    <canvas id="oscilloscope" width="800" height="400"></canvas>
    <div class="legend">
      <p>CH1: GPIO34 | CH2: GPIO35 | CH3: GPIO32 | CH4: GPIO33 | CH5: GPIO36 | CH6: GPIO39</p>
    </div>
    <script>
      let ctx = document.getElementById('oscilloscope').getContext('2d');
      let chart;
      let continuousMode = true;

      async function fetchData() {
        const response = await fetch('/data');
        const json = await response.json();
        updateChart(json);
      }

      function toggleMode() {
        continuousMode = !continuousMode;
      }

      function captureWaveform() {
        fetchData();
      }

      function updateChannels() {
        let channelBits = '';
        for (let i = 0; i < 6; i++) {
          channelBits += document.getElementById("ch" + i).checked ? '1' : '0';
        }

        fetch('/setChannels', {
          method: 'POST',
          headers: {
            'Content-Type': 'text/plain'
          },
          body: channelBits
        })
        .then(res => res.text())
        .then(txt => console.log(txt));
      }

      function updateChart(data) {
        if (!chart) {
          chart = new Chart(ctx, {
            type: 'line',
            data: {
              labels: Array.from({ length: data.channels[0].length }, (_, i) => i),
              datasets: data.channels.map((channel, index) => ({
                label: 'CH' + (index + 1),
                data: channel,
                borderColor: `hsl(${index * 60}, 100%, 50%)`,
                fill: false,
                tension: 0.1
              }))
            },
            options: {
              animation: false,
              scales: {
                y: {
                  beginAtZero: true,
                  suggestedMax: 4095
                }
              }
            }
          });
        } else {
          chart.data.datasets.forEach((dataset, i) => {
            dataset.data = data.channels[i];
          });
          chart.update();
        }
      }

      function fetchData() {
        fetch("/data")
            .then(res => res.json())
            .then(data => {
              chart.data.datasets = data.channels.map((channelData, index) => ({
                label: "Channel " + (index + 1),
                data: channelData,
                borderColor: colors[index % colors.length],
                fill: false,
                tension: 0
              }));
              chart.update();
            });
        } 

      setInterval(() => {
        if (continuousMode) fetchData();
      }, 500);
    </script>
    <script src="/chart.js"></script>
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
        Serial.println("\nTimeout reached.");
        wifiConnected = false;
        return;
      }
    }

    wifiConnected = true;
    Serial.print("\nConnected, IP: ");
    Serial.println(WiFi.localIP());
  }

  void startAP() {
    WiFi.softAP(ap_ssid, ap_password);
    Serial.println("Access Point started");
    Serial.println(WiFi.softAPIP());
  }


// ----------------------------
//     WAVEFORM CAPTURE
// ----------------------------
  void captureWaveform() {
    for (int i = 0; i < SAMPLES; i++) {
      for (int ch = 0; ch < NUM_CHANNELS; ch++) {
        if (channelEnabled[ch]) {
          waveformData[ch][i] = analogRead(analogPins[ch]);
        } else {
          waveformData[ch][i] = 0; // Or -1 if you want to mark it "disabled"
        }
      }
      delay(captureInterval);  // Keep your current timing
    }
    Serial.println("Waveform captured");
  }

// ----------------------------
//       HTTP HANDLERS
// ----------------------------
  void handleRoot() {
    server.send(200, "text/html", htmlPage);
  }

  void handleData() {
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


  void handleNotFound() {
    server.send(404, "text/plain", "404: Not Found");
  }

// ----------------------------
//      SERVER ROUTES
// ----------------------------
  void setupServerRoutes() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/data", HTTP_GET, handleData);
    server.on("/chart.js", HTTP_GET, []() {
      server.send_P(200, "application/javascript", chartJS);
    });

    server.on("/setMode", HTTP_GET, []() {
      if (server.hasArg("mode")) {
        String mode = server.arg("mode");
        isSnapshotMode = (mode == "snapshot");
        server.send(200, "text/plain", "Mode set to " + mode);
      } else {
        server.send(400, "text/plain", "Missing 'mode' argument");
      }
    });

    server.on("/setSpeed", HTTP_GET, []() {
      if (server.hasArg("value")) {
        captureInterval = server.arg("value").toInt();
        Serial.print("Capture interval set to: ");
        Serial.println(captureInterval);
        server.send(200, "text/plain", "Speed updated");
      } else {
        server.send(400, "text/plain", "Missing parameter");
      }
    });

      server.on("/setChannels", HTTP_POST, []() {
        if (server.hasArg("plain")) {
          String body = server.arg("plain");
          for (int i = 0; i < 6 && i < body.length(); i++) {
            channelEnabled[i] = (body[i] == '1');
          }
          server.send(200, "text/plain", "Channel settings updated");
        } else {
          server.send(400, "text/plain", "Missing body");
        }
      });

      server.on("/toggleChannel", HTTP_GET, []() {
        if (server.hasArg("ch") && server.hasArg("state")) {
          int ch = server.arg("ch").toInt();
          int state = server.arg("state").toInt();
          if (ch >= 0 && ch < NUM_CHANNELS) {
            channelEnabled[ch] = (state == 1);
            server.send(200, "text/plain", "Channel " + String(ch) + " set to " + String(state));
          } else {
            server.send(400, "text/plain", "Invalid channel index");
          }
        } else {
          server.send(400, "text/plain", "Missing 'ch' or 'state' parameter");
        }
      });

    server.onNotFound(handleNotFound);
  }

// ----------------------------
//            SETUP
// ----------------------------
  void setup() {
    Serial.begin(115200);
    pinMode(triggerPin, INPUT_PULLUP);

    connectWiFi();
    if (!wifiConnected) startAP();

    setupServerRoutes();
    server.begin();
    Serial.println("Server started");

    pinMode(LED_PIN, OUTPUT);
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
      // Network Indicator LED. Will blink on WLAN and steady on AP
      unsigned long previousMillis = 0;
      const long blinkInterval = 500;

      if (!wifiConnected) {
        digitalWrite(LED_PIN, HIGH); // solid
      } else {
        unsigned long currentMillis = millis();
        if (currentMillis - previousMillis >= blinkInterval) {
          previousMillis = currentMillis;
          digitalWrite(LED_PIN, !digitalRead(LED_PIN)); // toggle
        }
      }
  }
