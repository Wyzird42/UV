#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>

#include "secrets.h"
#include "web_server.h"

// Dose threshold = web slider level × 1000 (same units as the Uno: sum of reading/75).
static const int DEFAULT_UV_SELECTION = 10000;

// GPIO 34 is ADC1_CH6. WiFi uses ADC2, so ADC2 reads are invalid while the
// radio is on. GPIO 34 is input-only and must stay at or below 3.3 V.
static const int PIN_UV_SENSOR = 34;

// Uno used D2. GPIO 2 is a strapping pin on the ESP32, so the buzzer moved.
static const int PIN_BUZZER = 25;

static const int SOUND_MS = 200;
static const unsigned long SAMPLE_INTERVAL_MS = 1000;
static const unsigned long WIFI_SETUP_TIMEOUT_MS = 10000;
static const unsigned long WIFI_RETRY_MS = 10000;
static const char HOSTNAME[] = "uv";
static const uint16_t CHIRP_HZ[] = {1300, 800, 1300, 800};
static const int CHIRP_TONES = 4;

int uvSelection = DEFAULT_UV_SELECTION;
float total = 0;
bool run = true;
int count = 0;

float lastSensorAdc = 0;
float lastUv = 0;
float smoothedUvRate = 0;
unsigned long sessionStartMs = 0;
unsigned long roundStartMs = 0;
int completedRounds = 0;
unsigned long roundTimesMs[4] = {0, 0, 0, 0};

bool otaReady = false;
bool wifiAnnounced = false;
unsigned long lastWifiRetryMs = 0;

bool chirpActive = false;
int chirpStep = 0;
unsigned long chirpStepMs = 0;

unsigned long lastSampleMs = 0;
bool sampledOnce = false;

void hardwareReset() {
    noTone(PIN_BUZZER);
    chirpActive = false;
    chirpStep = 0;

    total = 0;
    count = 0;
    run = true;
    completedRounds = 0;
    for (int i = 0; i < 4; i++) {
        roundTimesMs[i] = 0;
    }
    sampledOnce = false;
    lastSampleMs = 0;
    lastSensorAdc = 0;
    lastUv = 0;
    smoothedUvRate = 0;

    unsigned long now = millis();
    sessionStartMs = now;
    roundStartMs = now;

    tone(PIN_BUZZER, 1300, SOUND_MS);
}

void onChirpFinished() {
    if (completedRounds < 4 && count > 0) {
        roundTimesMs[completedRounds] = millis() - roundStartMs;
        completedRounds++;
    }
    roundStartMs = millis();
}

void startChirp() {
    chirpActive = true;
    chirpStep = 0;
    // tone() returns immediately. The core's duration stops the pin.
    tone(PIN_BUZZER, CHIRP_HZ[0], SOUND_MS);
    chirpStepMs = millis();
}

// Steps the chirp without blocking. The Uno sketch starts the fourth tone
// and returns with no delay after it; this does the same.
void serviceChirp() {
    if (!chirpActive) {
        return;
    }
    if (millis() - chirpStepMs < (unsigned long)SOUND_MS) {
        return;
    }

    chirpStep++;
    if (chirpStep >= CHIRP_TONES - 1) {
        tone(PIN_BUZZER, CHIRP_HZ[chirpStep], SOUND_MS);
        chirpActive = false;
        onChirpFinished();
        return;
    }
    tone(PIN_BUZZER, CHIRP_HZ[chirpStep], SOUND_MS);
    chirpStepMs = millis();
}

void startOta() {
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]() { Serial.println("OTA start"); });
    ArduinoOTA.onEnd([]() { Serial.println("OTA end"); });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.print("OTA error ");
        Serial.println(static_cast<int>(error));
    });
    ArduinoOTA.begin();
    otaReady = true;
    beginWebServer(hardwareReset);
}

void announceWifi() {
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("OTA hostname: ");
    Serial.println(ArduinoOTA.getHostname());
    wifiAnnounced = true;
}

void beginWifi() {
    // config() before setHostname keeps the DHCP name from being cleared
    // on connect (long-standing ESP32 Arduino quirk).
    WiFi.mode(WIFI_STA);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.setHostname(HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    lastWifiRetryMs = millis();
}

void serviceWifi() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!otaReady) {
            startOta();
        }
        if (!wifiAnnounced) {
            announceWifi();
        }
        return;
    }

    wifiAnnounced = false;
    unsigned long now = millis();
    if (now - lastWifiRetryMs < WIFI_RETRY_MS) {
        return;
    }
    lastWifiRetryMs = now;
    // Non-blocking. Samples keep their 1 s cadence while this retries.
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void sampleUv() {
    if (!run || chirpActive) {
        return;
    }

    unsigned long now = millis();
    if (sampledOnce && (now - lastSampleMs < SAMPLE_INTERVAL_MS)) {
        return;
    }
    lastSampleMs = now;
    sampledOnce = true;

    // sensor is a float, so this is floating division, as on the Uno.
    float sensor = analogRead(PIN_UV_SENSOR);
    float uv = sensor / 75;
    lastSensorAdc = sensor;
    lastUv = uv;
    if (smoothedUvRate < 0.01f) {
        smoothedUvRate = uv;
    } else {
        smoothedUvRate = smoothedUvRate * 0.65f + uv * 0.35f;
    }

    total += uv;

    if (total >= uvSelection) {
        startChirp();
        total -= uvSelection;
        if (++count >= 4) {
            run = false;
        }
    }
}

void setup() {
    Serial.begin(115200);

    unsigned long now = millis();
    sessionStartMs = now;
    roundStartMs = now;

    // The Uno sketch divides a 10-bit analogRead (0–1023) by 75.
    // ESP32 defaults to 12-bit (0–4095). Without this, the same voltage
    // would add about four times as much dose.
    analogReadResolution(10);

    pinMode(PIN_BUZZER, OUTPUT);
    tone(PIN_BUZZER, 1300, SOUND_MS);
    delay(SOUND_MS);

    beginWifi();
    unsigned long started = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - started < WIFI_SETUP_TIMEOUT_MS) {
        delay(50);
    }
    lastWifiRetryMs = millis();

    if (WiFi.status() == WL_CONNECTED) {
        startOta();
        announceWifi();
    } else {
        Serial.println("WiFi unavailable, dosimeter running, will retry");
    }
}

void loop() {
    serviceWifi();
    if (otaReady) {
        ArduinoOTA.handle();
    }
    serviceChirp();
    sampleUv();
    // One millisecond, not the Uno's delay(1000). Keeps the idle task
    // fed and still polls OTA through a chirp.
    delay(1);
}
