# UV dosimeter (ESP32)

Port of the Uno UV dosimeter. Once a second it reads the sensor, adds `reading / 75` to a running total, and chirps the buzzer when the total crosses the dose selected at boot. After four chirps it stops sampling. Wi-Fi, a mobile-friendly web page, and ArduinoOTA keep running either way.

## Web UI (phone)

When the board is on Wi-Fi, open **`http://uv-dosimeter.local/`** in a phone browser (portrait layout: black background, large numbers). If mDNS does not resolve on your network, use the IP printed on the serial monitor at `115200` baud, for example `http://192.168.1.20/`.

The page polls **`GET /api/status`** once per second (JSON). **`POST /api/reset`** performs the same reset as power-cycling with respect to dose state: selector pins are read again, totals and round history clear, sampling restarts, and the startup beep plays.

Displayed values:

- **Current UV** — latest per-second increment the firmware uses (`ADC / 75`), with raw ADC shown underneath for debug.
- **Est. time remaining** — rough seconds until the full four-round session finishes. Assumes recent dose rate stays near the exponential moving average of the last samples (about 65% history / 35% newest per second). Remaining dose is `(4 − completed_threshold_crossings) × threshold − current_round_total`, divided by that smoothed rate. Shown only while sampling is active and rate is above zero; it is an estimate, not a guarantee.
- **Elapsed** — session time since boot or reset, current round time, and a growing list of each round’s elapsed time (from round start through chirp completion).

The UI does not replace OTA or block the 1 s sampling loop; the server is async.

## Wiring

GPIO 34 is the sensor input. It is **3.3 V maximum, not 5 V**. Do not connect a 5 V sensor output to it. The pin is input-only (ADC1), which is required because Wi-Fi uses ADC2 and ADC2 reads are invalid while Wi-Fi is on.

Selector pins use the internal pull-ups and are active-low. Ground one of them before boot or reset. The first match wins. If none is grounded, the dose stays at 10000 (the Uno sketch's default) and nothing is printed.

| ESP32 pin | Function | Original Uno pin |
| --- | --- | --- |
| GPIO 34 | UV sensor analog input, ADC1, 3.3 V max | A0 |
| GPIO 13 | Select 6000 (`uv6`), active-low, internal pull-up | D4 |
| GPIO 14 | Select 8000 (`uv8`), active-low, internal pull-up | D5 |
| GPIO 27 | Select 10000 (`uv10`), active-low, internal pull-up | D6 |
| GPIO 26 | Select 12000 (`uv12`), active-low, internal pull-up | D7 |
| GPIO 33 | Select 14000 (`uv14`), active-low, internal pull-up | D8 |
| GPIO 25 | Passive piezo buzzer | D2 |
| GND | Sensor ground, buzzer ground, and the grounded select pin | GND |

GPIO 34–39 cannot be the selectors: they have no internal pull-ups. The buzzer is not on GPIO 2, and none of the pins above are strapping pins 0, 2, 5, 12, or 15.

Boot still plays a 1300 Hz beep for 200 ms. Each later chirp is 1300 Hz, 800 Hz, 1300 Hz, 800 Hz, 200 ms each. The last tone is started and not followed by an extra wait, same as the Uno sketch.

## Secrets

SSID, Wi-Fi password, and OTA password are compiled in from a header that is not committed.

```bash
cp include/secrets.h.example include/secrets.h
```

Edit `include/secrets.h`. Leave the example file as placeholders.

## Build and serial flash

```bash
pio run
pio run -t upload
pio device monitor
```

Serial is 115200. On connect the firmware prints the IP and the OTA hostname (`uv-dosimeter`). If Wi-Fi does not come up within about 10 seconds at boot, the dosimeter runs anyway and retries in the background.

## OTA upload

The machine running PlatformIO and the board have to be on the same network. Flash a serial build with valid Wi-Fi credentials first, then note the IP from the serial monitor.

The default 4 MB partition table has two application slots, which OTA needs. Put the OTA password in `platformio_override.ini` (gitignored, loaded automatically when present). It must match `OTA_PASSWORD` in `include/secrets.h`:

```ini
[env:esp32dev-ota]
upload_flags =
  --auth=your-ota-password
```

Upload:

```bash
pio run -e esp32dev-ota -t upload --upload-port 192.168.1.20
```

`192.168.1.20` is the board's address. `uv-dosimeter.local` works when mDNS resolves. The uploader and the board must share a network; this does not cross subnets or guest isolation.
