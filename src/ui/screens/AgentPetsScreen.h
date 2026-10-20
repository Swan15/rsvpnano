#pragma once

#include "agent_pets/AgentPets.h"
#include "ui/screens/Screens.h"

namespace screens {

class AgentPetsScreen {
public:
  bool due(const ui::Context& ui, uint32_t nowMs) const {
    return static_cast<int32_t>(nowMs - nextFrameMs_) >= 0 || ui.touch() != nullptr;
  }
  void reset() { page_ = 0; nextFrameMs_ = 0; }
  void draw(ui::Context& ui, const agentpets::View& view, uint32_t nowMs, Screen& screen);

private:
  size_t page_ = 0;
  size_t lastCount_ = 0;
  uint32_t nextFrameMs_ = 0;
};

} // namespace screens
