/* app.js - IoT MQTT Dashboard Engine with Integrated Simulation */

// Default Connection Credentials (from ESP32 configuration)
const DEFAULT_CONFIG = {
  host: '3356a8cf8c9943d183bec9e288fc9d4c.s1.eu.hivemq.cloud',
  port: 8884,
  path: '/mqtt',
  user: 'spider.home',
  pass: '',
  hmacSecret: '',
  clientId: 'spider-web-' + Math.random().toString(16).substring(2, 8),
  rememberSecrets: true
};

// Global App State
let config = { ...DEFAULT_CONFIG };
let client = null;
let simulated = false;
let simulationInterval = null;
let activeAlerts = [];
let adminUnlocked = false;

// Visual charts tracking
let levelChart = null;
let usageChart = null;
const levelHistory = [];
const timeLabels = [];
let hourlyUsageData = Array(24).fill(0);
let lastUsageTotal = null;
let usageAnalyticsDate = currentDateKey();

const USAGE_ANALYTICS_KEY = "spider_hourly_usage_v1";
const ADMIN_AUTH_KEY = "spider_admin_auth_v1";
const ADMIN_SESSION_KEY = "spider_admin_session_v1";
const ADMIN_SESSION_TTL_MS = 30 * 24 * 60 * 60 * 1000; // 30 days session TTL

const debugSnapshot = {
  firmware: "--",
  buildDate: "--",
  chipModel: "--",
  cpuMhz: "--",
  cores: "--",
  freeHeap: "--",
  minHeap: "--",
  uptime: "--",
  wifiRssi: "--",
  ip: "--",
  mqttState: "--",
  systemState: "--",
  pumpState: "--",
  pumpMode: "--",
  sensorRaw: "--",
  sensorUpdatedAt: "--",
  ultrasonicDistance: "--",
  waterLevelRaw: "--",
  waterLevelSmooth: "--",
  voltage: "--",
  resetReason: "--",
  taskLag: "--",
  heartbeat: "--",
  mqttReconnects: "--",
  errorFlags: "--",
  lastAlert: "--"
};

const debugFieldLabels = [
  ["firmware", "Firmware Version"],
  ["buildDate", "Build Date"],
  ["chipModel", "Chip Model"],
  ["cpuMhz", "CPU MHz"],
  ["cores", "Core Count"],
  ["freeHeap", "Free Heap"],
  ["minHeap", "Minimum Heap"],
  ["uptime", "Uptime"],
  ["wifiRssi", "WiFi RSSI"],
  ["ip", "Device IP"],
  ["mqttState", "MQTT State"],
  ["systemState", "System State"],
  ["pumpState", "Pump State"],
  ["pumpMode", "Pump Mode"],
  ["sensorRaw", "Sensor Raw Values"],
  ["sensorUpdatedAt", "Last Sensor Update"],
  ["ultrasonicDistance", "Ultrasonic Distance"],
  ["waterLevelRaw", "Water Level Raw"],
  ["waterLevelSmooth", "Water Level Smooth"],
  ["voltage", "Voltage / PZEM"],
  ["resetReason", "Last Reset Reason"],
  ["taskLag", "Task Lag"],
  ["heartbeat", "Heartbeat Timers"],
  ["mqttReconnects", "MQTT Reconnect Count"],
  ["errorFlags", "Error Flags"],
  ["lastAlert", "Last Alert Reason"]
];

