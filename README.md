# UV dosimeter (ESP32)

## Clone

Primary repository (GitHub):

```bash
git clone https://github.com/Wyzird42/UV.git
cd UV
```

Works on Windows (Git Bash or Git for Windows), macOS, and Linux.

## Agent / branch workflow

Same pattern as [Polyglow](https://github.com/Wyzird42/polyglow): agent work is pushed to a **branch on GitHub**; you pull it locally to build and test. Cursor may show **“PR #N”** for a Cloud Agent run—that is a run label, not a GitHub Pull Request and not necessarily the branch name. Use the branch name from the agent or from `git branch -r` on GitHub.

From the repo root on Windows:

```batch
pull.bat
pull.bat cursor\my-feature-branch
pull.bat reset
```

- `pull.bat` — fetch and update local `main`
- `pull.bat <branch>` — fetch and check out that branch (slashes as in git: `cursor/foo-bar`)
- `pull.bat reset` — discard local changes and match `origin/main`

Integrate to `main` when you are satisfied (merge on GitHub or locally—your choice).

---

Port of the Uno UV dosimeter. The main loop polls the UV sensor about every millisecond, averages the **calibrated** reading over a 5 s window, and adds that average to a running dose total. Raw scaling uses `UV_ADC_DIVISOR` at the top of `src/UV.ino` (172 on ESP32; the Uno sketch used 75), then a two-point linear map (`UV_CAL_*` constants in the same file). It chirps when the total crosses the threshold from the web UI slider (level × `doseScale`, default level 10 → 2000). After four chirps it stops accumulating dose. Wi-Fi, a mobile-friendly web page, and ArduinoOTA keep running either way.

## Web UI (phone)

When the board is on Wi-Fi, open **`http://uv.local/`** in a phone browser (portrait layout: black background, large numbers). If mDNS does not resolve on your network, use the IP printed on the serial monitor at `115200` baud, for example `http://192.168.1.20/`.

The page polls **`GET /api/status`** once per second (JSON). Set the threshold with the slider (5–15), then **`POST /api/reset?level=<n>`** (the Reset button sends the current slider value): totals and round history clear, the new threshold applies, sampling restarts, and the startup beep plays.

Displayed values:

- **Current UV** — latest calibrated UV index from the firmware (after `UV_ADC_DIVISOR` and `UV_CAL_*` linear correction), averaged over the last 5 s reporting window. JSON also includes `"adc"` (raw averaged ADC counts) for debug.
- **Est. time remaining** — rough seconds until the full four-round session finishes. Assumes recent dose rate stays near the exponential moving average of each 5 s UV update (about 65% history / 35% newest). Remaining dose is `(4 − completed_threshold_crossings) × threshold − current_round_total`, divided by that smoothed rate. Shown only while sampling is active and rate is above zero; it is an estimate, not a guarantee.
- **Elapsed** — one live timer for the round in progress (hidden after all four rounds finish). Each completed round’s time (start through chirp completion) moves into the list below when that round ends.

The UI does not replace OTA or block the main loop; the HTTP server is async.

## Wiring

GPIO 34 is the sensor input. It is **3.3 V maximum, not 5 V**. Do not connect a 5 V sensor output to it. The pin is input-only (ADC1), which is required because Wi-Fi uses ADC2 and ADC2 reads are invalid while Wi-Fi is on.

| ESP32 pin | Function | Original Uno pin |
| --- | --- | --- |
| GPIO 34 | UV sensor analog input, ADC1, 3.3 V max | A0 |
| GPIO 25 | Passive piezo buzzer | D2 |
| GND | Sensor ground and buzzer ground | GND |

The buzzer is not on GPIO 2 (ESP32 strapping pin).

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

Serial is 115200. On connect the firmware prints the IP and the OTA hostname (`uv`). If Wi-Fi does not come up within about 10 seconds at boot, the dosimeter runs anyway and retries in the background.

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

`192.168.1.20` is the board's address. `uv.local` works when mDNS resolves. The uploader and the board must share a network; this does not cross subnets or guest isolation.
