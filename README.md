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

The password is not hardcoded in the app. It is saved only in the browser local storage after you enter it.

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
```

The dashboard publishes commands to:

```text
water_tank/cmd/pump
water_tank/cmd/mode
water_tank/cmd/reset
```

Your ESP32 firmware requires command payloads in this format:

```text
COMMAND|HMAC_SHA256
```

So pump/mode/restart controls need the same HMAC secret used by the ESP32 sketch. Enter it in the app settings as `Command HMAC Secret`.

## Run

For quick desktop testing, open `index.html` in a browser.

For phone install, host the folder on HTTPS, for example GitHub Pages. Then open the HTTPS URL on the phone and use the browser option `Add to Home screen`.
