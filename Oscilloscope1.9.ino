// ============================
//     Oscilloscope v2.2
//     Enhanced with Signal Generator
// ============================

  #include <WiFi.h>
  #include <WebServer.h>
  #include <Arduino.h>
 // #include "esp32-hal-timer.h"
  #include "chartjs_umd.h"

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
  const int analogPins[NUM_CHANNELS] = {34, 35, 32, 33, 36, 39};
  const int triggerPin = 27;
  const int signalPin = 26; // GPIO26 for signal generation

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
    int waveformType = 0;   // 0=DC, 1=Square, 2=Sine, 3=Triangle, 4=PWM
    int amplitude = 255;    // 0-255
    int frequency = 1000;   // Hz
    int pulseWidthMs = 100; // ms
    int dutyCycle = 50;     // %
    bool isEnabled = false;
    bool singlePulse = false;
    int dcOffset = 128;     // 0-255
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
    {"High Speed", 100, 10, 1, 20, 200},
    {"Balanced", 200, 100, 5, 50, 500},
    {"High Resolution", 500, 1000, 10, 200, 1000},
    {"Low Power", 50, 5000, 20, 1000, 2000}
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
    {"1kHz Square", 1, 255, 1000, 50, 128},
    {"10kHz Sine", 2, 200, 10000, 50, 128},
    {"PWM 25%", 4, 255, 1000, 25, 0},
    {"Test Signal", 1, 128, 100, 50, 64},
    {"DC 1.65V", 0, 128, 0, 0, 128}
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
  float signalPhase = 0.0;
  const long blinkInterval = 500;

  // Mutex for atomic signalConfig access
  portMUX_TYPE signalMux = portMUX_INITIALIZER_UNLOCKED;

  WebServer server(80);
  hw_timer_t *signalTimer = nullptr;

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

