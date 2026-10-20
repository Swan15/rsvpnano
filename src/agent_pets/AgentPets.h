#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace agentpets {

constexpr size_t kMaxAgents = 8;
constexpr size_t kMaxSnapshotBytes = 8192;
constexpr uint32_t kStaleMs = 15000;

struct Agent {
  std::string id;
  std::string name;
  std::string project;
  std::string state;
  std::string detail;
};

struct Snapshot {
  int version = 0;
  std::string source;
  std::string updatedAt;
  std::vector<Agent> agents;
};

struct Config {
  std::string url;
  std::string token;
  std::string caFile = "/config/agent-ca.pem";
};

struct View {
  Snapshot snapshot;
  std::string message;
  bool live = false;
};

bool validConfig(const Config& config);
bool decodeConfig(std::string_view text, Config& result);
bool knownState(std::string_view state);

class Model {
public:
  bool accept(std::string_view json, uint32_t nowMs);
  bool fresh(uint32_t nowMs) const;
  const Snapshot& snapshot() const { return snapshot_; }

private:
  Snapshot snapshot_;
  uint32_t lastChangedMs_ = 0;
  bool received_ = false;
};

} // namespace agentpets
