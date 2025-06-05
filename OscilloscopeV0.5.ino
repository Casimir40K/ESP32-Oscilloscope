#include <WiFi.h>
#include <WebServer.h>
#include "chartjs_umd.h"



// Replace with your Wi-Fi credentials
const char* ssid = "Solomon Wifi 2.4GHz";
const char* password = "Samson is splendid";

WebServer server(80);

// Analog pins (you can customize these)
const int analogPins[6] = {34, 35, 32, 33, 36, 39};

// HTML + Chart.js page
const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 Oscilloscope</title>
  <script src="/chart.js"></script>
  <style>
    body { font-family: Arial; margin: 20px; }
    canvas { max-width: 100%; }
  </style>
</head>
<body>
  <h2>ESP32 Oscilloscope</h2>
  <canvas id="oscilloscope" width="800" height="400"></canvas>
  <br>
  <button onclick="fetchData()">Capture</button>

  <script>
    let chart;
    const colors = ["red", "blue", "green", "orange", "purple", "teal"];

    window.onload = () => {
      const ctx = document.getElementById("oscilloscope").getContext("2d");
      chart = new Chart(ctx, {
        type: "line",
        data: {
          labels: Array.from({ length: 100 }, (_, i) => i),
          datasets: Array.from({ length: 6 }, (_, i) => ({
            label: "Channel " + (i + 1),
            data: [],
            borderColor: colors[i],
            fill: false,
            tension: 0.1
          }))
        },
        options: {
          animation: false,
          responsive: true,
          scales: {
            x: {
              title: { display: true, text: "Sample #" },
              grid: { display: true }
            },
            y: {
              title: { display: true, text: "ADC Value (0–4095)" },
              min: 0,
              max: 4095,
              grid: { display: true }
            }
          },
          plugins: {
            legend: { display: true }
          }
        }
      });
    };

    async function fetchData() {
      const response = await fetch("/data");
      const json = await response.json();
      json.forEach((channelData, i) => {
        chart.data.datasets[i].data = channelData;
      });
      chart.update();
    }
  </script>
</body>
</html>
)rawliteral";

// Setup Wi-Fi or fallback AP
void setupWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  unsigned long startAttempt = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 30000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect. Starting fallback AP...");
    WiFi.softAP("ESP32-Oscilloscope", "oscilloscope123");
    Serial.print("AP IP Address: ");
    Serial.println(WiFi.softAPIP());
  }
}

void handleRoot() {
  server.send_P(200, "text/html", htmlPage);
}

void handleData() {
  const int samples = 100;
  String json = "[";
  for (int ch = 0; ch < 6; ch++) {
    json += "[";
    for (int i = 0; i < samples; i++) {
      int val = analogRead(analogPins[ch]);
      json += val;
      if (i < samples - 1) json += ",";
      delayMicroseconds(50); // slight delay for stability
    }
    json += "]";
    if (ch < 5) json += ",";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  setupWiFi();

  server.on("/", handleRoot);
  server.on("/data", handleData);
  // Serve chart.js
  server.on("/chart.js", HTTP_GET, []() {
    server.send_P(200, "application/javascript", chartJS);
  });

  // Serve the main HTML interface
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", "<!DOCTYPE html><html><head><script src='/chart.js'></script></head><body>...</body></html>");
  });
  server.begin();
  Serial.println("Web server started.");
}

void loop() {
  server.handleClient();
}
