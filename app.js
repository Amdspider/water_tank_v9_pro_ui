/* app.js - IoT MQTT Dashboard Engine with Integrated Simulation */

// Default Connection Credentials (from ESP32 configuration)
const DEFAULT_CONFIG = {
  host: '3356a8cf8c9943d183bec9e288fc9d4c.s1.eu.hivemq.cloud',
  port: 8884,
  path: '/mqtt',
  user: 'spider.home',
  pass: '',
  hmacSecret: '',
  clientId: 'spider-web-' + Math.random().toString(16).substring(2, 8)
};

// Global App State
let config = { ...DEFAULT_CONFIG };
let client = null;
let simulated = false;
let simulationInterval = null;
let activeAlerts = [];

// Visual charts tracking
let levelChart = null;
let usageChart = null;
const levelHistory = [];
const timeLabels = [];
const hourlyUsageData = Array(24).fill(0);

// Sound effects active state
const audioCtx = new (window.AudioContext || window.webkitAudioContext)();
function playClickSound() {
  try {
    const osc = audioCtx.createOscillator();
    const gain = audioCtx.createGain();
    osc.connect(gain);
    gain.connect(audioCtx.destination);
    
    osc.type = 'sine';
    osc.frequency.setValueAtTime(1400, audioCtx.currentTime);
    osc.frequency.exponentialRampToValueAtTime(800, audioCtx.currentTime + 0.1);
    
    gain.gain.setValueAtTime(0.05, audioCtx.currentTime);
    gain.gain.linearRampToValueAtTime(0.001, audioCtx.currentTime + 0.1);
    
    osc.start();
    osc.stop(audioCtx.currentTime + 0.1);
  } catch (e) {
    // Audio Context blocked by autoplay policies until interaction
  }
}