// Sound effects active state
let audioCtx = null;
function playClickSound() {
  try {
    const AudioContextCtor = window.AudioContext || window.webkitAudioContext;
    if (!AudioContextCtor) return;
    if (!audioCtx) audioCtx = new AudioContextCtor();

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
  loadUsageAnalytics();
  populateForm();
  initializeCharts();
  initializeAdminDebugPanel();
  
  // Settings drawer toggle
  const settingsBtn = document.getElementById("settings-toggle-btn");
  const settingsOpenBtn = document.getElementById("settings-open-btn");
  const settingsPanel = document.getElementById("settings-panel");
  settingsBtn.addEventListener("click", () => {
    playClickSound();
    settingsPanel.classList.toggle("collapsed");
  });
  settingsOpenBtn.addEventListener("click", () => {
    playClickSound();
    settingsPanel.classList.remove("collapsed");
  });

  // Reconnect / Toggle settings drawer when clicking connection status pill
  const statusPill = document.getElementById("connection-status-pill");
  if (statusPill) {
    statusPill.addEventListener("click", () => {
      playClickSound();
      if (!client || !client.connected) {
        connectMQTT();
      } else {
        settingsPanel.classList.toggle("collapsed");
      }
    });
  }

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
    const nextPass = document.getElementById("mqtt-pass").value.trim();
    const nextHmac = document.getElementById("mqtt-hmac-secret").value.trim();
    config.pass = nextPass || config.pass;
    config.hmacSecret = nextHmac || config.hmacSecret;
    config.clientId = document.getElementById("mqtt-client-id").value.trim();
    config.rememberSecrets = document.getElementById("remember-secrets").checked;

    if (!config.pass) {
      alert("Enter the HiveMQ password once, or keep a remembered password on this device.");
      return;
    }
    
    saveConfig();
    populateForm();
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
    setTimeout(() => {
      if (usageChart) usageChart.update();
    }, 100);
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
      const parsed = JSON.parse(saved);
      config = {
        ...DEFAULT_CONFIG,
        ...parsed
      };
      if (config.rememberSecrets === false) {
        config.pass = "";
        config.hmacSecret = "";
      }
    } catch (e) {
      config = { ...DEFAULT_CONFIG };
    }
  }
}

function saveConfig() {
  const safeConfig = {
    host: config.host,
    port: config.port,
    path: config.path,
    user: config.user,
    clientId: config.clientId,
    rememberSecrets: config.rememberSecrets
  };
  if (config.rememberSecrets) {
    safeConfig.pass = config.pass;
    safeConfig.hmacSecret = config.hmacSecret;
  }
  localStorage.setItem("spider_config", JSON.stringify(safeConfig));
  localStorage.removeItem("aquafsm_config");
}

function populateForm() {
  document.getElementById("mqtt-host").value = config.host;
  document.getElementById("mqtt-port").value = config.port;
  document.getElementById("mqtt-path").value = config.path;
  document.getElementById("mqtt-user").value = config.user;
  document.getElementById("mqtt-pass").value = "";
  document.getElementById("mqtt-hmac-secret").value = "";
  const canReuseSecrets = config.rememberSecrets !== false;
  document.getElementById("mqtt-pass").placeholder = canReuseSecrets && config.pass
    ? "Saved password - leave blank to keep"
    : "Enter HiveMQ password";
  document.getElementById("mqtt-hmac-secret").placeholder = canReuseSecrets && config.hmacSecret
    ? "Saved HMAC secret - leave blank to keep"
    : "Required only for pump/mode control";
  document.getElementById("mqtt-client-id").value = config.clientId;
  document.getElementById("remember-secrets").checked = config.rememberSecrets !== false;
}

// Hourly water analytics are stored locally so refreshes do not erase the day.
function currentDateKey() {
  return new Date().toISOString().slice(0, 10);
}

function loadUsageAnalytics() {
  const saved = localStorage.getItem(USAGE_ANALYTICS_KEY);
  if (!saved) return;

  try {
    const parsed = JSON.parse(saved);
    if (parsed.date === currentDateKey() && Array.isArray(parsed.hourly)) {
      hourlyUsageData = parsed.hourly.slice(0, 24).map(v => Number(v) || 0);
      while (hourlyUsageData.length < 24) hourlyUsageData.push(0);
      lastUsageTotal = Number.isFinite(parsed.lastTotal) ? parsed.lastTotal : null;
      usageAnalyticsDate = parsed.date;
    }
  } catch (e) {
    hourlyUsageData = Array(24).fill(0);
    lastUsageTotal = null;
  }
}

function saveUsageAnalytics() {
  localStorage.setItem(USAGE_ANALYTICS_KEY, JSON.stringify({
    date: usageAnalyticsDate,
    hourly: hourlyUsageData,
    lastTotal: lastUsageTotal
  }));
}

function ensureUsageDateIsCurrent() {
  const today = currentDateKey();
  if (usageAnalyticsDate !== today) {
    usageAnalyticsDate = today;
    hourlyUsageData = Array(24).fill(0);
    lastUsageTotal = null;
    saveUsageAnalytics();
  }
}

function recordHourlyUsage(totalLitres) {
  if (!Number.isFinite(totalLitres)) return;

  ensureUsageDateIsCurrent();
  const hr = new Date().getHours();
  let delta = 0;

  if (lastUsageTotal !== null) {
    delta = totalLitres >= lastUsageTotal ? totalLitres - lastUsageTotal : totalLitres;
  }

  hourlyUsageData[hr] = Number((hourlyUsageData[hr] + Math.max(0, delta)).toFixed(2));
  lastUsageTotal = totalLitres;
  saveUsageAnalytics();
}

