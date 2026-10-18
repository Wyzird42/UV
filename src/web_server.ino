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
extern unsigned long roundStartMs;
extern int completedRounds;
extern unsigned long roundTimesMs[4];

static AsyncWebServer server(80);
static std::function<void()> resetCallback;
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
    padding: max(1.5rem, env(safe-area-inset-top)) 1rem max(1.5rem, env(safe-area-inset-bottom));
    max-width: 28rem;
    margin: 0 auto;
  }
  .stack {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 1.25rem;
    flex: 1;
    width: 100%;
  }
  .num {
    font-size: clamp(4.75rem, 22vw, 6.75rem);
    font-weight: 700;
    line-height: 1.05;
    font-variant-numeric: tabular-nums;
    text-align: center;
    width: 100%;
    color: #00ff00;
  }
  .rounds { list-style: none; width: 100%; }
  .rounds li {
    font-size: clamp(4.75rem, 22vw, 6.75rem);
    font-weight: 700;
    line-height: 1.05;
    font-variant-numeric: tabular-nums;
    text-align: center;
    padding: 0.15rem 0;
    color: #ffffff;
  }
  .reset-wrap { margin-top: auto; padding-top: 2rem; width: 100%; }
  .threshold {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    width: 100%;
    margin-bottom: 1.25rem;
  }
  .threshold input[type="range"] {
    flex: 1;
    min-width: 0;
    height: 2.25rem;
    accent-color: #00ff00;
    touch-action: manipulation;
  }
  .threshold-val {
    flex-shrink: 0;
    font-size: 1.35rem;
    font-weight: 600;
    font-variant-numeric: tabular-nums;
    color: #ccc;
    min-width: 1.75rem;
    text-align: right;
  }
  button.reset {
    width: 100%;
    padding: 1.1rem 1.25rem;
    font-size: 1.35rem;
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
  <div class="stack">
    <div class="num" id="uv">—</div>
    <div class="num" id="eta">—</div>
    <div class="num" id="elapsed" hidden>—</div>
    <ul class="rounds" id="rounds"></ul>
  </div>
  <div class="reset-wrap">
    <div class="threshold">
      <input type="range" id="threshold" min="5" max="15" step="1" value="10" aria-label="Target">
      <span class="threshold-val" id="threshold-val">10</span>
    </div>
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
function thresholdLevel(threshold) {
  return Math.round(threshold / 1000);
}
var thresholdEditing = false;
function syncThresholdFromStatus(threshold) {
  var slider = document.getElementById('threshold');
  var level = thresholdLevel(threshold);
  slider.value = level;
  document.getElementById('threshold-val').textContent = level;
}
async function poll() {
  try {
    var r = await fetch('/api/status');
    if (!r.ok) throw new Error('status ' + r.status);
    var d = await r.json();
    if (!thresholdEditing) {
      syncThresholdFromStatus(d.threshold);
    }
    document.getElementById('uv').textContent = d.uv.toFixed(2);
    document.getElementById('eta').textContent = d.run ? fmtEta(d.eta_sec) : '—';
    var elapsedEl = document.getElementById('elapsed');
    if (d.run) {
      elapsedEl.hidden = false;
      elapsedEl.textContent = fmt(d.elapsed_ms);
    } else {
      elapsedEl.hidden = true;
    }
    var ul = document.getElementById('rounds');
    ul.innerHTML = '';
    (d.rounds || []).forEach(function(ms) {
      var li = document.createElement('li');
      li.textContent = fmt(ms);
      ul.appendChild(li);
    });
    document.getElementById('err').hidden = true;
  } catch (e) {
    var el = document.getElementById('err');
    el.textContent = 'Update failed';
    el.hidden = false;
  }
}
var thresholdEl = document.getElementById('threshold');
thresholdEl.addEventListener('pointerdown', function() {
  thresholdEditing = true;
});
thresholdEl.addEventListener('keydown', function() {
  thresholdEditing = true;
});
thresholdEl.addEventListener('blur', function() {
  thresholdEditing = false;
});
thresholdEl.addEventListener('input', function() {
  document.getElementById('threshold-val').textContent = this.value;
});
thresholdEl.addEventListener('change', function() {
  thresholdEditing = false;
});
document.getElementById('reset-btn').addEventListener('click', async function() {
  var btn = this;
  btn.disabled = true;
  var level = document.getElementById('threshold').value;
  try {
    var r = await fetch('/api/reset?level=' + encodeURIComponent(level), { method: 'POST' });
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
    unsigned long elapsedMs = now - roundStartMs;

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
    out += F(",\"elapsed_ms\":");
    out += String(elapsedMs);
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

static bool applyThresholdLevel(int level) {
    if (level < 5 || level > 15) {
        return false;
    }
    uvSelection = level * 1000;
    saveThresholdLevel(level);
    return true;
}

void beginWebServer(std::function<void()> resetFn) {
    if (webStarted) {
        return;
    }
    resetCallback = resetFn;

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
            if (!request->hasParam("level")) {
                request->send(400, "application/json", "{\"ok\":false}");
                return;
            }
            int level = request->getParam("level")->value().toInt();
            if (!applyThresholdLevel(level)) {
                request->send(400, "application/json", "{\"ok\":false}");
                return;
            }
            if (resetCallback) {
                resetCallback();
            }
            request->send(200, "application/json", "{\"ok\":true}");
        });

    server.begin();
    webStarted = true;
    Serial.println("Web UI: http://uv.local/ (or device IP)");
}