// ── Initial Setup & UI Bindings ─────────────────────────────────────
document.addEventListener("DOMContentLoaded", () => {
  loadConfig();
  populateForm();
  initializeCharts();
  
  // Settings drawer toggle
  const settingsBtn = document.getElementById("settings-toggle-btn");
  const settingsPanel = document.getElementById("settings-panel");
  settingsBtn.addEventListener("click", () => {
    playClickSound();
    settingsPanel.classList.toggle("collapsed");
  });

  // Alerts sidebar toggle
  const alertsBtn = document.getElementById("alerts-toggle-btn");
  const alertsCloseBtn = document.getElementById("alerts-close-btn");
  const alertsSidebar = document.getElementById("alerts-drawer");
  
  alertsBtn.addEventListener("click", () => {
    playClickSound();
    alertsSidebar.classList.toggle("closed");
    // Clear badge when opened
    document.getElementById("notification-badge").classList.add("hidden");
    document.getElementById("notification-badge").textContent = "0";
  });
  
  alertsCloseBtn.addEventListener("click", () => {
    playClickSound();
    alertsSidebar.classList.add("closed");
  });

  // Password hide/reveal logic
  const togglePassBtn = document.getElementById("toggle-password-btn");
  const passInput = document.getElementById("mqtt-pass");
  togglePassBtn.addEventListener("click", () => {
    playClickSound();
    if (passInput.type === "password") {
      passInput.type = "text";
      togglePassBtn.style.color = "var(--accent-blue)";
    } else {
      passInput.type = "password";
      togglePassBtn.style.color = "var(--text-secondary)";
    }
  });

  const toggleHmacBtn = document.getElementById("toggle-hmac-btn");
  const hmacInput = document.getElementById("mqtt-hmac-secret");
  toggleHmacBtn.addEventListener("click", () => {
    playClickSound();
    if (hmacInput.type === "password") {
      hmacInput.type = "text";
      toggleHmacBtn.style.color = "var(--accent-blue)";
    } else {
      hmacInput.type = "password";
      toggleHmacBtn.style.color = "var(--text-secondary)";
    }
  });

  // Reset defaults button
  document.getElementById("reset-defaults-btn").addEventListener("click", () => {
    playClickSound();
    if (confirm("Reset connection settings to standard ESP32 defaults?")) {
      config = { ...DEFAULT_CONFIG };
      saveConfig();
      populateForm();
    }
  });

  // Form submission connect trigger
  document.getElementById("connection-form").addEventListener("submit", (e) => {
    e.preventDefault();
    playClickSound();
    
    config.host = document.getElementById("mqtt-host").value.trim();
    config.port = parseInt(document.getElementById("mqtt-port").value.trim());
    config.path = document.getElementById("mqtt-path").value.trim();
    config.user = document.getElementById("mqtt-user").value.trim();
    config.pass = document.getElementById("mqtt-pass").value.trim();
    config.hmacSecret = document.getElementById("mqtt-hmac-secret").value.trim();
    config.clientId = document.getElementById("mqtt-client-id").value.trim();
    
    saveConfig();
    settingsPanel.classList.add("collapsed");
    connectMQTT();
  });

  // Manual Pump toggle switch handler
  const pumpBtn = document.getElementById("pump-toggle-btn");
  pumpBtn.addEventListener("click", () => {
    playClickSound();
    const isRunning = pumpBtn.classList.contains("active");
    const nextState = isRunning ? "OFF" : "ON";
    
    if (simulated) {
      updatePumpUI(nextState);
    } else {
      publishCommand("water_tank/cmd/pump", nextState);
    }
  });

  // Data reset actions
  document.getElementById("reset-usage-btn").addEventListener("click", () => {
    playClickSound();
    if (confirm("Restart the ESP32 controller?")) {
      if (simulated) {
        injectAlert("INFO", "Demo Restart", "Simulated controller restart acknowledged.");
      } else {
        publishCommand("water_tank/cmd/reset", "RESET");
      }
    }
  });

  document.getElementById("clear-alerts-btn").addEventListener("click", () => {
    playClickSound();
    clearAlertsFeed();
  });

  // Chart Tab Toggles
  const tabLvl = document.getElementById("chart-lvl-btn");
  const tabUsg = document.getElementById("chart-usage-btn");
  const lvlContainer = document.getElementById("level-chart-container");
  const usgContainer = document.getElementById("usage-chart-container");

  tabLvl.addEventListener("click", () => {
    playClickSound();
    tabLvl.classList.add("active");
    tabUsg.classList.remove("active");
    lvlContainer.classList.remove("hidden");
    usgContainer.classList.add("hidden");
  });

  tabUsg.addEventListener("click", () => {
    playClickSound();
    tabUsg.classList.add("active");
    tabLvl.classList.remove("active");
    usgContainer.classList.remove("hidden");
    lvlContainer.classList.add("hidden");
    setTimeout(() => { usageChart.update(); }, 100);
  });

  // Kickstart connection only after password is entered.
  if (config.pass) {
    connectMQTT();
  } else {
    updateConnectionPill("disconnected", "Enter password");
    settingsPanel.classList.remove("collapsed");
  }
});

// ── Configuration Persistence ────────────────────────────────────────
function loadConfig() {
  const saved = localStorage.getItem("spider_config") || localStorage.getItem("aquafsm_config");
  if (saved) {
    try {
      config = { ...DEFAULT_CONFIG, ...JSON.parse(saved) };
    } catch (e) {
      config = { ...DEFAULT_CONFIG };
    }
  }
}

function saveConfig() {
  localStorage.setItem("spider_config", JSON.stringify(config));
}

function populateForm() {
  document.getElementById("mqtt-host").value = config.host;
  document.getElementById("mqtt-port").value = config.port;
  document.getElementById("mqtt-path").value = config.path;
  document.getElementById("mqtt-user").value = config.user;
  document.getElementById("mqtt-pass").value = config.pass;
  document.getElementById("mqtt-hmac-secret").value = config.hmacSecret || "";
  document.getElementById("mqtt-client-id").value = config.clientId;
}

