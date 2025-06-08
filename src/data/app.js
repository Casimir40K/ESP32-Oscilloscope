
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
