#include "web_server.h"

#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include <cstring>

extern int uvSelection;
extern float total;
extern bool run;
extern int count;
extern bool chirpActive;

extern float lastSensorAdc;
extern float lastUv;
extern float smoothedUvRate;
extern unsigned long sessionStartMs;
extern unsigned long roundStartMs;
extern int completedRounds;
extern unsigned long roundTimesMs[4];

static AsyncWebServer server(80);
static std::function<void()> hardwareReset;
static bool webStarted = false;

static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="theme-color" content="#000000">
<title>UV Dosimeter</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  html, body {
    min-height: 100%;
    background: #000;
    color: #f5f5f5;
    font-family: system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
    -webkit-font-smoothing: antialiased;
  }
  body {
    display: flex;
    flex-direction: column;
    padding: max(1.25rem, env(safe-area-inset-top)) 1.25rem max(1.5rem, env(safe-area-inset-bottom));
    max-width: 28rem;
    margin: 0 auto;
  }
  .label {
    font-size: 0.85rem;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    color: #888;
    margin-bottom: 0.35rem;
  }
  .block { margin-bottom: 1.75rem; }
  .uv-value {
    font-size: clamp(3.5rem, 18vw, 5rem);
    font-weight: 700;
    line-height: 1;
    font-variant-numeric: tabular-nums;
  }
  .uv-unit { font-size: 1.25rem; color: #aaa; margin-left: 0.25rem; }
  .adc { font-size: 0.95rem; color: #666; margin-top: 0.5rem; }
  .big {
    font-size: clamp(2rem, 10vw, 2.75rem);
    font-weight: 600;
    font-variant-numeric: tabular-nums;
  }
  .rounds { list-style: none; margin-top: 0.75rem; }
  .rounds li {
    font-size: 1.35rem;
    padding: 0.35rem 0;
    border-bottom: 1px solid #222;
    font-variant-numeric: tabular-nums;
  }
  .status-line { font-size: 0.9rem; color: #777; margin-top: 0.5rem; }
  .reset-wrap { margin-top: auto; padding-top: 2rem; }
  button.reset {
    width: 100%;
    padding: 1.1rem 1.25rem;
    font-size: 1.25rem;
    font-weight: 600;
    border: 2px solid #444;
    border-radius: 12px;
    background: #111;
    color: #fff;
    cursor: pointer;
    touch-action: manipulation;
  }
  button.reset:active { background: #222; }
  button.reset:disabled { opacity: 0.5; }
  .err { color: #c44; font-size: 0.9rem; margin-top: 0.5rem; }
</style>
</head>
<body>
  <section class="block">
    <div class="label">Current UV</div>
    <div><span class="uv-value" id="uv">—</span><span class="uv-unit">/s</span></div>
    <div class="adc" id="adc"></div>
  </section>
  <section class="block">
    <div class="label">Est. time remaining</div>
    <div class="big" id="eta">—</div>
    <div class="status-line" id="dose"></div>
  </section>
  <section class="block">
    <div class="label">Elapsed</div>
    <div class="big" id="session-elapsed">—</div>
    <div class="status-line">This round: <span id="round-elapsed">—</span></div>
    <ul class="rounds" id="rounds"></ul>
  </section>
  <div class="reset-wrap">
    <button type="button" class="reset" id="reset-btn">Reset</button>
    <div class="err" id="err" hidden></div>
  </div>
<script>
function fmt(ms) {
  if (ms == null || ms < 0) return '—';
  var s = Math.floor(ms / 1000);
  var m = Math.floor(s / 60);
  s = s % 60;
  var h = Math.floor(m / 60);
  m = m % 60;
  if (h > 0) return h + ':' + String(m).padStart(2,'0') + ':' + String(s).padStart(2,'0');
  return m + ':' + String(s).padStart(2,'0');
}
function fmtEta(sec) {
  if (sec == null || sec < 0) return '—';
  return fmt(sec * 1000);
}
async function poll() {
  try {
    var r = await fetch('/api/status');
    if (!r.ok) throw new Error('status ' + r.status);
    var d = await r.json();
    document.getElementById('uv').textContent = d.uv.toFixed(2);
    document.getElementById('adc').textContent = 'ADC ' + d.adc;
    document.getElementById('eta').textContent = d.run ? fmtEta(d.eta_sec) : 'Done';
    document.getElementById('dose').textContent =
      'Round ' + Math.min(d.count + 1, 4) + '/4 · ' + Math.round(d.total) + ' / ' + d.threshold;
    document.getElementById('session-elapsed').textContent = fmt(d.session_elapsed_ms);
    document.getElementById('round-elapsed').textContent = fmt(d.round_elapsed_ms);
    var ul = document.getElementById('rounds');
    ul.innerHTML = '';
    (d.rounds || []).forEach(function(ms, i) {
      var li = document.createElement('li');
      li.textContent = 'Round ' + (i + 1) + ': ' + fmt(ms);
      ul.appendChild(li);
    });
    document.getElementById('err').hidden = true;
  } catch (e) {
    var el = document.getElementById('err');
    el.textContent = 'Update failed';
    el.hidden = false;
  }
}
document.getElementById('reset-btn').addEventListener('click', async function() {
  var btn = this;
  btn.disabled = true;
  try {
    var r = await fetch('/api/reset', { method: 'POST' });
    if (!r.ok) throw new Error('reset failed');
    await poll();
  } catch (e) {
    document.getElementById('err').textContent = 'Reset failed';
    document.getElementById('err').hidden = false;
  }
  btn.disabled = false;
});
poll();
setInterval(poll, 1000);
</script>
</body>
</html>)rawliteral";

static void appendStatusJson(String& out) {
    unsigned long now = millis();
    unsigned long sessionElapsed = now - sessionStartMs;
    unsigned long roundElapsed = now - roundStartMs;

    float remainingDose = 0;
    int etaSec = -1;
    if (run && !chirpActive && smoothedUvRate > 0.01f) {
        int roundsLeft = 4 - count;
        if (roundsLeft < 0) {
            roundsLeft = 0;
        }
        remainingDose =
            (static_cast<float>(roundsLeft) * static_cast<float>(uvSelection)) - total;
        if (remainingDose < 0) {
            remainingDose = 0;
        }
        etaSec = static_cast<int>(remainingDose / smoothedUvRate + 0.5f);
    }

    out.reserve(384);
    out += F("{\"uv\":");
    out += String(lastUv, 2);
    out += F(",\"adc\":");
    out += String(static_cast<int>(lastSensorAdc));
    out += F(",\"total\":");
    out += String(total, 1);
    out += F(",\"threshold\":");
    out += String(uvSelection);
    out += F(",\"count\":");
    out += String(count);
    out += F(",\"run\":");
    out += run ? F("true") : F("false");
    out += F(",\"session_elapsed_ms\":");
    out += String(sessionElapsed);
    out += F(",\"round_elapsed_ms\":");
    out += String(roundElapsed);
    out += F(",\"eta_sec\":");
    out += String(etaSec);
    out += F(",\"rounds\":[");
    for (int i = 0; i < completedRounds; i++) {
        if (i > 0) {
            out += ',';
        }
        out += String(roundTimesMs[i]);
    }
    out += F("]}");
}

void beginWebServer(std::function<void()> resetFn) {
    if (webStarted) {
        return;
    }
    hardwareReset = resetFn;

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", INDEX_HTML);
    });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        String json;
        appendStatusJson(json);
        request->send(200, "application/json", json);
    });

    server.on(
        "/api/reset", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            if (hardwareReset) {
                hardwareReset();
            }
            request->send(200, "application/json", "{\"ok\":true}");
        });

    server.begin();
    webStarted = true;
    Serial.println("Web UI: http://uv-dosimeter.local/ (or device IP)");
}