// ── MQTT Client Management ──────────────────────────────────────────
function connectMQTT() {
  stopSimulation();
  
  if (client) {
    try { client.end(); } catch(e){}
  }
  
  if (!window.mqtt) {
    updateConnectionPill("disconnected", "MQTT lib missing");
    injectAlert("CRITICAL", "App Library Missing", "MQTT.js did not load. Check internet access or host the library locally.");
    return;
  }

  if (!config.pass) {
    updateConnectionPill("disconnected", "Password needed");
    injectAlert("WARNING", "Connection Not Started", "Enter the HiveMQ Cloud password in settings, then connect.");
    return;
  }

  updateConnectionPill("connecting", "Connecting...");
  
  const options = {
    clientId: config.clientId,
    username: config.user,
    password: config.pass,
    clean: true,
    connectTimeout: 7000,
    reconnectPeriod: 5000
  };
  
  const brokerUrl = `wss://${config.host}:${config.port}${config.path}`;
  console.log(`Connecting to secure WSS broker: ${brokerUrl}`);
  
  try {
    client = mqtt.connect(brokerUrl, options);
  } catch (err) {
    console.error("MQTT Connect exception:", err);
    updateConnectionPill("disconnected", "Error");
    injectAlert("WARNING", "MQTT Connection Refused", err?.message || "Could not open the WebSocket connection.");
    return;
  }
  
  // Timeout monitor with clear feedback. No fake data is shown on failure.
  const connectTimer = setTimeout(() => {
    if (client && !client.connected) {
      console.warn("Connection timeout.");
      updateConnectionPill("disconnected", "Timeout");
      injectAlert("WARNING", "MQTT Timeout", "Could not connect. Check host, WSS port 8884, path /mqtt, username, and password.");
    }
  }, 8000);
  
  client.on("connect", () => {
    clearTimeout(connectTimer);
    console.log("Connected directly to HiveMQ Cloud WSS");
    updateConnectionPill("connected", "Connected");
    
    // Subscribe to all tank telemetry topics
    const topics = [
      "water_tank/level", "water_tank/pump", "water_tank/voltage",
      "water_tank/alert", "water_tank/usage", "water_tank/mode",
      "water_tank/status", "water_tank/fsm_state", "water_tank/health",
      "water_tank/leak", "water_tank/fill_eta", "water_tank/leak_score"
    ];
    
    topics.forEach(topic => {
      client.subscribe(topic, { qos: 0 }, (err) => {
        if (!err) console.log(`Subscribed: ${topic}`);
      });
    });
  });
  
  client.on("message", (topic, payload) => {
    const msg = payload.toString().trim();
    console.log(`[MQTT Rx] ${topic}: ${msg}`);
    handleBrokerPayload(topic, msg);
  });
  
  client.on("close", () => {
    console.warn("WSS Connection Closed");
    if (!simulated) {
      updateConnectionPill("disconnected", "Offline");
    }
  });
  
  client.on("error", (err) => {
    console.error("MQTT Connection Error:", err);
    updateConnectionPill("disconnected", "Error");
    injectAlert("WARNING", "MQTT Error", err?.message || "Connection failed.");
  });
}

