#include <unity.h>
#include <glaze/json.hpp>

#include "agent_pets/AgentPets.h"
#include "ui/screens/AgentPetsScreen.h"
#include <array>
#include <vector>

void setUp() {}
void tearDown() {}

namespace {
class RecordingGfx : public Arduino_GFX {
public:
  using Arduino_GFX::Arduino_GFX;
  std::vector<std::array<int16_t, 4>> petRects;
  uint16_t watchedColor = 0;
  bool outside = false;
  std::string text;

  size_t write(uint8_t byte) override {
    text.push_back(static_cast<char>(byte));
    return Arduino_GFX::write(byte);
  }

  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    if (w > 0 && h > 0 && (x < 0 || y < 0 || x + w > width() || y + h > height()))
      outside = true;
    if (color == watchedColor)
      petRects.push_back({x, y, w, h});
    Arduino_GFX::fillRect(x, y, w, h, color);
  }
};

ui::TouchContact contact;
ui::TouchSampleResult sample(ui::TouchContact& output) {
  output = contact;
  return ui::TouchSampleResult::Contact;
}

std::string snapshot(std::string stamp = "2026-10-20T00:00:00Z") {
  agentpets::Snapshot value{1, "VPS", std::move(stamp),
    {{"a", "Task", "Project", "working", "Working"}}};
  std::string json;
  TEST_ASSERT_FALSE(glz::write_json(value, json));
  return json;
}
}

void test_statuses_are_bounded_and_transactional() {
  agentpets::Model model;
  TEST_ASSERT_TRUE(model.accept(snapshot(), 100));
  TEST_ASSERT_FALSE(model.accept("{broken", 200));
  TEST_ASSERT_EQUAL_STRING("Task", model.snapshot().agents[0].name.c_str());
  TEST_ASSERT_TRUE(model.fresh(200));
  auto invalid = snapshot();
  invalid.replace(invalid.find("working"), 7, "unknown");
  TEST_ASSERT_FALSE(model.accept(invalid, 200));
  invalid.assign(agentpets::kMaxSnapshotBytes + 1, ' ');
  TEST_ASSERT_FALSE(model.accept(invalid, 200));
}

void test_duplicate_agents_and_new_schema_are_rejected() {
  agentpets::Model model;
  agentpets::Snapshot value{1, "VPS", "stamp", {{"a", "Task", "P", "idle", "Idle"}}};
  value.agents.push_back(value.agents.front());
  std::string json;
  TEST_ASSERT_FALSE(glz::write_json(value, json));
  TEST_ASSERT_FALSE(model.accept(json, 0));
  value.agents.pop_back();
  value.version = 2;
  TEST_ASSERT_FALSE(glz::write_json(value, json));
  TEST_ASSERT_FALSE(model.accept(json, 0));
  value.version = 1;
  value.agents.assign(9, value.agents.front());
  TEST_ASSERT_FALSE(glz::write_json(value, json));
  TEST_ASSERT_FALSE(model.accept(json, 0));
}

void test_unchanged_source_stamp_becomes_stale_and_recovers() {
  agentpets::Model model;
  TEST_ASSERT_FALSE(model.fresh(0));
  TEST_ASSERT_TRUE(model.accept(snapshot(), 100));
  TEST_ASSERT_TRUE(model.accept(snapshot(), 14000));
  TEST_ASSERT_TRUE(model.fresh(15099));
  TEST_ASSERT_FALSE(model.fresh(15100));
  TEST_ASSERT_TRUE(model.accept(snapshot("2026-10-20T00:01:00Z"), 15200));
  TEST_ASSERT_TRUE(model.fresh(15200));
}

void test_stale_detection_handles_millis_wrap_and_empty_feed() {
  agentpets::Model model;
  const uint32_t start = 0xfffffff0U;
  TEST_ASSERT_TRUE(model.accept(snapshot(), start));
  TEST_ASSERT_TRUE(model.fresh(start + 500U));
  TEST_ASSERT_FALSE(model.fresh(start + agentpets::kStaleMs));
  TEST_ASSERT_TRUE(model.accept(R"({"version":1,"source":"VPS","updatedAt":"new","agents":[]})", 200));
  TEST_ASSERT_TRUE(model.fresh(200));
  TEST_ASSERT_EQUAL(0, model.snapshot().agents.size());
}

