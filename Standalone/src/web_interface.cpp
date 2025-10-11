#include "pins_config.h"
#include "web_interface.h"

#include <Arduino.h>
#include <WiFiClient.h>

#if defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#elif defined(ARDUINO_UNOR4_WIFI)
#include <WiFiS3.h>
#else
#error "Unsupported board for web interface"
#endif

#include <cstring>

#include "hardware_control.h"
#include "wifi_config.h"

namespace
{
    WiFiServer httpServer(80);

    String formatElapsed(unsigned long ms)
    {
        unsigned long totalSeconds = ms / 1000UL;
        unsigned int seconds = totalSeconds % 60UL;
        unsigned int minutes = (totalSeconds / 60UL) % 60UL;
        unsigned int hours = (totalSeconds / 3600UL) % 24UL; // drop %24 if you want >24h

        String out;
        out.reserve(8); // "HH:MM:SS"
        if (hours < 10U) out += '0';
        out += String(hours);
        out += ':';
        if (minutes < 10U) out += '0';
        out += String(minutes);
        out += ':';
        if (seconds < 10U) out += '0';
        out += String(seconds);
        return out;
    }

    const char INDEX_HTML_TEMPLATE[] = R"rawliteral(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>AngryESP Standalone Controller (%CONTROL_BOARD%)</title>
<style>
body { font-family: Arial, sans-serif; background: #1c1c1c; color: #f0f0f0; margin: 0; padding: 2rem; }
h1 { margin-top: 0; }
section { background: #2a2a2a; border-radius: 8px; padding: 1.5rem; margin-bottom: 1.5rem; box-shadow: 0 2px 6px rgba(0,0,0,0.4); }
button { background: #ff6a00; color: #fff; border: none; border-radius: 4px; padding: 0.75rem 1rem; margin-right: 0.5rem; cursor: pointer; font-size: 1rem; }
button.secondary { background: #444; }
button.danger { background: #d32f2f; }
button:disabled { opacity: 0.5; cursor: not-allowed; }
#status { margin-top: 0.5rem; font-size: 0.9rem; color: #bbb; }
.note { font-size: 0.85rem; color: #bbb; margin: 0 0 1rem; }
.flex { display: flex; align-items: center; gap: 1rem; flex-wrap: wrap; }
.card-title { font-size: 1.2rem; margin-bottom: 0.75rem; }
#ledState { font-size: 2.4rem; font-weight: 700; letter-spacing: 0.05em; display: inline-block; min-width: 6ch; text-align: center; padding: 0.1rem 0.4rem; border-radius: 4px; }
.state-on { color: #1b5e20; background: linear-gradient(135deg, #80e27e, #43a047); box-shadow: 0 0 12px rgba(102, 187, 106, 0.4); }
.state-off { color: #b71c1c; background: linear-gradient(135deg, #ff867c, #e53935); box-shadow: 0 0 12px rgba(239, 83, 80, 0.35); }
.state-unknown { color: #ffeb3b; background: linear-gradient(135deg, rgba(255, 235, 59, 0.4), rgba(255, 213, 79, 0.6)); box-shadow: 0 0 12px rgba(255, 235, 59, 0.3); }
.icon-button { width: 1.75rem; height: 1.75rem; padding: 0; display: inline-flex; align-items: center; justify-content: center; font-size: 1.2rem; font-weight: 600; }
.icon-button span{ display:inline-block; transform-origin: 54% 56%; transform: rotate(90deg); }
.console {
  background: #141414;
  border: 1px solid #333;
  border-radius: 4px;
  font-family: "Courier New", monospace;
  font-size: 0.85rem;
  height: 180px;
  overflow-y: auto;
  padding: 0.75rem;
  box-sizing: border-box;
}
.console-entry { margin-bottom: 0.35rem; }
.console-entry:last-child { margin-bottom: 0; }
</style>
<script>
let ledRefreshTimer = null;
let ledAutoRefreshInterval = null;
const CONSOLE_MAX_ENTRIES = 200;
const INITIAL_LED = %INITIAL_LED%;
const LED_AUTO_REFRESH_MS = 5000;
const REBOOT_MONITOR_INTERVAL_MS = 3000;
const REBOOT_MONITOR_TIMEOUT_MS = 1500;
let lastLedLogLabel = null;
let lastLedLogStatus = null;
let rebootMonitorTimer = null;
let rebootMonitorKickoff = null;

function logEvent(message) {
  const consoleEl = document.getElementById('consoleLog');
  if (!consoleEl) {
    return;
  }
  const now = new Date();
  const timestamp = now.toLocaleTimeString([], { hour12: false });
  const entry = document.createElement('div');
  entry.className = 'console-entry';
  entry.textContent = '[' + timestamp + '] ' + message;
  consoleEl.appendChild(entry);
  while (consoleEl.children.length > CONSOLE_MAX_ENTRIES) {
    consoleEl.removeChild(consoleEl.firstChild);
  }
  consoleEl.scrollTop = consoleEl.scrollHeight;
}

function updateStatus(message) {
  const statusEl = document.getElementById('status');
  if (!statusEl) {
    return;
  }
  statusEl.textContent = message;
}

function clearScheduledLedRefresh() {
  if (ledRefreshTimer !== null) {
    clearTimeout(ledRefreshTimer);
    ledRefreshTimer = null;
  }
}

function scheduleLedRefresh() {
  clearScheduledLedRefresh();
  ledRefreshTimer = setTimeout(() => {
    ledRefreshTimer = null;
    refreshLed(true);
  }, 1500);
}

function ensureAutoLedRefresh() {
  if (ledAutoRefreshInterval === null) {
    ledAutoRefreshInterval = setInterval(() => {
      refreshLed(true);
    }, LED_AUTO_REFRESH_MS);
  }
}

function describeActionPath(path) {
  switch (path) {
    case '/api/power/on':
      return {
        label: 'Power On',
        inProgress: 'Power On requested; sending a short pulse to the power header...'
      };
    case '/api/power/off':
      return {
        label: 'Power Off',
        inProgress: 'Power Off requested; holding the power header for 6 seconds...'
      };
    case '/api/power/reset':
      return {
        label: 'Reboot',
        inProgress: 'Reboot requested; shorting the reset header...'
      };
    case '/api/system/reboot':
      return {
        label: 'Controller Reboot',
        inProgress: 'Controller reboot requested; device will reset in about 1 second...'
      };
    default:
      return null;
  }
}

function triggerAction(path) {
  const info = describeActionPath(path);
  const requestMessage = info ? info.inProgress : 'Requesting ' + path + ' ...';
  updateStatus(requestMessage);
  logEvent(requestMessage);
  fetch(path, { method: 'POST' })
    .then(response => response.json())
    .then(data => {
      const statusLabel = data.status ? data.status.toUpperCase() : 'INFO';
      const actionLabel = data.action || (info ? info.label : path);
      const details = [];
      const isControllerReboot = actionLabel === 'Controller Reboot' || info?.label === 'Controller Reboot';
      if (data.message && !isControllerReboot) {
        details.push(data.message);
      }
      let rebootSeconds = null;
      let rebootDelayMs = null;
      if (data.reboot && data.reboot.scheduled) {
        if (typeof data.reboot.remaining_ms === 'number') {
          rebootSeconds = Math.max(0, data.reboot.remaining_ms) / 1000;
          rebootDelayMs = Math.max(0, data.reboot.remaining_ms);
        } else if (typeof data.reboot.delay_ms === 'number') {
          rebootSeconds = Math.max(0, data.reboot.delay_ms) / 1000;
          rebootDelayMs = Math.max(0, data.reboot.delay_ms);
        }
        startRebootMonitor(rebootDelayMs);
      }
      if (isControllerReboot) {
        if (rebootSeconds !== null) {
          const rounded = Math.max(1, Math.round(rebootSeconds));
          details.push('Waiting for controller to return (~' + rounded + 's)');
        } else if (data.message) {
          details.push(data.message);
        }
      }
      if (data.power_state) {
        const stateLabel = data.power_state.state ? data.power_state.state.toUpperCase() : 'UNKNOWN';
        let powerLine = 'Power state: ' + stateLabel;
        if (data.power_state.last_action) {
          powerLine += ' ( last action: ' + data.power_state.last_action + ')';
        }
        details.push(powerLine);
      }
      const verb = isControllerReboot ? 'scheduled' : 'complete';
      const base = actionLabel + ' ' + verb + ' (' + statusLabel + ')';
      const message = details.length ? base + ': ' + details.join('; ') : base;
      updateStatus(message);
      logEvent(message);
      scheduleLedRefresh();
    })
    .catch((err) => {
      const failure = info ? `${info.label} request failed` : 'Action request failed';
      const detail = `path=${path}, online=${navigator.onLine}, error=${err?.message || err}`;
      console.error('[action-error]', err);
      updateStatus(`${failure}. ${detail}`);
      logEvent(`${failure}. ${detail}`);
      scheduleLedRefresh();
    });
}

function stopRebootMonitor(success) {
  if (rebootMonitorTimer !== null) {
    clearInterval(rebootMonitorTimer);
    rebootMonitorTimer = null;
  }
  if (rebootMonitorKickoff !== null) {
    clearTimeout(rebootMonitorKickoff);
    rebootMonitorKickoff = null;
  }
  if (success) {
    updateStatus('Controller back online.');
    logEvent('Controller back online.');
    refreshLed();
  }
}

function pollControllerHealth() {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), REBOOT_MONITOR_TIMEOUT_MS);
  fetch('/api/health', { signal: controller.signal, cache: 'no-store' })
    .then(response => {
      clearTimeout(timeout);
      if (response.ok) {
        stopRebootMonitor(true);
      }
    })
    .catch(() => {
      clearTimeout(timeout);
    });
}

function startRebootMonitor(expectedDelayMs) {
  if (rebootMonitorTimer !== null || rebootMonitorKickoff !== null) {
    return;
  }
  logEvent('Waiting for controller to come back online...');
  rebootMonitorTimer = setInterval(pollControllerHealth, REBOOT_MONITOR_INTERVAL_MS);
  const firstDelay = expectedDelayMs != null ? Math.max(1200, expectedDelayMs + 200) : 1500;
  rebootMonitorKickoff = setTimeout(() => {
    rebootMonitorKickoff = null;
    pollControllerHealth();
  }, firstDelay);
}

function applyLedPayload(data, silent) {
  const suppress = !!silent;
  let label = 'Unknown';
  if (data.state === 'on') {
    label = 'ON';
  } else if (data.state === 'off') {
    label = 'OFF';
  }

  const ledStateEl = document.getElementById('ledState');
  if (ledStateEl) {
    ledStateEl.textContent = label;
    ledStateEl.classList.remove('state-on', 'state-off', 'state-unknown');
    if (label === 'ON') {
      ledStateEl.classList.add('state-on');
    } else if (label === 'OFF') {
      ledStateEl.classList.add('state-off');
    } else {
      ledStateEl.classList.add('state-unknown');
    }
  }

  const statusLabel = data.status ? data.status.toUpperCase() : 'INFO';
  const extra = data.last_change ? ' (last observed change: ' + data.last_change + ' ago)' : '';
  const logLine = 'LED ' + label + extra;
  const shouldLog = !suppress || lastLedLogLabel === null || label !== lastLedLogLabel || lastLedLogStatus !== statusLabel;

  if (!suppress) {
    const message = statusLabel + ': LED state ' + label + extra;
    updateStatus(message);
  }

  if (shouldLog) {
    logEvent(logLine);
  }

  lastLedLogLabel = label;
  lastLedLogStatus = statusLabel;
}

function refreshLed(silent) {
  const suppress = !!silent;
  if (!suppress) {
    const message = 'Refreshing power LED...';
    updateStatus(message);
    logEvent(message);
  }
  fetch('/api/power/led')
    .then(response => response.json())
    .then(data => {
      applyLedPayload(data, suppress);
    })
    .catch(() => {
      if (suppress) {
        logEvent('LED refresh failed');
      } else {
        const message = 'LED refresh failed';
        updateStatus(message);
        logEvent(message);
      }
    });
}

window.addEventListener('load', () => {
  logEvent('Console ready');
  applyLedPayload(INITIAL_LED, false);
  ensureAutoLedRefresh();
});
</script>
</head>
<body>
<h1>AngryESP Standalone Control (%CONTROL_BOARD%)</h1>
<p>HTTP API is available under <code>/api/</code>.</p>
<section>
  <div class="card-title">Power Management</div>
  <div class="flex">
    <button onclick="triggerAction('/api/power/on')">Power On</button>
    <button onclick="triggerAction('/api/power/off')">Power Off</button>
    <button onclick="triggerAction('/api/power/reset')">Reboot</button>
  </div>
  <div id="status">Idle</div>
</section>
<section>
  <div class="card-title">Power LED</div>
  <div class="flex">
    <strong id="ledState" class="state-unknown">Unknown</strong>
    <button class="secondary icon-button" onclick="refreshLed()" aria-label="Refresh LED"><span>&#x21bb;</span></button>
  </div>
</section>
<section>
  <div class="card-title">Event Log</div>
  <div id="consoleLog" class="console"></div>
</section>
<section>
  <div class="card-title">Controller Maintenance</div>
  <p class="note">Soft reboot the microcontroller itself. Network connectivity will drop for a few seconds.</p>
  <div class="flex">
    <button class="danger" onclick="triggerAction('/api/system/reboot')">Reboot Controller</button>
  </div>
</section>
</body>
</html>
)rawliteral";

    const char* httpStatusText(int code)
    {
        switch (code)
        {
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 204:
            return "No Content";
        case 400:
            return "Bad Request";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 500:
            return "Internal Server Error";
        default:
            return "OK";
        }
    }

    void sendHttpResponse(WiFiClient& client, int statusCode, const char* contentType, const String& body,
                          const char* extraHeaders = nullptr)
    {
        client.print("HTTP/1.1 ");
        client.print(statusCode);
        client.print(' ');
        client.println(httpStatusText(statusCode));
        client.print("Content-Type: ");
        client.println(contentType);
        client.print("Content-Length: ");
        client.println(body.length());
        client.println("Connection: close");
        if (extraHeaders != nullptr)
        {
            size_t headerLen = strlen(extraHeaders);
            if (headerLen > 0)
            {
                client.print(extraHeaders);
                if (extraHeaders[headerLen - 1] != '\n')
                {
                    client.println();
                }
            }
        }
        client.println();
        client.print(body);
    }

    void sendJsonResponse(WiFiClient& client, int statusCode, const String& payload, const char* extraHeaders = nullptr)
    {
        sendHttpResponse(client, statusCode, "application/json", payload, extraHeaders);
    }

    void sendHtmlResponse(WiFiClient& client, const String& html)
    {
        sendHttpResponse(client, 200, "text/html; charset=utf-8", html);
    }

    void sendJsonError(WiFiClient& client, int statusCode, const char* errorKey)
    {
        String payload = "{\"error\":\"";
        payload += errorKey;
        payload += "\"}";
        sendJsonResponse(client, statusCode, payload);
    }

    String buildPowerStateJson(const PowerControllerSnapshot& snapshot)
    {
        String payload;
        payload += "\"state\":\"";
        payload += powerStateToText(snapshot.logicalState);
        payload += "\"";
        if (snapshot.logicalState != PowerLogicalState::Unknown && snapshot.stateSinceMs != 0)
        {
            payload += ",\"since\":\"";
            payload += formatElapsed(millis() - snapshot.stateSinceMs);
            payload += "\"";
        }
        if (snapshot.lastActionLabel != nullptr && snapshot.lastActionAtMs != 0)
        {
            payload += ",\"last_action\":\"";
            payload += snapshot.lastActionLabel;
            payload += "\",\"last_action_age\":\"";
            payload += formatElapsed(millis() - snapshot.lastActionAtMs);
            payload += "\"";
        }
        return payload;
    }

    String buildLedStatusJson(bool markPublish)
    {
        LedTrackerSnapshot snapshot = getLedTrackerSnapshot();
        PowerControllerSnapshot controller = getPowerControllerSnapshot();
        String payload = "{";

        if (!snapshot.hasStable)
        {
            payload += "\"status\":\"unknown\",\"state\":\"unknown\"";
        }
        else
        {
            if (!snapshot.hasPublished && markPublish)
            {
                markLedStatusPublished();
                snapshot.hasPublished = true;
            }
            payload += "\"status\":\"ok\",\"state\":\"";
            payload += ledLevelToStateText(snapshot.stableLevel);
            payload += "\",\"last_change\":\"";
            payload += formatElapsed(millis() - snapshot.lastStableChange);
            payload += "\"";
        }

        payload += ",\"power\":{";
        payload += buildPowerStateJson(controller);
        payload += "}}";

        return payload;
    }

    String buildIndexHtml(const String& initialLedJson)
    {
        String html = INDEX_HTML_TEMPLATE;
#if defined(ARDUINO_ARCH_ESP32)
        html.replace("%CONTROL_BOARD%", "ESP32");
#elif defined(ARDUINO_UNOR4_WIFI)
        html.replace("%CONTROL_BOARD%", "Uno R4 WiFi");
#else
        html.replace("%CONTROL_BOARD%", "Unknown Board");
#endif
        html.replace("%INITIAL_LED%", initialLedJson);
        return html;
    }

    void sendActionResponse(WiFiClient& client, const ActionFeedback& feedback)
    {
        PowerControllerSnapshot controller = getPowerControllerSnapshot();

        String payload = "{\"status\":\"";
        payload += feedback.success ? "ok" : "error";
        payload += "\",\"action\":\"";
        payload += feedback.actionLabel;
        payload += "\"";
        payload += ",\"power_state\":{";
        payload += buildPowerStateJson(controller);
        payload += "}}";

        sendJsonResponse(client, 200, payload);
    }

    void handlePowerOn(WiFiClient& client)
    {
        ActionFeedback feedback = performPowerAction(PowerAction::On);
        sendActionResponse(client, feedback);
    }

    void handlePowerOff(WiFiClient& client)
    {
        ActionFeedback feedback = performPowerAction(PowerAction::Off);
        sendActionResponse(client, feedback);
    }

    void handlePowerReset(WiFiClient& client)
    {
        ActionFeedback feedback = performPowerAction(PowerAction::Reset);
        sendActionResponse(client, feedback);
    }

    void handleControllerReboot(WiFiClient& client)
    {
        constexpr unsigned long REBOOT_DELAY_MS = 1000UL;
        bool scheduled = scheduleControllerReboot(REBOOT_DELAY_MS);
        bool pending = isControllerRebootPending();
        unsigned long remainingMs = getControllerRebootRemainingMs();
        unsigned long originalDelayMs = getControllerRebootDelayMs();
        PowerControllerSnapshot controller = getPowerControllerSnapshot();

        String message;
        if (scheduled)
        {
            message = "Controller reboot scheduled; device will reset in about 1 second.";
        }
        else if (pending)
        {
            if (remainingMs > 0)
            {
                unsigned long roundedSeconds = (remainingMs + 500UL) / 1000UL;
                if (roundedSeconds == 0UL)
                {
                    roundedSeconds = 1UL;
                }
                message = "Controller reboot already pending; about ";
                message += String(roundedSeconds);
                message += (roundedSeconds == 1UL) ? " second remaining." : " seconds remaining.";
            }
            else
            {
                message = "Controller reboot already pending; reset is imminent.";
            }
        }
        else
        {
            message = "Controller reboot unavailable.";
        }

        String payload = "{\"status\":\"";
        payload += scheduled ? "ok" : (pending ? "pending" : "error");
        payload += "\",\"action\":\"Controller Reboot\"";
        payload += ",\"message\":\"";
        payload += message;
        payload += "\"";
        payload += ",\"power_state\":{";
        payload += buildPowerStateJson(controller);
        payload += "}";
        payload += ",\"reboot\":{\"scheduled\":";
        payload += pending ? "true" : "false";
        if (pending)
        {
            payload += ",\"original_delay_ms\":";
            payload += originalDelayMs;
        }
        if (scheduled)
        {
            payload += ",\"delay_ms\":";
            payload += REBOOT_DELAY_MS;
        }
        else if (pending)
        {
            payload += ",\"remaining_ms\":";
            payload += remainingMs;
        }
        payload += "}}";

        int statusCode = scheduled ? 202 : (pending ? 200 : 503);
        sendJsonResponse(client, statusCode, payload);
    }

    void handleLedStatus(WiFiClient& client)
    {
        String payload = buildLedStatusJson(true);
        sendJsonResponse(client, 200, payload);
    }

    void handleHealth(WiFiClient& client)
    {
        String payload = "{\"wifi\":{\"ssid\":\"";
        payload += WIFI_SSID;
        payload += "\",\"ip\":\"";
        payload += WiFi.localIP().toString();
        payload += "\"}}";
        sendJsonResponse(client, 200, payload);
    }

    void handleRoot(WiFiClient& client)
    {
        String initialLed = buildLedStatusJson(true);
        String html = buildIndexHtml(initialLed);
        sendHtmlResponse(client, html);
    }

    void handleNotFound(WiFiClient& client)
    {
        sendJsonError(client, 404, "not_found");
    }

    bool parseRequestLine(const String& line, String& method, String& path)
    {
        int firstSpace = line.indexOf(' ');
        if (firstSpace <= 0)
        {
            return false;
        }
        int secondSpace = line.indexOf(' ', firstSpace + 1);
        if (secondSpace <= firstSpace)
        {
            return false;
        }
        method = line.substring(0, firstSpace);
        path = line.substring(firstSpace + 1, secondSpace);
        return method.length() > 0 && path.length() > 0;
    }

    void discardRequestBody(WiFiClient& client, size_t length)
    {
        while (length > 0 && client.connected())
        {
            if (client.available())
            {
                client.read();
                length--;
            }
            else
            {
                delay(1);
            }
        }
    }

    void sendMethodNotAllowed(WiFiClient& client)
    {
        sendJsonError(client, 405, "method_not_allowed");
    }

    void dispatchRequest(WiFiClient& client, const String& method, const String& path)
    {
        String cleanPath = path;
        int queryIndex = cleanPath.indexOf('?');
        if (queryIndex >= 0)
        {
            cleanPath = cleanPath.substring(0, queryIndex);
        }

        if (cleanPath == "/")
        {
            if (method == "GET")
            {
                handleRoot(client);
            }
            else
            {
                sendMethodNotAllowed(client);
            }
            return;
        }

        if (cleanPath == "/api/power/on")
        {
            if (method == "POST")
            {
                handlePowerOn(client);
            }
            else
            {
                sendMethodNotAllowed(client);
            }
            return;
        }

        if (cleanPath == "/api/power/off")
        {
            if (method == "POST")
            {
                handlePowerOff(client);
            }
            else
            {
                sendMethodNotAllowed(client);
            }
            return;
        }

        if (cleanPath == "/api/power/reset")
        {
            if (method == "POST")
            {
                handlePowerReset(client);
            }
            else
            {
                sendMethodNotAllowed(client);
            }
            return;
        }

        if (cleanPath == "/api/power/led")
        {
            if (method == "GET")
            {
                handleLedStatus(client);
            }
            else
            {
                sendMethodNotAllowed(client);
            }
            return;
        }

        if (cleanPath == "/api/system/reboot")
        {
            if (method == "POST")
            {
                handleControllerReboot(client);
            }
            else
            {
                sendMethodNotAllowed(client);
            }
            return;
        }

        if (cleanPath == "/api/health")
        {
            if (method == "GET")
            {
                handleHealth(client);
            }
            else
            {
                sendMethodNotAllowed(client);
            }
            return;
        }

        handleNotFound(client);
    }

    void handleHttpClient(WiFiClient& client)
    {
        client.setTimeout(2000);
        String requestLine = client.readStringUntil('\n');
        requestLine.trim();
        if (requestLine.length() == 0)
        {
            return;
        }

        String method;
        String path;
        if (!parseRequestLine(requestLine, method, path))
        {
            sendJsonError(client, 400, "bad_request");
            return;
        }

        method.toUpperCase();

        size_t contentLength = 0;
        while (client.connected())
        {
            String headerLine = client.readStringUntil('\n');
            headerLine.trim();
            if (headerLine.length() == 0)
            {
                break;
            }
            String headerLower = headerLine;
            headerLower.toLowerCase();
            if (headerLower.startsWith("content-length"))
            {
                int colonIndex = headerLine.indexOf(':');
                if (colonIndex > 0)
                {
                    String value = headerLine.substring(colonIndex + 1);
                    value.trim();
                    contentLength = value.toInt();
                }
            }
        }

        if (contentLength > 0)
        {
            discardRequestBody(client, contentLength);
        }

        dispatchRequest(client, method, path);
    }

    void connectWifi()
    {
#if defined(ARDUINO_ARCH_ESP32)
        WiFi.mode(WIFI_STA);
#endif

        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        unsigned long start = millis();
        Serial.print("Connecting to WiFi...");

        // Wait until connected AND IP assigned
        while ((WiFi.status() != WL_CONNECTED || WiFi.localIP() == INADDR_NONE) && millis() - start <
            WIFI_CONNECT_TIMEOUT_MS)
        {
            Serial.print('.');
            delay(WIFI_RETRY_INTERVAL_MS);
        }

        if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != INADDR_NONE)
        {
            Serial.println(" Connected!");
        }
        else
        {
            Serial.println(" WiFi connection timed out");
            // Halt here; no point in continuing without network
            while (true)
            {
                delay(1000);
            }
        }
    }
} // namespace

void webSetup()
{
    connectWifi();

    byte m[6]; WiFi.macAddress(m);
    Serial.print("MAC Address: ");
    for (int i = 0; i < 6; i++) {
      if (m[i] < 0x10) Serial.print('0');
      Serial.print(m[i], HEX);
      if (i < 5) Serial.print(':');
    }
    Serial.println();

    httpServer.begin();
    Serial.println("HTTP server started");
    Serial.print("Web Server IP address: http://");
    Serial.println(WiFi.localIP());
}

void webLoop()
{
    WiFiClient client = httpServer.available();
    if (client)
    {
        handleHttpClient(client);
        client.flush();
        client.stop();
    }
}