async function signCommandPayload(payload) {
  const secret = (config.hmacSecret || "").trim();
  if (!secret) {
    throw new Error("Command HMAC secret is required for pump and mode control.");
  }
  if (!window.crypto?.subtle) {
    throw new Error("This browser does not support Web Crypto HMAC signing.");
  }
  const encoder = new TextEncoder();
  const key = await crypto.subtle.importKey(
    "raw",
    encoder.encode(secret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"]
  );
  const signature = await crypto.subtle.sign("HMAC", key, encoder.encode(payload));
  const hex = Array.from(new Uint8Array(signature))
    .map(b => b.toString(16).padStart(2, "0"))
    .join("");
  return `${payload}|${hex}`;
}

async function publishCommand(topic, payload) {
  if (client && client.connected) {
    let signedPayload;
    try {
      signedPayload = await signCommandPayload(payload);
    } catch (err) {
      alert(err.message);
      return;
    }
    client.publish(topic, signedPayload, { qos: 1, retain: false }, (err) => {
      if (err) {
        console.error(`Pub failed for ${topic}:`, err);
      } else {
        console.log(`[MQTT Tx] ${topic}: ${payload}|<hmac>`);
      }
    });
  } else {
    alert("Offline: Command cannot be published. Connect to HiveMQ first.");
  }
}

function publishMode(mode) {
  playClickSound();
  if (simulated) {
    updateModeUI(mode);
    if (mode === "AUTO") {
      simulateAutoState();
    }
  } else {
    publishCommand("water_tank/cmd/mode", mode);
  }
}

// ── Telemetry Payload Router ──────────────────────────────────────────
function handleBrokerPayload(topic, msg) {
  switch (topic) {
    case "water_tank/level":
      updateLevelUI(parseFloat(msg));
      break;
    case "water_tank/pump":
      updatePumpUI(msg);
      break;
    case "water_tank/voltage":
      updateVoltageUI(parseFloat(msg));
      break;
    case "water_tank/usage":
      updateUsageUI(parseFloat(msg));
      break;
    case "water_tank/mode":
      updateModeUI(msg);
      break;
    case "water_tank/fill_eta":
      updateEtaUI(parseFloat(msg));
      break;
    case "water_tank/leak":
      updateLeakUI(msg);
      break;
    case "water_tank/leak_score":
      updateLeakScoreUI(parseInt(msg));
      break;
    case "water_tank/alert":
      injectAlert("CRITICAL", "Hardware Incident", msg);
      break;
    case "water_tank/health":
      handleHealthPayload(msg);
      break;
    default:
      break;
  }
}

function handleHealthPayload(msg) {
  // Format: runSecs,cycles
  const parts = msg.split(",");
  if (parts.length >= 2) {
    const runSecs = parseInt(parts[0]);
    const cycles = parseInt(parts[1]);
    console.log(`Motor health: cycles=${cycles}, runTime=${runSecs}s`);
  }
}

// ── UI Telemetry Updates ──────────────────────────────────────────────
function updateLevelUI(level) {
  level = Math.max(0, Math.min(100, level));
  
  // Set percentage string
  document.getElementById("level-percentage").textContent = `${Math.round(level)}%`;
  
  // Update volume (1000 litres capacity scale)
  const volume = Math.round(level * 10);
  document.getElementById("volume-readout").textContent = `${volume} Litres`;
  
  // SVG outer progress ring mapping (dashoffset goes from 534 to 0 as level goes 0 to 100)
  const ringOffset = 534 - (level * 534) / 100;
  const progressRing = document.getElementById("ring-progress-bar");
  progressRing.style.strokeDashoffset = ringOffset;
  
  // Color code progress indicator based on level
  if (level >= 90) {
    progressRing.style.stroke = "var(--accent-green)";
  } else if (level >= 30) {
    progressRing.style.stroke = "var(--accent-cyan)";
  } else if (level >= 10) {
    progressRing.style.stroke = "var(--accent-yellow)";
  } else {
    progressRing.style.stroke = "var(--accent-red)";
  }

  // Adjust fluid waves vertical displacement
  // Geometry: translateY(180px) is empty tank, translateY(20px) is full tank
  const waveTranslate = 180 - (level * 1.6);
  document.getElementById("wave-back").style.transform = `translateY(${waveTranslate}px)`;
  document.getElementById("wave-front").style.transform = `translateY(${waveTranslate}px)`;
  
  // Push level records to the analytics line chart
  const now = new Date();
  const timeStr = now.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
  
  levelHistory.push(level);
  timeLabels.push(timeStr);
  
  // Cap at 15 samples
  if (levelHistory.length > 15) {
    levelHistory.shift();
    timeLabels.shift();
  }
  
  if (levelChart) {
    levelChart.update();
  }
}

function updatePumpUI(state) {
  const switchBtn = document.getElementById("pump-toggle-btn");
  const label = document.getElementById("pump-state-label");
  const robotAnim = document.getElementById("gauge-robot-anim");
  const waveFront = document.getElementById("wave-front");
  const waveBack = document.getElementById("wave-back");
  
  if (state === "ON" || state === "RUNNING") {
    switchBtn.classList.add("active");
    label.textContent = "PUMPING";
    label.className = "pump-lbl-running";
    robotAnim.classList.remove("hidden");
    
    // Speed up wave visual animation when pump is active
    waveFront.style.animationDuration = "2s";
    waveBack.style.animationDuration = "3.2s";
  } else {
    switchBtn.classList.remove("active");
    label.textContent = "STOPPED";
    label.className = "pump-lbl-stopped";
    robotAnim.classList.add("hidden");
    
    // Slower calm waves when idle
    waveFront.style.animationDuration = "4s";
    waveBack.style.animationDuration = "6s";
    
    // Clear eta values
    document.getElementById("fill-eta-val").textContent = "-- min";
  }
}

function updateVoltageUI(volts) {
  document.getElementById("voltage-val").textContent = `${Math.round(volts)}V`;
  
  const statusPill = document.getElementById("voltage-status-pill");
  const card = document.getElementById("voltage-card");
  
  if (volts >= 180 && volts <= 250) {
    statusPill.textContent = "Safe Range";
    statusPill.className = "pill normal";
    card.classList.remove("voltage-fault");
  } else if (volts === 0) {
    statusPill.textContent = "NO POWER";
    statusPill.className = "pill critical";
    card.classList.add("voltage-fault");
  } else {
    statusPill.textContent = volts > 250 ? "OVERVOLT" : "UNDERVOLT";
    statusPill.className = "pill critical";
    card.classList.add("voltage-fault");
    injectAlert("CRITICAL", "Voltage Fault", `Anomalous supply grid voltage caught at ${Math.round(volts)}V AC.`);
  }
}

function updateUsageUI(litres) {
  document.getElementById("daily-usage-val").textContent = `${Math.round(litres)} L`;
  
  // Spread usage roughly into analytics hourly usage tracker
  const hr = new Date().getHours();
  hourlyUsageData[hr] = litres;
  if (usageChart) {
    usageChart.update();
  }
}

function updateModeUI(mode) {
  document.getElementById("mode-auto-btn").classList.remove("active");
  document.getElementById("mode-manual-btn").classList.remove("active");
  document.getElementById("mode-maint-btn").classList.remove("active");
  
  if (mode === "AUTO") {
    document.getElementById("mode-auto-btn").classList.add("active");
  } else if (mode === "MANUAL") {
    document.getElementById("mode-manual-btn").classList.add("active");
  } else if (mode === "MAINTENANCE") {
    document.getElementById("mode-maint-btn").classList.add("active");
  }
}

function updateEtaUI(minutes) {
  if (minutes > 0) {
    document.getElementById("fill-eta-val").textContent = `${Math.round(minutes)} min`;
  } else {
    document.getElementById("fill-eta-val").textContent = "-- min";
  }
}

function updateLeakUI(status) {
  const statusPill = document.getElementById("leak-status-val");
  const card = document.getElementById("leak-card");
  const footer = document.getElementById("leak-footer");
  
  if (status === "CONFIRMED" || status === "LEAK" || status === "LEAK!") {
    statusPill.textContent = "LEAK ALERT";
    statusPill.style.color = "var(--accent-orange)";
    card.classList.add("leak-fault");
    footer.textContent = "Potential pipe failure flagged";
    injectAlert("WARNING", "Leakage Detected", "Sub-surface leakage suspected. Evaluation score exceeds safe threshold.");
  } else if (status === "POSSIBLE") {
    statusPill.textContent = "SUSPICIOUS";
    statusPill.style.color = "var(--accent-yellow)";
    card.classList.remove("leak-fault");
    footer.textContent = "Stable volume evaluations pending";
  } else {
    statusPill.textContent = "SECURE";
    statusPill.style.color = "var(--accent-green)";
    card.classList.remove("leak-fault");
    footer.textContent = "Slope matching algorithm reports safe";
  }
}

function updateLeakScoreUI(score) {
  const badge = document.getElementById("leak-score-badge");
  badge.textContent = `Score: ${score}/6`;
  
  if (score >= 4) {
    badge.className = "pill score-alert";
  } else {
    badge.className = "pill secure";
  }
}

function updateConnectionPill(state, text) {
  const pill = document.getElementById("connection-status-pill");
  const label = document.getElementById("connection-status-text");
  
  pill.className = `status-pill ${state}`;
  label.textContent = text;
}

// ── High-Fidelity Alert Feed Injection ──────────────────────────────────
function injectAlert(level, title, message) {
  const timestamp = new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  
  // Deduplicate exact alerts within 10 seconds
  const duplicated = activeAlerts.find(a => a.message === message && (Date.now() - a.time) < 10000);
  if (duplicated) return;
  
  activeAlerts.unshift({ level, title, message, timestamp, time: Date.now() });
  if (activeAlerts.length > 20) activeAlerts.pop();
  
  renderAlertsFeed();
  
  // Trigger notification indicator badge
  const badge = document.getElementById("notification-badge");
  const isSidebarOpen = !document.getElementById("alerts-drawer").classList.contains("closed");
  if (!isSidebarOpen) {
    badge.classList.remove("hidden");
    const count = parseInt(badge.textContent) + 1;
    badge.textContent = count;
  }
}

function renderAlertsFeed() {
  const emptyFeed = document.getElementById("alerts-empty");
  const alertsList = document.getElementById("alerts-list");
  
  if (activeAlerts.length === 0) {
    emptyFeed.classList.remove("hidden");
    alertsList.classList.add("hidden");
    return;
  }
  
  emptyFeed.classList.add("hidden");
  alertsList.classList.remove("hidden");
  alertsList.innerHTML = "";
  
  activeAlerts.forEach(alert => {
    const li = document.createElement("li");
    li.className = `alert-item ${alert.level.toLowerCase()}`;
    
    let iconSvg = '';
    if (alert.level === 'CRITICAL') {
      iconSvg = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg>`;
    } else {
      iconSvg = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="12"/><line x1="12" y1="16" x2="12.01" y2="16"/></svg>`;
    }
    
    li.innerHTML = `
      <div class="alert-item-icon">${iconSvg}</div>
      <div class="alert-item-content">
        <h3>${alert.title}</h3>
        <p>${alert.message}</p>
      </div>
      <span class="alert-item-time">${alert.timestamp}</span>
    `;
    alertsList.appendChild(li);
  });
}

function clearAlertsFeed() {
  activeAlerts = [];
  renderAlertsFeed();
  document.getElementById("notification-badge").classList.add("hidden");
  document.getElementById("notification-badge").textContent = "0";
}

// ── Chart.js Configurations ───────────────────────────────────────────
function initializeCharts() {
  // 1. Water Level Trend Line Chart
  const ctxLvl = document.getElementById("levelTrendChart").getContext("2d");
  
  const neonCyanGrad = ctxLvl.createLinearGradient(0, 0, 0, 200);
  neonCyanGrad.addColorStop(0, 'rgba(0, 210, 255, 0.25)');
  neonCyanGrad.addColorStop(1, 'rgba(0, 102, 204, 0.0)');
  
  levelChart = new Chart(ctxLvl, {
    type: 'line',
    data: {
      labels: timeLabels,
      datasets: [{
        label: 'Level History',
        data: levelHistory,
        borderColor: '#00d2ff',
        borderWidth: 2,
        backgroundColor: neonCyanGrad,
        fill: true,
        tension: 0.4,
        pointBackgroundColor: '#fff',
        pointHoverRadius: 6,
        pointRadius: 3
      }]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      plugins: {
        legend: { display: false }
      },
      scales: {
        y: {
          min: 0,
          max: 100,
          grid: { color: 'rgba(255, 255, 255, 0.05)' },
          ticks: { color: '#86868b', font: { size: 9 } }
        },
        x: {
          grid: { display: false },
          ticks: { color: '#86868b', font: { size: 9 } }
        }
      }
    }
  });

  // 2. Hourly Consumption Bar Chart
  const ctxUsg = document.getElementById("hourlyUsageChart").getContext("2d");
  const hourlyLabels = Array.from({ length: 24 }, (_, i) => `${i.toString().padStart(2, '0')}:00`);
  
  usageChart = new Chart(ctxUsg, {
    type: 'bar',
    data: {
      labels: hourlyLabels,
      datasets: [{
        data: hourlyUsageData,
        backgroundColor: 'rgba(0, 102, 204, 0.7)',
        hoverBackgroundColor: 'var(--accent-cyan)',
        borderRadius: 4,
        borderWidth: 0
      }]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      plugins: {
        legend: { display: false }
      },
      scales: {
        y: {
          beginAtZero: true,
          grid: { color: 'rgba(255, 255, 255, 0.05)' },
          ticks: { color: '#86868b', font: { size: 9 } }
        },
        x: {
          grid: { display: false },
          ticks: { color: '#86868b', font: { size: 8 } }
        }
      }
    }
  });
}

// ── Dynamic Local Simulator Mode (FSM Logic Mirror) ─────────────────
function triggerFallbackSimulation(reason) {
  console.log(`Initiating Integrated Local Simulator Engine (Reason: ${reason})`);
  simulated = true;
  updateConnectionPill("connecting", "Simulating FSM");
  
  setTimeout(() => {
    updateConnectionPill("connected", "Simulator Live");
    
    // Seed initial values matching a typical tank FSM setup
    simState.level = 45.0;
    simState.voltage = 232.0;
    simState.usageToday = 185.0;
    simState.mode = "AUTO";
    simState.pumpState = "OFF";
    simState.leakScore = 0;
    simState.lpm = 14.8;
    
    updateLevelUI(simState.level);
    updatePumpUI(simState.pumpState);
    updateVoltageUI(simState.voltage);
    updateUsageUI(simState.usageToday);
    updateModeUI(simState.mode);
    updateEtaUI(0);
    updateLeakUI("SECURE");
    updateLeakScoreUI(0);
    document.getElementById("learned-lpm-val").textContent = `${simState.lpm.toFixed(1)} LPM`;
    
    startSimulationLoop();
  }, 1000);
}

const simState = {
  level: 45,
  voltage: 232,
  usageToday: 185,
  mode: "AUTO",
  pumpState: "OFF",
  leakScore: 0,
  lpm: 15.0
};

function startSimulationLoop() {
  stopSimulation();
  
  simulationInterval = setInterval(() => {
    // 1. Slightly fluctuate AC Grid Voltage
    simState.voltage += (Math.random() - 0.5) * 4;
    simState.voltage = Math.max(170, Math.min(260, simState.voltage));
    updateVoltageUI(simState.voltage);
    
    // Occasional grid spike simulation
    if (Math.random() > 0.98) {
      simState.voltage = Math.random() > 0.5 ? 255.0 : 172.0;
      injectAlert("CRITICAL", "Voltage Fault (SIM)", `Grid variance trigger simulated at ${Math.round(simState.voltage)}V`);
    }

    // 2. FSM Logic: Handle pump filling / water consumption drops
    if (simState.pumpState === "ON") {
      // Pump adds water (learned lpm speed factor)
      const waterAdded = (simState.lpm / 60) * 2; // scale factor per ticks
      const levelPctAdded = (waterAdded / 1000) * 100;
      simState.level += levelPctAdded;
      simState.usageToday += waterAdded;
      updateUsageUI(simState.usageToday);
      
      // Calculate remaining minutes to hit cfg.highThr (90%)
      if (simState.level < 90) {
        const litresRemaining = ((90 - simState.level) / 100) * 1000;
        const eta = litresRemaining / simState.lpm;
        updateEtaUI(eta);
      } else {
        updateEtaUI(0);
      }
      
      // Auto cut-off at highThr (90%) in AUTO mode
      if (simState.mode === "AUTO" && simState.level >= 90) {
        updatePumpUI("OFF");
        simState.pumpState = "OFF";
        injectAlert("INFO", "Filling Complete", "Water tank reached top safety threshold (90%).");
      }
    } else {
      // Slow standard consumption drop
      simState.level -= 0.05 + (Math.random() * 0.05);
      simState.level = Math.max(0, simState.level);
      
      // Auto start trigger at lowThr (30%) in AUTO mode
      if (simState.mode === "AUTO" && simState.level <= 30) {
        updatePumpUI("ON");
        simState.pumpState = "ON";
        injectAlert("INFO", "Refilling Triggered", "Water level below start threshold (30%). Starting motor.");
      }
    }
    
    updateLevelUI(simState.level);
    
    // 3. Periodic Leak Simulator checks
    if (simState.pumpState === "OFF") {
      // Small chance of showing leak behavior
      if (Math.random() > 0.99 && simState.leakScore === 0) {
        simState.leakScore = 4;
        updateLeakUI("POSSIBLE");
        updateLeakScoreUI(4);
      } else if (simState.leakScore === 4 && Math.random() > 0.9) {
        simState.leakScore = 6;
        updateLeakUI("CONFIRMED");
        updateLeakScoreUI(6);
      }
    } else {
      // Motor running clears leak flags
      simState.leakScore = 0;
      updateLeakUI("SECURE");
      updateLeakScoreUI(0);
    }
  }, 2000);
}

function simulateAutoState() {
  if (simState.level <= 30 && simState.pumpState === "OFF") {
    simState.pumpState = "ON";
    updatePumpUI("ON");
    injectAlert("INFO", "Auto-Mode Start", "AUTO FSM engaged. Low level threshold active.");
  } else if (simState.level >= 90 && simState.pumpState === "ON") {
    simState.pumpState = "OFF";
    updatePumpUI("OFF");
  }
}

function stopSimulation() {
  if (simulationInterval) {
    clearInterval(simulationInterval);
    simulationInterval = null;
  }
  simulated = false;
}
