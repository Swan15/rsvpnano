#include "agent_pets/AgentPets.h"

#include <algorithm>
#include <glaze/json.hpp>
#include <glaze/toml.hpp>

namespace agentpets {
namespace {

bool boundedText(std::string_view text, size_t maximum) {
  return text.size() <= maximum && std::ranges::none_of(text, [](unsigned char c) {
    return c < 0x20 || c == 0x7f;
  });
}

} // namespace

bool knownState(std::string_view state) {
  return state == "working" || state == "waiting" || state == "done" || state == "error" || state == "idle";
}

bool validConfig(const Config& config) {
  return config.url.starts_with("https://") && config.url.size() > 8 && config.url.size() <= 256
      && config.url.find_first_of("\r\n\t @#") == std::string::npos
      && config.token.size() >= 32 && config.token.size() <= 128
      && std::ranges::all_of(config.token, [](unsigned char c) {
           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
         })
      && config.caFile.starts_with("/config/") && config.caFile.find("..") == std::string::npos
      && boundedText(config.caFile, 96);
}

bool decodeConfig(std::string_view text, Config& result) {
  if (text.empty() || text.size() > 4096)
    return false;
  Config parsed;
  if (glz::read<glz::opts{.format = glz::TOML, .error_on_unknown_keys = false}>(parsed, text)
      || !validConfig(parsed))
    return false;
  result = std::move(parsed);
  return true;
}

bool Model::accept(std::string_view json, uint32_t nowMs) {
  if (json.empty() || json.size() > kMaxSnapshotBytes)
    return false;
  Snapshot parsed;
  if (glz::read<glz::opts{.error_on_unknown_keys = false}>(parsed, json) || parsed.version != 1
      || !boundedText(parsed.source, 128) || parsed.updatedAt.empty() || !boundedText(parsed.updatedAt, 40)
      || parsed.agents.size() > kMaxAgents)
    return false;
  for (size_t index = 0; index < parsed.agents.size(); ++index) {
    const auto& agent = parsed.agents[index];
    if (agent.id.empty() || agent.name.empty() || !knownState(agent.state)
        || !boundedText(agent.id, 64) || !boundedText(agent.name, 240)
        || !boundedText(agent.project, 160) || !boundedText(agent.detail, 160))
      return false;
    for (size_t prior = 0; prior < index; ++prior)
      if (parsed.agents[prior].id == agent.id)
        return false;
  }
  if (!received_ || parsed.updatedAt != snapshot_.updatedAt)
    lastChangedMs_ = nowMs;
  snapshot_ = std::move(parsed);
  received_ = true;
  return true;
}

bool Model::fresh(uint32_t nowMs) const {
  return received_ && static_cast<uint32_t>(nowMs - lastChangedMs_) < kStaleMs;
}

} // namespace agentpets