function formatDebugValue(value) {
  if (value === null || value === undefined || value === "") return "--";
  if (typeof value === "number") return Number.isInteger(value) ? String(value) : value.toFixed(2);
  if (Array.isArray(value)) return value.join(", ");
  if (typeof value === "object") return JSON.stringify(value);
  return String(value);
}

function renderDebugPanel() {
  const grid = document.getElementById("debug-fields-grid");
  if (!grid) return;

  document.getElementById("debug-system-state").textContent = formatDebugValue(debugSnapshot.systemState);
  document.getElementById("debug-pump-state").textContent = formatDebugValue(debugSnapshot.pumpState);
  document.getElementById("debug-mqtt-state").textContent = formatDebugValue(debugSnapshot.mqttState);
  document.getElementById("debug-wifi-rssi").textContent = formatDebugValue(debugSnapshot.wifiRssi);

  grid.innerHTML = "";
  debugFieldLabels.forEach(([key, label]) => {
    const item = document.createElement("div");
    item.className = "debug-field";
    item.innerHTML = `<span>${label}</span><strong>${formatDebugValue(debugSnapshot[key])}</strong>`;
    grid.appendChild(item);
  });
}

function updateDebugSnapshot(values) {
  Object.assign(debugSnapshot, values);
  if (adminUnlocked) renderDebugPanel();
}

function firstPresent(...values) {
  return values.find(value => value !== undefined && value !== null && value !== "");
}

function normalizeDebugPayload(raw) {
  const source = raw || {};
  return {
    firmware: firstPresent(source.firmware, source.firmwareVersion, source.version),
    buildDate: firstPresent(source.buildDate, source.build_date, source.build),
    chipModel: firstPresent(source.chipModel, source.chip_model, source.chip),
    cpuMhz: firstPresent(source.cpuMhz, source.cpu_mhz, source.cpuMHz),
    cores: firstPresent(source.cores, source.coreCount, source.core_count),
    freeHeap: firstPresent(source.freeHeap, source.free_heap),
    minHeap: firstPresent(source.minHeap, source.minimumHeap, source.min_heap),
    uptime: firstPresent(source.uptime, source.uptimeText, source.uptime_sec),
    wifiRssi: firstPresent(source.wifiRssi, source.rssi, source.wifi_rssi),
    ip: firstPresent(source.ip, source.wifiIp, source.wifi_ip),
    mqttState: firstPresent(source.mqttState, source.mqtt, source.mqtt_connected),
    systemState: firstPresent(source.systemState, source.state, source.fsm_state),
    pumpState: firstPresent(source.pumpState, source.pump),
    pumpMode: firstPresent(source.pumpMode, source.mode),
    sensorRaw: firstPresent(source.sensorRaw, source.rawSensors, source.sensor_raw),
    sensorUpdatedAt: firstPresent(source.sensorUpdatedAt, source.lastSensorUpdate, source.sensor_updated_at),
    ultrasonicDistance: firstPresent(source.ultrasonicDistance, source.distanceCm, source.distance_cm),
    waterLevelRaw: firstPresent(source.waterLevelRaw, source.levelRaw, source.level_raw),
    waterLevelSmooth: firstPresent(source.waterLevelSmooth, source.levelSmooth, source.level_smooth),
    voltage: firstPresent(source.voltage, source.lineVoltage, source.pzemVoltage),
    resetReason: firstPresent(source.resetReason, source.lastResetReason, source.reset_reason),
    taskLag: firstPresent(source.taskLag, source.task_lag),
    heartbeat: firstPresent(source.heartbeat, source.heartbeats),
    mqttReconnects: firstPresent(source.mqttReconnects, source.mqtt_reconnects),
    errorFlags: firstPresent(source.errorFlags, source.errors, source.error_flags),
    lastAlert: firstPresent(source.lastAlert, source.lastAlertReason, source.last_alert)
  };
}

function handleDebugPayload(msg) {
  if (!adminUnlocked) return;

  try {
    updateDebugSnapshot(normalizeDebugPayload(JSON.parse(msg)));
  } catch (e) {
    updateDebugSnapshot({ lastAlert: msg });
  }
}

