#pragma once

#include <functional>

// Slider level 5–15 in NVS; default 10 when unset or invalid.
int loadThresholdLevel();
void saveThresholdLevel(int level);

// Starts the async HTTP server (idempotent). resetFn mirrors a hardware reset.
void beginWebServer(std::function<void()> resetFn);
