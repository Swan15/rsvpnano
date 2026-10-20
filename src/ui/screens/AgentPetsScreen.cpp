#include "ui/screens/AgentPetsScreen.h"

#include <algorithm>

namespace screens {
namespace {

uint16_t stateColor(std::string_view state) {
  using ui::themes::rgb565;
  if (state == "working") return rgb565(118, 228, 165);
  if (state == "waiting") return rgb565(255, 208, 121);
  if (state == "done") return rgb565(129, 182, 255);
  if (state == "error") return rgb565(255, 118, 128);
  return rgb565(187, 167, 237);
}

void pet(ui::Context& ui, ui::Rect bounds, const agentpets::Agent& agent, bool live, uint32_t nowMs) {
  const uint16_t color = live ? stateColor(agent.state) : ui.color(ui::themes::Muted);
  const uint16_t background = ui.color(ui::themes::Background);
  ui.paint(bounds, [&](Arduino_GFX& gfx, ui::Rect area) {
    gfx.fillRect(area.x, area.y, area.w, area.h, background);
    const int scale = std::clamp<int>(std::min(area.w / 24, area.h / 18), 1, 4);
    const int phase = live ? (nowMs / 180) % 2 : 0;
    const int bob = live && (agent.state == "working" || agent.state == "done") ? phase : 0;
    const int x = area.x + (area.w - 22 * scale) / 2;
    const int y = area.y + (area.h - 16 * scale) / 2 + bob;
    const auto block = [&](int px, int py, int w, int h, uint16_t ink) {
      gfx.fillRect(x + px * scale, y + py * scale, w * scale, h * scale, ink);
    };
    block(5, 5, 11, 8, color);
    block(5, 2, 3, 4, color);
    block(13, 2, 3, 4, color);
    block(4, 12, 4, 2, color);
    block(14, 12, 4, 2, color);
    block(16, 10, 5, 2, color);
    block(19, 7, 2, 4, color);
    block(7, 8, 1, agent.state == "idle" || !live ? 1 : 2, background);
    block(12, 8, 1, agent.state == "idle" || !live ? 1 : 2, background);
    block(10, 11, 1, 1, background);
    if (live && agent.state == "working") {
      block(3, 15, 15, 1, ui.color(ui::themes::Subtle));
      block(6 + phase * 6, 14, 2, 1, color);
    } else if (live && (agent.state == "waiting" || agent.state == "error")) {
      block(19, 1, 1, 3, color);
      block(19, 5, 1, 1, color);
    } else if (live && agent.state == "done") {
      block(1, 3 + phase, 1, 3, color);
      block(0, 4 + phase, 3, 1, color);
    }
  });
}

} // namespace

void AgentPetsScreen::draw(ui::Context& ui, const agentpets::View& view, uint32_t nowMs, Screen& screen) {
  nextFrameMs_ = nowMs + 160;
  if (lastCount_ != view.snapshot.agents.size()) {
    ui.invalidate();
    lastCount_ = view.snapshot.agents.size();
  }
  ui.beginFrame(static_cast<uint8_t>(Screen::AgentPets));
  const int16_t width = ui.width();
  if (ui.button({4, 2, 56, 26}, "Back")) screen = Screen::Device;
  ui.label({66, 3, static_cast<int16_t>(std::max(0, width - 212)), 24}, "T3 PETS", 2);
  ui.label({static_cast<int16_t>(std::max(0, width - 145)), 3, 74, 24},
           view.live ? "LIVE" : "OFFLINE", 1,
           view.live ? ui::themes::Accent : ui::themes::Muted, ui::TextAlign::Center);
  const size_t columns = std::clamp<size_t>(width / 145, 1, 4);
  const size_t rows = std::clamp<size_t>((ui.height() - 34) / 126, 1, 2);
  const size_t perPage = columns * rows;
  const size_t pages = std::max<size_t>(1, (view.snapshot.agents.size() + perPage - 1) / perPage);
  page_ = std::min(page_, pages - 1);
  const std::string page = std::to_string(page_ + 1) + "/" + std::to_string(pages);
  if (ui.button({static_cast<int16_t>(width - 66), 2, 62, 26}, page, pages > 1)) {
    page_ = (page_ + 1) % pages;
    nextFrameMs_ = nowMs;
    ui.invalidate();
    ui.endFrame();
    return;
  }
  if (view.snapshot.agents.empty()) {
    ui.label({8, 42, static_cast<int16_t>(width - 16), 40},
             view.live ? "No active T3 agents" : view.message, 2, ui::themes::Foreground,
             ui::TextAlign::Center, 2);
    ui.label({8, 92, static_cast<int16_t>(width - 16), 40},
             view.live ? view.snapshot.source : "Wi-Fi + /config/agents.toml", 1,
             ui::themes::Muted, ui::TextAlign::Center, 2);
  } else {
    const int16_t cellWidth = width / columns;
    const int16_t cellHeight = (ui.height() - 34) / rows;
    const size_t first = page_ * perPage;
    for (size_t i = 0; i < perPage && first + i < view.snapshot.agents.size(); ++i) {
      const auto& agent = view.snapshot.agents[first + i];
      const int16_t x = (i % columns) * cellWidth + 4;
      const int16_t y = 32 + (i / columns) * cellHeight;
      const int16_t w = cellWidth - 8;
      ui.label({x, y, w, 26}, agent.name, 1, ui::themes::Foreground, ui::TextAlign::Center, 2);
      const int16_t spriteHeight = std::max<int16_t>(20, cellHeight - 68);
      pet(ui, {x, static_cast<int16_t>(y + 26), w, spriteHeight}, agent, view.live, nowMs);
      ui.fixedText({x, static_cast<int16_t>(y + cellHeight - 38), w, 16},
                   view.live ? agent.detail : "LAST KNOWN UPDATE", 1,
                   view.live ? stateColor(agent.state) : ui.color(ui::themes::Muted));
      ui.label({x, static_cast<int16_t>(y + cellHeight - 20), w, 16}, agent.project, 1,
               ui::themes::Muted, ui::TextAlign::Center);
    }
  }
  ui.endFrame();
}

} // namespace screens