// ----------------------------
//      HTML Page String 
// ----------------------------
  const char htmlPage[] PROGMEM = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <title>ESP32 Oscilloscope & Signal Generator</title>
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
        height: 40vh !important;
        min-height: 250px;
        max-height: 400px;
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
      button.signal-on { background-color: #f44336; }
      button.signal-on:hover { background-color: #d32f2f; }
      button.pulse { background-color: #ff9800; }
      button.pulse:hover { background-color: #f57c00; }
      
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
      .setting-row input, .setting-row select {
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
      .signal-status {
        background-color: #2a4a2a;
        border: 2px solid #4CAF50;
        border-radius: 8px;
        padding: 10px;
        margin: 10px auto;
        max-width: 600px;
      }
      .signal-status.active {
        background-color: #4a2a2a;
        border-color: #f44336;
        animation: pulse 1s infinite;
      }
      @keyframes pulse {
        0% { opacity: 1; }
        50% { opacity: 0.7; }
        100% { opacity: 1; }
      }
      .two-column {
        display: grid;
        grid-template-columns: 1fr 1fr;
        gap: 20px;
        margin: 20px 0;
      }
      @media (max-width: 768px) {
        .two-column {
          grid-template-columns: 1fr;
        }
      }
    </style>
  </head>
  <body>
    <h2>ESP32 Oscilloscope & Signal Generator</h2>
    
    <div class="status">
      <span>Scope Mode: <span id="modeStatus">Continuous</span></span>
      <span>Status: <span id="connectionStatus">Loading...</span></span>
      <span>Samples: <span id="currentSamples">100</span></span>
      <span>Rate: <span id="currentRate">100µs</span></span>
    </div>
    
    <div class="signal-status" id="signalStatus">
      <strong>Signal Generator:</strong> 
      <span id="signalState">OFF</span> | 
      <span id="signalInfo">DC 0V</span>
    </div>
    
    <div class="controls">
      <button onclick="toggleMode()">Toggle Scope Mode</button>
      <button onclick="captureWaveform()">Manual Capture</button>
      <button onclick="clearChart()">Clear Chart</button>
      <button onclick="toggleSettings()">Settings</button>
      <button onclick="toggleSignalSettings()">Signal Generator</button>
    </div>
    
    <div class="two-column">
      <div id="settingsPanel" class="settings" style="display: none;">
        <h3>Oscilloscope Configuration</h3>
        
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

      <div id="signalPanel" class="settings" style="display: none;">
        <h3>Signal Generator (GPIO26)</h3>
        
        <div class="presets">
          <strong>Signal Presets:</strong><br>
          <button class="preset" onclick="applySignalPreset(0)">1kHz Square</button>
          <button class="preset" onclick="applySignalPreset(1)">10kHz Sine</button>
          <button class="preset" onclick="applySignalPreset(2)">PWM 25%</button>
          <button class="preset" onclick="applySignalPreset(3)">Test Signal</button>
          <button class="preset" onclick="applySignalPreset(4)">DC 1.65V</button>
        </div>
        
        <div class="setting-row">
          <label>Waveform:</label>
          <select id="waveformType">
            <option value="0">DC</option>
            <option value="1">Square</option>
            <option value="2">Sine</option>
            <option value="3">Triangle</option>
            <option value="4">PWM</option>
          </select>
        </div>
        
        <div class="setting-row">
          <label>Amplitude:</label>
          <input type="number" id="amplitude" min="0" max="255" value="128">
          <span>(0-255, ~0-3.3V)</span>
        </div>
        
        <div class="setting-row">
          <label>Frequency:</label>
          <input type="number" id="frequency" min="1" max="50000" value="1000">
          <span>Hz (1-50000)</span>
        </div>
        
        <div class="setting-row">
          <label>DC Offset:</label>
          <input type="number" id="dcOffset" min="0" max="255" value="128">
          <span>(0-255)</span>
        </div>
        
        <div class="setting-row">
          <label>PWM Duty:</label>
          <input type="number" id="dutyCycle" min="1" max="99" value="50">
          <span>% (1-99)</span>
        </div>
        
        <div class="setting-row">
          <label>Pulse Width:</label>
          <input type="number" id="pulseWidth" min="1" max="10000" value="100">
          <span>ms (1-10000)</span>
        </div>
        
        <div class="setting-row">
          <button id="signalToggle" onclick="toggleSignal()">Start Signal</button>
          <button class="pulse" onclick="sendSinglePulse()">Single Pulse</button>
          <button onclick="applySignalSettings()">Apply Settings</button>
        </div>
      </div>
    </div>
    
    <canvas id="oscilloscope"></canvas>
    
    <div class="legend">
      <p><strong>Oscilloscope Channels:</strong></p>
      <div style="display: flex; flex-wrap: wrap; justify-content: center; gap: 15px; margin-top: 8px;">
        <span style="color: #ff6384;">CH1: GPIO34</span>
        <span style="color: #36a2eb;">CH2: GPIO35</span>
        <span style="color: #ffce56;">CH3: GPIO32</span>
        <span style="color: #4bc0c0;">CH4: GPIO33</span>
        <span style="color: #9966ff;">CH5: GPIO36</span>
        <span style="color: #ff9f40;">CH6: GPIO39</span>
      </div>
      <p><strong>Signal Output:</strong> GPIO26 (DAC)</p>
    </div>
    
    <script src="/chart.js"></script>
    <script>
      let ctx = document.getElementById('oscilloscope').getContext('2d');
      let chart;
      let continuousMode = true;
      let isCapturing = false;
      let updateInterval;
      let signalEnabled = false;
      let currentSettings = {
        numSamples: 100,
        sampleRate: 100,
        channelDelay: 5,
        captureInterval: 50,
        webUpdate: 500
      };
      let currentSignalSettings = {
        waveformType: 0,
        amplitude: 128,
        frequency: 1000,
        dutyCycle: 50,
        dcOffset: 128,
        pulseWidthMs: 100
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

      const signalPresets = [
        {name: "1kHz Square", waveform: 1, amplitude: 255, frequency: 1000, dutyCycle: 50, dcOffset: 128},
        {name: "10kHz Sine", waveform: 2, amplitude: 200, frequency: 10000, dutyCycle: 50, dcOffset: 128},
        {name: "PWM 25%", waveform: 4, amplitude: 255, frequency: 1000, dutyCycle: 25, dcOffset: 0},
        {name: "Test Signal", waveform: 1, amplitude: 128, frequency: 100, dutyCycle: 50, dcOffset: 64},
        {name: "DC 1.65V", waveform: 0, amplitude: 128, frequency: 0, dutyCycle: 0, dcOffset: 128}
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
          updateSignalStatus();
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

      function toggleSignalSettings() {
        const panel = document.getElementById('signalPanel');
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

      function applySignalPreset(index) {
        const preset = signalPresets[index];
        document.getElementById('waveformType').value = preset.waveform;
        document.getElementById('amplitude').value = preset.amplitude;
        document.getElementById('frequency').value = preset.frequency;
        document.getElementById('dutyCycle').value = preset.dutyCycle;
        document.getElementById('dcOffset').value = preset.dcOffset;
        applySignalSettings();
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

      async function applySignalSettings() {
        const settings = {
          waveformType: parseInt(document.getElementById('waveformType').value),
          amplitude: parseInt(document.getElementById('amplitude').value),
          frequency: parseInt(document.getElementById('frequency').value),
          dutyCycle: parseInt(document.getElementById('dutyCycle').value),
          dcOffset: parseInt(document.getElementById('dcOffset').value),
          pulseWidthMs: parseInt(document.getElementById('pulseWidth').value)
        };

        try {
          const response = await fetch('/setSignalConfig', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(settings)
          });

          if (response.ok) {
            currentSignalSettings = settings;
            console.log('Signal settings applied successfully');
            updateSignalStatus();
          } else {
            console.error('Failed to apply signal settings');
          }
        } catch (error) {
          console.error('Error applying signal settings:', error);
        }
      }

      async function toggleSignal() {
        try {
          const response = await fetch('/toggleSignal', { method: 'POST' });
          if (response.ok) {
            const result = await response.json();
            signalEnabled = result.enabled;
            updateSignalToggleButton();
            updateSignalStatus();
          }
        } catch (error) {
          console.error('Error toggling signal:', error);
        }
      }

      async function sendSinglePulse() {
        try {
          const response = await fetch('/singlePulse', { method: 'POST' });
          if (response.ok) {
            console.log('Single pulse sent');
            setTimeout(updateSignalStatus, 100); // Update status after pulse
          }
        } catch (error) {
          console.error('Error sending pulse:', error);
        }
      }

      function updateSignalToggleButton() {
        const button = document.getElementById('signalToggle');
        if (signalEnabled) {
          button.textContent = 'Stop Signal';
          button.className = 'signal-on';
        } else {
          button.textContent = 'Start Signal';
          button.className = '';
        }
      }

      async function updateSignalStatus() {
        try {
          const response = await fetch('/getSignalStatus');
          if (response.ok) {
            const status = await response.json();
            signalEnabled = status.enabled;
            
            const statusEl = document.getElementById('signalStatus');
            const stateEl = document.getElementById('signalState');
            const infoEl = document.getElementById('signalInfo');
            
            if (status.enabled) {
              statusEl.className = 'signal-status active';
              stateEl.textContent = 'ON';
              
              const waveforms = ['DC', 'Square', 'Sine', 'Triangle', 'PWM'];
              const voltage = (status.amplitude * 3.3 / 255).toFixed(2);
              
              if (status.waveformType === 0) {
                infoEl.textContent = `DC ${voltage}V`;
              } else if (status.waveformType === 4) {
                infoEl.textContent = `PWM ${status.frequency}Hz ${status.dutyCycle}% (${voltage}V)`;
              } else {
                infoEl.textContent = `${waveforms[status.waveformType]} ${status.frequency}Hz (${voltage}V)`;
              }
            } else {
              statusEl.className = 'signal-status';
              stateEl.textContent = 'OFF';
              infoEl.textContent = 'No Signal';
            }
            
            updateSignalToggleButton();
          }
        } catch (error) {
          console.error('Error fetching signal status:', error);
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

      async function fetchCurrentSignalConfig() {
        try {
          const response = await fetch('/getSignalConfig');
          if (response.ok) {
            const config = await response.json();
            currentSignalSettings = config;
            
            // Update UI inputs
            document.getElementById('waveformType').value = config.waveformType;
            document.getElementById('amplitude').value = config.amplitude;
            document.getElementById('frequency').value = config.frequency;
            document.getElementById('dutyCycle').value = config.dutyCycle;
            document.getElementById('dcOffset').value = config.dcOffset;
            document.getElementById('pulseWidth').value = config.pulseWidthMs;
          }
        } catch (error) {
          console.error('Error fetching signal config:', error);
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
          Promise.all([fetchCurrentConfig(), fetchCurrentSignalConfig()]).then(() => {
            initChart();
            setTimeout(() => {
              fetchData();
              updateSignalStatus();
            }, 1000);
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
//   SIGNAL GENERATION LOGIC
// ----------------------------
  void updateSignalOutput() {
      int outputValue = 0;

      // Safely copy config for use in ISR
      portENTER_CRITICAL_ISR(&signalMux);
      SignalConfig config = signalConfig;
      portEXIT_CRITICAL_ISR(&signalMux);

      // Sanity checks
      if (config.amplitude < 0) config.amplitude = 0;
      if (config.amplitude > 255) config.amplitude = 255;
      if (config.dcOffset < 0) config.dcOffset = 0;
      if (config.dcOffset > 255) config.dcOffset = 255;
      if (config.frequency < 1) config.frequency = 1; // Prevent divide-by-zero!
      if (config.dutyCycle < 0) config.dutyCycle = 0;
      if (config.dutyCycle > 100) config.dutyCycle = 100;

      if (config.isEnabled) {
          float periodUs = 1000000.0 / config.frequency;
          float timeInPeriod = fmod(micros(), periodUs);
          float normalizedTime = timeInPeriod / periodUs;
          if (isnan(normalizedTime) || isinf(normalizedTime)) normalizedTime = 0.0;

          switch (config.waveformType) {
              case 0: // DC
                  outputValue = config.amplitude;
                  break;
              case 1: // Square
                  outputValue = (normalizedTime < 0.5) ? config.amplitude : 0;
                  outputValue += config.dcOffset;
                  break;
              case 2: // Sine
                  {
                      float sineValue = sin(2 * PI * normalizedTime);
                      outputValue = config.dcOffset + int((config.amplitude / 2.0) * (sineValue + 1.0));
                  }
                  break;
              case 3: // Triangle
                  {
                      float triangleValue;
                      if (normalizedTime < 0.5f) {
                          triangleValue = 4.0f * normalizedTime - 1.0f; // -1 to 1
                      } else {
                          triangleValue = 3.0f - 4.0f * normalizedTime; // 1 to -1
                      }
                      outputValue = config.dcOffset + int((config.amplitude / 2.0) * (triangleValue + 1.0f));
                  }
                  break;
              case 4: // PWM
                  {
                      float dutyCycleNorm = config.dutyCycle / 100.0;
                      outputValue = (normalizedTime < dutyCycleNorm) ? config.amplitude : 0;
                      outputValue += config.dcOffset;
                  }
                  break;
              default:
                  outputValue = 0;
          }
          if (isnan(outputValue) || isinf(outputValue)) outputValue = 0;
          outputValue = constrain(outputValue, 0, 255);
      } else {
          outputValue = 0;
      }

      dacWrite(signalPin, outputValue);
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

      portENTER_CRITICAL(&signalMux);
      if (waveformType >= 0 && waveformType <= 4) signalConfig.waveformType = waveformType;
      if (amplitude >= 0 && amplitude <= 255) signalConfig.amplitude = amplitude;
      if (frequency >= 0 && frequency <= 50000) signalConfig.frequency = frequency;
      if (dutyCycle >= 1 && dutyCycle <= 99) signalConfig.dutyCycle = dutyCycle;
      if (dcOffset >= 0 && dcOffset <= 255) signalConfig.dcOffset = dcOffset;
      if (pulseWidthMs >= 1 && pulseWidthMs <= 10000) signalConfig.pulseWidthMs = pulseWidthMs;
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
    server.on("/", HTTP_GET, handleRoot);
    server.on("/data", HTTP_GET, handleData);
    server.on("/getConfig", HTTP_GET, handleGetConfig);
    server.on("/setConfig", HTTP_POST, handleSetConfig);
    server.on("/chart.js", HTTP_GET, handleChartJS);
    server.on("/setMode", HTTP_GET, handleSetMode);

    // Signal generator endpoints
    server.on("/setSignalConfig", HTTP_POST, handleSetSignalConfig);
    server.on("/getSignalConfig", HTTP_GET, handleGetSignalConfig);
    server.on("/toggleSignal", HTTP_POST, handleToggleSignal);
    server.on("/singlePulse", HTTP_POST, handleSinglePulse);
    server.on("/getSignalStatus", HTTP_GET, handleGetSignalStatus);

    server.onNotFound(handleNotFound);
  }

  // ----------------------------
  //     SIGNAL TIMER ISR HOOK
  // ----------------------------
  void IRAM_ATTR onSignalTimer() {
    updateSignalOutput();
  }

// ----------------------------
//            SETUP
// ----------------------------
  void setup() {
    Serial.begin(115200);
    Serial.println("ESP32 Oscilloscope & Signal Generator Starting...");

    pinMode(LED_PIN, OUTPUT);
    pinMode(signalPin, OUTPUT);

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
    signalTimer = timerBegin(10000); // 10kHz timer
    timerAttachInterrupt(signalTimer, &onSignalTimer);
    timerAlarm(signalTimer, 1, true, 0); // Set to 1, but you may need 100 for 100us, or 10,000 for 1ms

  }

// ----------------------------
//             LOOP
// ----------------------------
  void loop() {
    server.handleClient();

    // Scope mode logic
    if (isSnapshotMode) {
      pinMode(triggerPin, INPUT_PULLUP); // allow button use if not generating
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
      Serial.print("isEnabled="); Serial.print(signalConfig.isEnabled);
      Serial.print(", singlePulse="); Serial.print(signalConfig.singlePulse);
      Serial.print(", amplitude="); Serial.print(signalConfig.amplitude);
      Serial.print(", frequency="); Serial.print(signalConfig.frequency);
      Serial.print(", waveformType="); Serial.println(signalConfig.waveformType);
      Serial.print("pulseActive="); Serial.print(pulseActive);
      Serial.print(", pulseStartTime="); Serial.println(pulseStartTime);
      Serial.println("--------------");
    }
  }