# spider

Phone-friendly web dashboard for the ESP32 smart water tank controller. It connects directly to HiveMQ Cloud using secure MQTT over WebSocket.

## Files

- `index.html` - app screen
- `style.css` - mobile/desktop UI styling
- `app.js` - MQTT connection, telemetry updates, command signing
- `manifest.webmanifest`, `sw.js`, `icon.svg` - installable PWA support

## HiveMQ Connection

Use the app settings panel:

```text
Host: 3356a8cf8c9943d183bec9e288fc9d4c.s1.eu.hivemq.cloud
Port: 8884
Path: /mqtt
Username: spider.home
Password: your HiveMQ Cloud password
```

The password is not hardcoded in the app and is not saved in browser storage. Enter it each time you open the dashboard.

## ESP32 Topics

The dashboard subscribes to:

```text
water_tank/level
water_tank/voltage
water_tank/usage
water_tank/pump
water_tank/mode
water_tank/fsm_state
water_tank/status
water_tank/fill_eta
water_tank/leak
water_tank/leak_score
water_tank/alert
water_tank/health
water_tank/debug
water_tank/admin/debug
```

The dashboard publishes commands to:

```text
water_tank/cmd/pump
water_tank/cmd/mode
water_tank/cmd/reset
water_tank/cmd/debug
```

Your ESP32 firmware requires command payloads in this format:

```text
COMMAND|HMAC_SHA256
```

So pump/mode/restart controls need the same HMAC secret used by the ESP32 sketch. Enter it in the app settings as `Command HMAC Secret`.

## Admin Debug Panel

The web app includes an admin-only debug drawer. First unlock creates a local salted password hash in the browser, and later unlocks use that hash instead of storing the password directly.

The panel can render firmware snapshots from `water_tank/debug` or `water_tank/admin/debug` as JSON. It expects fields such as firmware/build date, chip model, heap, uptime, WiFi RSSI/IP, MQTT state, FSM state, pump state/mode, raw sensor values, ultrasonic distance, raw/smoothed level, voltage/PZEM data, reset reason, heartbeat/task lag, MQTT reconnect count, error flags, and last alert reason.

Important: this browser-side lock only protects the UI. The ESP32 firmware should still enforce admin authentication before publishing real debug data.

## Water Analytics

The hourly usage chart stores per-hour water usage in browser local storage for the current day. Incoming `water_tank/usage` values are treated as a daily cumulative litre total; the app records only the positive delta into the current hour.

## Run

For quick desktop testing, open `index.html` in a browser.

For phone install, host the folder on HTTPS, for example GitHub Pages. Then open the HTTPS URL on the phone and use the browser option `Add to Home screen`.