async function hashAdminPassword(username, password, salt) {
  const encoder = new TextEncoder();
  const material = await crypto.subtle.importKey(
    "raw",
    encoder.encode(`${username}:${password}`),
    { name: "PBKDF2" },
    false,
    ["deriveBits"]
  );
  const bits = await crypto.subtle.deriveBits(
    { name: "PBKDF2", salt: encoder.encode(salt), iterations: 120000, hash: "SHA-256" },
    material,
    256
  );
  return Array.from(new Uint8Array(bits)).map(b => b.toString(16).padStart(2, "0")).join("");
}

function randomHex(bytes = 16) {
  const data = new Uint8Array(bytes);
  crypto.getRandomValues(data);
  return Array.from(data).map(b => b.toString(16).padStart(2, "0")).join("");
}

function loadAdminAuth() {
  try {
    return JSON.parse(localStorage.getItem(ADMIN_AUTH_KEY) || "null");
  } catch (e) {
    return null;
  }
}

function saveAdminSession(username) {
  localStorage.setItem(ADMIN_SESSION_KEY, JSON.stringify({
    username,
    expiresAt: Date.now() + ADMIN_SESSION_TTL_MS
  }));
}

function loadAdminSession() {
  try {
    const session = JSON.parse(localStorage.getItem(ADMIN_SESSION_KEY) || "null");
    if (session && session.expiresAt > Date.now()) return session;
  } catch (e) {}
  localStorage.removeItem(ADMIN_SESSION_KEY);
  return null;
}

function setAdminUnlocked(unlocked, username = "") {
  adminUnlocked = unlocked;
  document.getElementById("admin-login-panel").classList.toggle("hidden", unlocked);
  document.getElementById("admin-debug-content").classList.toggle("hidden", !unlocked);
  document.getElementById("admin-debug-subtitle").textContent = unlocked
    ? "Live trusted diagnostics. Keep this locked when not maintaining the controller."
    : "Locked diagnostics for trusted maintenance only.";
  document.getElementById("admin-session-label").textContent = username ? `Admin: ${username}` : "Admin unlocked";
  if (unlocked) renderDebugPanel();
}

function requestDebugSnapshot() {
  if (!adminUnlocked) return;
  if (client && client.connected) {
    publishCommand("water_tank/cmd/debug", "SNAPSHOT");
  } else if (simulated) {
    updateDebugSnapshot(makeSimulatedDebugSnapshot());
  } else {
    injectAlert("WARNING", "Debug Snapshot Not Sent", "Connect MQTT before requesting live firmware diagnostics.");
  }
}

function makeSimulatedDebugSnapshot() {
  return {
    firmware: "web-sim",
    buildDate: new Date().toLocaleString(),
    chipModel: "ESP32 simulator",
    cpuMhz: "240",
    cores: "2",
    freeHeap: "184 KB",
    minHeap: "150 KB",
    uptime: "demo session",
    wifiRssi: "-54 dBm",
    ip: "192.168.1.42",
    mqttState: simulated ? "Simulator Live" : debugSnapshot.mqttState,
    systemState: simState.pumpState === "ON" ? "filling" : "idle",
    pumpState: simState.pumpState,
    pumpMode: simState.mode,
    sensorRaw: `level=${simState.level.toFixed(1)}, voltage=${simState.voltage.toFixed(0)}`,
    sensorUpdatedAt: new Date().toLocaleTimeString(),
    ultrasonicDistance: `${(120 - simState.level).toFixed(1)} cm`,
    waterLevelRaw: `${simState.level.toFixed(1)}%`,
    waterLevelSmooth: `${simState.level.toFixed(1)}%`,
    voltage: `${Math.round(simState.voltage)}V`,
    resetReason: "software restart",
    taskLag: "< 50 ms",
    heartbeat: "healthy",
    mqttReconnects: "0",
    errorFlags: simState.leakScore >= 4 ? "LEAK_SUSPECT" : "none",
    lastAlert: activeAlerts[0]?.message || "none"
  };
}

