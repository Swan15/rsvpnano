#pragma once

#include <Arduino.h>
#include <FS.h>
#include <atomic>
#include <mutex>

#include "agent_pets/AgentPets.h"

namespace agentpets {

class Client {
public:
  bool begin(fs::FS& filesystem, std::string ssid, std::string password);
  void end();
  bool active() const { return running_.load(); }
  View view(uint32_t nowMs);

private:
  static void taskEntry(void* context);
  void run();
  void message(std::string text);
  bool poll();
  std::mutex mutex_;
  Model model_;
  std::string message_ = "Copy /config/agents.toml to the SD card";
  Config config_;
  std::string ca_;
  std::string ssid_;
  std::string password_;
  bool transportOkay_ = false;
  std::atomic<bool> enabled_{false};
  std::atomic<bool> running_{false};
};

} // namespace agentpets
