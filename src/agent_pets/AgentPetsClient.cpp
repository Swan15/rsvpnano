#include "agent_pets/AgentPetsClient.h"

#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <time.h>

namespace agentpets {
namespace {

std::string readFile(fs::FS& fs, const char* path, size_t maximum) {
  File file = fs.open(path);
  if (!file || file.isDirectory() || file.size() == 0 || file.size() > maximum)
    return {};
  std::string data(file.size(), '\0');
  if (file.readBytes(data.data(), data.size()) != data.size())
    return {};
  return data;
}

} // namespace

bool Client::begin(fs::FS& filesystem, std::string ssid, std::string password) {
  end();
  {
    std::lock_guard lock(mutex_);
    model_ = {};
    transportOkay_ = false;
  }
  if (!decodeConfig(readFile(filesystem, "/config/agents.toml", 4096), config_)) {
    message("Missing or invalid /config/agents.toml");
    return false;
  }
  ca_ = readFile(filesystem, config_.caFile.c_str(), 8192);
  if (ca_.find("-----BEGIN CERTIFICATE-----") == std::string::npos) {
    message("Missing trusted CA certificate on SD card");
    return false;
  }
  if (ssid.empty()) {
    message("Save home Wi-Fi in Settings first");
    return false;
  }
  ssid_ = std::move(ssid);
  password_ = std::move(password);
  message("Connecting to home Wi-Fi");
  enabled_ = true;
  running_ = true;
  if (xTaskCreate(taskEntry, "agent-pets", 16384, this, 1, nullptr) != pdPASS) {
    enabled_ = false;
    running_ = false;
    message("Could not start agent monitor");
    return false;
  }
  return true;
}

void Client::end() {
  enabled_ = false;
  // Join before another feature can reuse Wi-Fi. HTTP and TLS have bounded timeouts.
  while (running_.load())
    delay(20);
  std::lock_guard lock(mutex_);
  transportOkay_ = false;
}

View Client::view(uint32_t nowMs) {
  std::lock_guard lock(mutex_);
  const bool live = running_.load() && transportOkay_ && model_.fresh(nowMs);
  std::string status = message_;
  if (transportOkay_ && !live)
    status = "Feed stale - showing last known update";
  return {model_.snapshot(), std::move(status), live};
}

void Client::message(std::string text) {
  std::lock_guard lock(mutex_);
  message_ = std::move(text);
  transportOkay_ = false;
}

void Client::taskEntry(void* context) {
  auto& self = *static_cast<Client*>(context);
  self.run();
  self.running_ = false;
  vTaskDelete(nullptr);
}

void Client::run() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid_.c_str(), password_.c_str());
  uint32_t lastConnect = millis();
  bool clockStarted = false;
  uint32_t lastPoll = millis() - 2000;
  while (enabled_) {
    if (WiFi.status() != WL_CONNECTED) {
      message("Wi-Fi offline - reconnecting");
      if (millis() - lastConnect >= 15000) {
        WiFi.reconnect();
        lastConnect = millis();
      }
    } else if (time(nullptr) < 1700000000) {
      message("Syncing clock for secure connection");
      if (!clockStarted) {
        configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
        clockStarted = true;
      }
    } else if (millis() - lastPoll >= 2000) {
      poll();
      lastPoll = millis();
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  password_.clear();
}

bool Client::poll() {
  const uint32_t started = millis();
  NetworkClientSecure socket;
  socket.setCACert(ca_.c_str());
  // Certificate verification on the ESP32 can outlast a desktop TLS handshake.
  socket.setHandshakeTimeout(10);
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(socket, config_.url.c_str())) {
    message("Could not open secure feed");
    return false;
  }
  http.addHeader("Authorization", ("Bearer " + config_.token).c_str());
  const int code = http.GET();
  const int size = http.getSize();
  if (code != 200 || size < 0 || size > static_cast<int>(kMaxSnapshotBytes)) {
    ESP_LOGW("agent-pets", "Feed request failed: HTTP %d, size %d, elapsed %lu ms, RSSI %d",
             code, size, static_cast<unsigned long>(millis() - started), WiFi.RSSI());
    message(code == 401 ? "Feed token rejected"
                       : "Feed offline (HTTP " + std::to_string(code) + ")");
    http.end();
    return false;
  }
  const String body = http.getString();
  http.end();
  std::lock_guard lock(mutex_);
  transportOkay_ = body.length() == static_cast<size_t>(size)
      && model_.accept(std::string_view{body.c_str(), body.length()}, millis());
  message_ = transportOkay_ ? "Connected" : "Invalid status feed";
  return transportOkay_;
}

} // namespace agentpets
