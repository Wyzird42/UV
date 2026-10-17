#pragma once

#include <functional>

// Starts the async HTTP server (idempotent). resetFn mirrors a hardware reset.
void beginWebServer(std::function<void()> resetFn);