void test_device_config_requires_https_token_and_safe_ca_path() {
  agentpets::Config config;
  const std::string text = "url = \"https://example.com/v1/status\"\ntoken = \"" + std::string(64, 'a') + "\"\n";
  TEST_ASSERT_TRUE(agentpets::decodeConfig(text, config));
  TEST_ASSERT_EQUAL_STRING("/config/agent-ca.pem", config.caFile.c_str());
  config.url = "http://example.com/v1/status";
  TEST_ASSERT_FALSE(agentpets::validConfig(config));
  config.url = "https://example.com/@leak";
  TEST_ASSERT_FALSE(agentpets::validConfig(config));
  config.url = "https://example.com/v1/status";
  config.caFile = "/config/../secret.pem";
  TEST_ASSERT_FALSE(agentpets::validConfig(config));
  config.caFile = "/config/agent-ca.pem";
  config.token += '\n';
  TEST_ASSERT_FALSE(agentpets::validConfig(config));
}

void test_pet_layout_fits_original_lcd_and_compact_screen() {
  for (const int16_t width: {640, 320}) {
    RecordingGfx gfx(width, 172);
    ui::Context ui(gfx);
    screens::AgentPetsScreen pets;
    auto screen = screens::Screen::AgentPets;
    agentpets::View view;
    view.live = true;
    for (const auto* state: {"working", "waiting", "done", "error", "idle"})
      view.snapshot.agents.push_back({state, "Task title", "Project", state, state});
    pets.draw(ui, view, 0, screen);
    pets.draw(ui, view, 180, screen);
    TEST_ASSERT_FALSE(gfx.outside);
    view.snapshot.agents.clear();
    view.live = false;
    view.message = "Feed unavailable";
    pets.draw(ui, view, 360, screen);
    TEST_ASSERT_FALSE(gfx.outside);
  }
}

void test_pets_animate_live_and_freeze_when_offline() {
  RecordingGfx gfx(640, 172);
  ui::Context ui(gfx);
  screens::AgentPetsScreen pets;
  auto screen = screens::Screen::AgentPets;
  agentpets::View view;
  view.live = true;
  view.snapshot.agents.push_back({"id", "Task", "Project", "working", "Working"});
  gfx.watchedColor = ui::themes::rgb565(118, 228, 165);
  pets.draw(ui, view, 0, screen);
  const auto first = gfx.petRects;
  gfx.petRects.clear();
  pets.draw(ui, view, 180, screen);
  TEST_ASSERT_FALSE(first.empty());
  TEST_ASSERT_FALSE(first == gfx.petRects);
  view.live = false;
  gfx.watchedColor = ui.color(ui::themes::Muted);
  pets.draw(ui, view, 360, screen);
  gfx.petRects.clear();
  pets.draw(ui, view, 540, screen);
  const auto offline = gfx.petRects;
  gfx.petRects.clear();
  pets.draw(ui, view, 720, screen);
  TEST_ASSERT_FALSE(offline.empty());
  TEST_ASSERT_TRUE(offline == gfx.petRects);
}

void test_page_and_back_controls_work_without_waiting_for_next_animation() {
  RecordingGfx gfx(640, 172);
  ui::Context ui(gfx);
  ui.setTouchSource({.surface = {640, 172}, .poll = sample});
  screens::AgentPetsScreen pets;
  auto screen = screens::Screen::AgentPets;
  agentpets::View view;
  view.live = true;
  for (int i = 0; i < 8; ++i)
    view.snapshot.agents.push_back({std::to_string(i), "Agent" + std::to_string(i), "P", "working", "Working"});
  pets.draw(ui, view, 0, screen);
  contact = {true, 610, 14};
  TEST_ASSERT_TRUE(ui.pollTouch(1));
  pets.draw(ui, view, 1, screen);
  contact = {};
  TEST_ASSERT_TRUE(ui.pollTouch(2));
  pets.draw(ui, view, 2, screen);
  TEST_ASSERT_TRUE(pets.due(ui, 3));
  gfx.text.clear();
  pets.draw(ui, view, 3, screen);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, gfx.text.find("Agent4"));
  TEST_ASSERT_EQUAL(std::string::npos, gfx.text.find("Agent0"));
  contact = {true, 20, 14};
  TEST_ASSERT_TRUE(ui.pollTouch(4));
  pets.draw(ui, view, 4, screen);
  contact = {};
  TEST_ASSERT_TRUE(ui.pollTouch(5));
  pets.draw(ui, view, 5, screen);
  TEST_ASSERT_EQUAL(screens::Screen::Device, screen);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_statuses_are_bounded_and_transactional);
  RUN_TEST(test_duplicate_agents_and_new_schema_are_rejected);
  RUN_TEST(test_unchanged_source_stamp_becomes_stale_and_recovers);
  RUN_TEST(test_stale_detection_handles_millis_wrap_and_empty_feed);
  RUN_TEST(test_device_config_requires_https_token_and_safe_ca_path);
  RUN_TEST(test_pet_layout_fits_original_lcd_and_compact_screen);
  RUN_TEST(test_pets_animate_live_and_freeze_when_offline);
  RUN_TEST(test_page_and_back_controls_work_without_waiting_for_next_animation);
  return UNITY_END();
}
