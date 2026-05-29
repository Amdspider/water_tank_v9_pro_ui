# AquaFSM — Premium IoT MQTT Dashboard

AquaFSM is a state-of-the-art, high-fidelity Single-Page Application (SPA) designed to monitor and control your Smart Water Tank system in real-time. Inspired by Apple's signature design principles, the panel features a sleek minimalist dark graphite theme, premium glassmorphism, responsive grids, fluid SVG keyframe animations, and micro-transitions.

It communicates directly with your **HiveMQ Cloud** broker over secure WebSockets (WSS), acting as the central interface for your ESP32-powered Smart Water Tank hardware.

---

## 🌟 Key Features

* **Apple-Style Premium UI:** Clean typography using *Plus Jakarta Sans*, backdrop blurs, floating graphical cards, and soft ambient glowing indicators.
* **SVG Liquid Wave Gauge:** A custom vector fluid gauge with smooth physics-based waving masks that speed up or slow down depending on pumping activity.
* **Collapsible Connection Manager:** An expandable interface to configure secure MQTT connection credentials. Input parameters (Host, Port, Path, Username, Password, Client ID) persist in the browser's `localStorage` for uninterrupted subsequent reloads.
* **Dynamic FSM Simulation Engine:** Integrated local hardware emulator that triggers automatically if the HiveMQ broker cannot be reached within 8 seconds, allowing off-grid visual testing of all gauges, switches, alarm logs, and graphs.
* **High-Fidelity Charts:** Interactive trend timelines using *Chart.js* highlighting real-time water levels and 24-hour water consumption bars.
* **Alert System Log Feed:** Collapsible notifications feed highlighting critical errors, dry runs, voltage faults, and piping leaks with custom iconography and timestamps.

---

## 📟 Hardware MQTT Topic Map

AquaFSM maps directly onto the telemetry topics configured in your ESP32 Arduino firmware:

### Telemetry (Subscribed by Panel)
* `water_tank/level` — Water level (float `0.0` - `100.0%`).
* `water_tank/pump` — Motor relay status (`"ON"` / `"OFF"`).
* `water_tank/voltage` — Grid supply AC voltage (float). Triggers safety color indicators in card.
* `water_tank/usage` — Accumulated consumption in Litres (EEPROM backed).
* `water_tank/mode` — Current operational state (`"AUTO"`, `"MANUAL"`, `"MAINTENANCE"`).
* `water_tank/fill_eta` — Learned estimation of minutes remaining until tank is full.
* `water_tank/leak` — Sub-surface slope detection status (`"SECURE"`, `"POSSIBLE"`, `"CONFIRMED"`).
* `water_tank/leak_score` — Diagnostic index (`0` - `6`).
* `water_tank/alert` — Urgent critical alarms broadcasted by device FSM.

### Commands (Published by Panel)
* `water_tank/cmd/pump` — Relay override command (`"ON"` / `"OFF"`).
* `water_tank/cmd/mode` — State transition trigger (`"AUTO"`, `"MANUAL"`, `"MAINTENANCE"`).
* `water_tank/cmd/reset` — EEPROM scrub commands (`"usage"` to reset consumption, `"alerts"` to clear warnings).

---

## 🚀 Easy 1-Click Setup & Run

No compile-time dependencies, node bundles, or command pipelines are required! AquaFSM runs instantly in any modern browser.

### Run Locally
Simply double-click the [index.html](index.html) file to open the interface directly in your web browser. Alternatively, run a lightweight static web server in the directory:

```bash
# Using Python
python -m http.server 8000

# Using Node / npx
npx serve .
```

---

## 🐙 Push to GitHub & Deploy to GitHub Pages

Since AquaFSM is written entirely in pure HTML5, Vanilla CSS, and JavaScript, it is a perfect candidate for **GitHub Pages**—providing you with a globally accessible, secure IoT Control Panel hosted completely for free.

### Step 1: Initialize Git Repo Locally
Open your command terminal (such as PowerShell or Bash) in this project directory and run:

```bash
# Initialize git repository
git init

# Stage all files
git add .

# Create the initial commit
git commit -m "feat: AquaFSM Apple-Style Premium Dashboard Initial Release"
```

### Step 2: Push to Your GitHub Account
1. Log into [GitHub](https://github.com) and click **New Repository**.
2. Name it (e.g., `water-tank-iot-panel`). **Do not** initialize it with a README, gitignore, or license.
3. Copy the push command blocks from the empty repository landing page and run them in your local terminal:

```bash
# Rename default branch to main
git branch -M main

# Link your local repo to the new GitHub repo
git remote add origin https://github.com/<your-github-username>/<your-repo-name>.git

# Push the codebase to GitHub
git push -u origin main
```

### Step 3: Enable Free Hosting (GitHub Pages)
1. Go to your new repository on GitHub.
2. Select **Settings** (gear icon in the top repository menu bar).
3. Navigate to **Pages** in the left sidebar menu (under the *Code and automation* section).
4. Under **Build and deployment**, set the Source to **Deploy from a branch**.
5. Set the Branch selection dropdown to `main` and the folder path to `/ (root)`.
6. Click **Save**.

Within 1-2 minutes, GitHub will build and host your panel securely at:
`https://<your-github-username>.github.io/<your-repo-name>/`

**Congratulations! Your premium IoT MQTT dashboard is now live, secure, and globally accessible from any phone or desktop!**