function initializeAdminDebugPanel() {
  renderDebugPanel();

  const drawer = document.getElementById("admin-debug-drawer");
  const openBtn = document.getElementById("admin-debug-open-btn");
  const closeBtn = document.getElementById("admin-debug-close-btn");
  const form = document.getElementById("admin-login-form");
  const lockBtn = document.getElementById("admin-debug-lock-btn");
  const refreshBtn = document.getElementById("admin-debug-refresh-btn");
  const existingAuth = loadAdminAuth();

  document.getElementById("admin-login-btn").textContent = existingAuth ? "Unlock Debug" : "Create Admin";
  document.getElementById("admin-security-note").textContent = existingAuth
    ? "Admin unlock is local to this browser. Firmware must reject debug data unless its own admin check passes."
    : "First unlock creates a local salted password hash in this browser. Firmware must still enforce the same admin gate before exposing real debug data.";

  const session = loadAdminSession();
  if (session) setAdminUnlocked(true, session.username);

  openBtn.addEventListener("click", () => {
    playClickSound();
    drawer.classList.remove("closed");
  });

  closeBtn.addEventListener("click", () => {
    playClickSound();
    drawer.classList.add("closed");
  });

  lockBtn.addEventListener("click", () => {
    playClickSound();
    localStorage.removeItem(ADMIN_SESSION_KEY);
    setAdminUnlocked(false);
  });

  refreshBtn.addEventListener("click", () => {
    playClickSound();
    requestDebugSnapshot();
  });

  form.addEventListener("submit", async (e) => {
    e.preventDefault();
    playClickSound();

    if (!window.crypto?.subtle) {
      alert("Admin password hashing requires a modern browser with Web Crypto.");
      return;
    }

    const username = document.getElementById("admin-username").value.trim() || "admin";
    const password = document.getElementById("admin-password").value;
    if (password.length < 8) {
      alert("Use at least 8 characters for the admin password.");
      return;
    }

    let auth = loadAdminAuth();
    if (!auth) {
      auth = { username, salt: randomHex(16), hash: "" };
      auth.hash = await hashAdminPassword(username, password, auth.salt);
      localStorage.setItem(ADMIN_AUTH_KEY, JSON.stringify(auth));
    } else if (auth.username !== username) {
      alert("Admin username does not match this browser's configured admin.");
      return;
    } else {
      const hash = await hashAdminPassword(username, password, auth.salt);
      if (hash !== auth.hash) {
        alert("Admin password is incorrect.");
        return;
      }
    }

    document.getElementById("admin-password").value = "";
    saveAdminSession(username);
    setAdminUnlocked(true, username);
    requestDebugSnapshot();
  });
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
      "water_tank/leak", "water_tank/fill_eta", "water_tank/leak_score",
      "water_tank/debug", "water_tank/admin/debug"
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
      updateDebugSnapshot({ pumpState: msg });
      break;
    case "water_tank/voltage":
      updateVoltageUI(parseFloat(msg));
      break;
    case "water_tank/usage":
      updateUsageUI(parseFloat(msg));
      break;
    case "water_tank/mode":
      updateModeUI(msg);
      updateDebugSnapshot({ pumpMode: msg });
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
    case "water_tank/status":
    case "water_tank/fsm_state":
      updateDebugSnapshot({ systemState: msg });
      break;
    case "water_tank/debug":
    case "water_tank/admin/debug":
      handleDebugPayload(msg);
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
    updateDebugSnapshot({ uptime: `${runSecs}s`, heartbeat: `cycles=${cycles}` });
  }
}

// ── UI Telemetry Updates ──────────────────────────────────────────────
function updateLevelUI(level) {
  level = Math.max(0, Math.min(100, level));
  updateDebugSnapshot({
    waterLevelSmooth: `${level.toFixed(1)}%`,
    waterLevelRaw: `${level.toFixed(1)}%`
  });
  
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
  updateDebugSnapshot({ voltage: `${Math.round(volts)}V` });
  
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

  recordHourlyUsage(litres);
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
  updateDebugSnapshot({ mqttState: text });
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
  const levelCanvas = document.getElementById("levelTrendChart");
  const usageCanvas = document.getElementById("hourlyUsageChart");

  if (!levelCanvas || !usageCanvas) return;

  if (!window.Chart) {
    console.warn("Chart.js not loaded; chart tabs will show fallback text.");
    levelCanvas.insertAdjacentHTML("afterend", '<div class="chart-fallback">Chart library not loaded</div>');
    usageCanvas.insertAdjacentHTML("afterend", '<div class="chart-fallback">Chart library not loaded</div>');
    return;
  }

  // 1. Water Level Trend Line Chart
  const ctxLvl = levelCanvas.getContext("2d");
  
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
  const ctxUsg = usageCanvas.getContext("2d");
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
