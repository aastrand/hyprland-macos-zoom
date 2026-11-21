#include "zoom_math.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <deque>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/state/MonitorState.hpp>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace {

HANDLE pluginHandle = nullptr;

struct SConfig {
  SP<Config::Values::CFloatValue> step;
  SP<Config::Values::CFloatValue> minimum;
  SP<Config::Values::CFloatValue> maximum;
  SP<Config::Values::CFloatValue> snapThreshold;
  SP<Config::Values::CFloatValue> toggleFactor;
  SP<Config::Values::CFloatValue> sensitivity;
  SP<Config::Values::CBoolValue> independentDisplays;
  SP<Config::Values::CBoolValue> rawScroll;
  SP<Config::Values::CBoolValue> consumeScroll;
  SP<Config::Values::CStringValue> modifier;
  SP<Config::Values::CBoolValue> invertTouchpad;
  SP<Config::Values::CBoolValue> momentum;
  SP<Config::Values::CFloatValue> momentumDecay;
} config;

std::unordered_map<MONITORID, float> previousZoom;

// Touchpad samples of the running gesture, used to estimate its velocity on lift
struct SScrollSample {
  uint32_t timeMs = 0;
  float step = 0.F;
};
std::deque<SScrollSample> gestureSamples;
PHLMONITORREF gestureMonitor;

// Only motion this close to the lift counts towards the momentum velocity
constexpr uint32_t VELOCITY_WINDOW_MS = 100;
// Momentum ends below this speed (zoom factor per millisecond)
constexpr float MIN_MOMENTUM_VELOCITY = 0.0001F;
constexpr auto MOMENTUM_TICK = std::chrono::milliseconds(8);

struct SMomentum {
  bool active = false;
  PHLMONITORREF monitor;
  float velocity = 0.F; // zoom factor per millisecond
  std::chrono::steady_clock::time_point lastTick;
} momentum;
SP<CEventLoopTimer> momentumTimer;

SDispatchResult failure(std::string message) {
  return {.success = false, .error = std::move(message)};
}

PHLMONITOR monitorUnderPointer() {
  if (!g_pInputManager || !State::monitorState())
    return nullptr;

  return State::monitorState()
      ->query()
      .vec(g_pInputManager->getMouseCoordsInternal())
      .run();
}

std::vector<PHLMONITOR> targetMonitors() {
  if (!State::monitorState())
    return {};

  if (!config.independentDisplays->value())
    return State::monitorState()->monitors();

  const auto monitor = monitorUnderPointer();
  return monitor ? std::vector<PHLMONITOR>{monitor} : std::vector<PHLMONITOR>{};
}

void setZoom(const PHLMONITOR &monitor, float target, bool remember = true,
             bool immediate = false) {
  if (!monitor || !monitor->m_cursorZoom)
    return;

  const auto minimum = config.minimum->value();
  const auto maximum = std::max(minimum, config.maximum->value());
  const auto current = monitor->m_cursorZoom->goal();
  const auto bounded = std::clamp(target, minimum, maximum);

  if (remember && current > minimum)
    previousZoom[monitor->m_id] = current;

  if (immediate)
    monitor->m_cursorZoom->setValueAndWarp(bounded);
  else
    *monitor->m_cursorZoom = bounded;
}

uint32_t modifierMask(std::string value) {
  std::ranges::transform(value, value.begin(),
                         [](unsigned char c) { return std::toupper(c); });

  uint32_t mask = 0;
  std::string token;
  std::istringstream stream(value);
  while (std::getline(stream, token, '+')) {
    token.erase(std::remove_if(token.begin(), token.end(),
                               [](unsigned char c) { return std::isspace(c); }),
                token.end());
    if (token == "CTRL" || token == "CONTROL")
      mask |= HL_MODIFIER_CTRL;
    else if (token == "ALT" || token == "OPTION")
      mask |= HL_MODIFIER_ALT;
    else if (token == "META" || token == "SUPER" || token == "COMMAND" ||
             token == "CMD")
      mask |= HL_MODIFIER_META;
    else if (token == "SHIFT")
      mask |= HL_MODIFIER_SHIFT;
  }
  return mask;
}

// Moves the zoom by `step` immediately. Returns false once it rests on a bound,
// so it cannot move further in that direction.
bool applyZoomStep(const PHLMONITOR &monitor, float step) {
  const auto minimum = config.minimum->value();
  const auto maximum = std::max(minimum, config.maximum->value());
  auto next =
      std::clamp(monitor->m_cursorZoom->value() + step, minimum, maximum);
  if (step < 0.F && next < config.snapThreshold->value())
    next = minimum;

  setZoom(monitor, next, false, true);
  return next > minimum && next < maximum;
}

void stopMomentum() {
  momentum.active = false;
  momentum.monitor.reset();
  if (momentumTimer)
    momentumTimer->updateTimeout(std::nullopt);
}

void onMomentumTick(SP<CEventLoopTimer> self, void *) {
  const auto monitor = momentum.monitor.lock();
  if (!momentum.active || !monitor || !monitor->m_cursorZoom) {
    stopMomentum();
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  const auto elapsed = std::clamp(
      std::chrono::duration<float, std::milli>(now - momentum.lastTick).count(),
      0.F, 50.F);
  momentum.lastTick = now;

  // Exponential decay with time constant tau: v(t) = v0 * e^(-t/tau). The
  // distance covered in this tick is the integral of v over the elapsed time.
  const auto tau = config.momentumDecay->value();
  const auto decay = std::exp(-elapsed / tau);
  const auto travelled = momentum.velocity * tau * (1.F - decay);
  momentum.velocity *= decay;

  if (!applyZoomStep(monitor, travelled) ||
      std::abs(momentum.velocity) < MIN_MOMENTUM_VELOCITY) {
    stopMomentum();
    return;
  }

  self->updateTimeout(MOMENTUM_TICK);
}

// Fingers left the touchpad: keep zooming with the gesture's final velocity
void startMomentum(uint32_t liftTimeMs) {
  const auto monitor = gestureMonitor.lock();
  float sum = 0.F;
  std::optional<uint32_t> first;
  for (const auto &sample : gestureSamples) {
    if (static_cast<int64_t>(liftTimeMs) - sample.timeMs > VELOCITY_WINDOW_MS)
      continue;
    // The first sample only marks where the measured interval begins
    if (!first)
      first = sample.timeMs;
    else
      sum += sample.step;
  }
  gestureSamples.clear();
  gestureMonitor.reset();

  if (!config.momentum->value() || !momentumTimer || !first || !monitor ||
      !monitor->m_cursorZoom)
    return;

  const auto span =
      static_cast<float>(std::max<int64_t>(
          static_cast<int64_t>(liftTimeMs) - *first, 1));
  const auto velocity = sum / span;
  if (std::abs(velocity) < MIN_MOMENTUM_VELOCITY)
    return;

  momentum = {.active = true,
              .monitor = monitor,
              .velocity = velocity,
              .lastTick = std::chrono::steady_clock::now()};
  momentumTimer->updateTimeout(MOMENTUM_TICK);
}

void onRawAxis(IPointer::SAxisEvent event, Event::SCallbackInfo &info) {
  if (!config.rawScroll->value() ||
      event.axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
    return;

  const bool touchpad = event.source == WL_POINTER_AXIS_SOURCE_FINGER;

  // A zero delta from a touchpad is the axis stop sent when the fingers lift
  if (event.delta == 0.0) {
    if (touchpad && !gestureSamples.empty()) {
      startMomentum(event.timeMs);
      info.cancelled = config.consumeScroll->value();
    }
    return;
  }

  constexpr uint32_t PRIMARY_MODIFIERS =
      HL_MODIFIER_SHIFT | HL_MODIFIER_CTRL | HL_MODIFIER_ALT | HL_MODIFIER_META;
  const auto required = modifierMask(config.modifier->value());
  const auto active = g_pInputManager->getModsFromAllKBs() & PRIMARY_MODIFIERS;
  if (required == 0 || active != required) {
    gestureSamples.clear();
    return;
  }

  const auto monitor = monitorUnderPointer();
  if (!monitor || !monitor->m_cursorZoom)
    return;

  stopMomentum();

  auto delta = static_cast<float>(event.delta);
  if (touchpad && config.invertTouchpad->value())
    delta = -delta;
  const auto step = -delta * config.sensitivity->value();
  applyZoomStep(monitor, step);

  if (touchpad) {
    if (gestureMonitor.lock() != monitor)
      gestureSamples.clear();
    gestureMonitor = monitor;
    gestureSamples.push_back({.timeMs = event.timeMs, .step = step});
    while (gestureSamples.size() > 1 &&
           static_cast<int64_t>(event.timeMs) - gestureSamples.front().timeMs >
               VELOCITY_WINDOW_MS)
      gestureSamples.pop_front();
  } else
    gestureSamples.clear();

  info.cancelled = config.consumeScroll->value();
}

std::optional<float> parseFloat(std::string_view input) {
  float value = 0.F;
  const auto [ptr, error] =
      std::from_chars(input.data(), input.data() + input.size(), value);
  if (error != std::errc{} || ptr != input.data() + input.size() ||
      !std::isfinite(value))
    return std::nullopt;
  return value;
}

SDispatchResult adjustZoom(std::string arguments) {
  std::istringstream stream(arguments);
  std::string action;
  stream >> action;

  if (action.empty())
    return failure(
        "macos-zoom requires one of: in, out, reset, toggle, set <factor>");

  auto monitors = targetMonitors();
  if (monitors.empty())
    return failure("macos-zoom could not find a monitor under the pointer");

  stopMomentum();

  const auto minimum = config.minimum->value();
  const auto maximum = std::max(minimum, config.maximum->value());
  const auto step = std::max(1.001F, config.step->value());
  const auto snap = std::clamp(config.snapThreshold->value(), minimum, maximum);

  if (action == "in" || action == "out") {
    const auto multiplier = action == "in" ? step : 1.F / step;
    for (const auto &monitor : monitors) {
      const auto current = monitor->m_cursorZoom->goal();
      setZoom(
          monitor,
          MacOSZoom::boundedZoom(current, multiplier, minimum, maximum, snap),
          false);
    }
    return {};
  }

  if (action == "reset") {
    for (const auto &monitor : monitors)
      setZoom(monitor, minimum);
    return {};
  }

  if (action == "toggle") {
    for (const auto &monitor : monitors) {
      const auto current = monitor->m_cursorZoom->goal();
      if (current > snap) {
        previousZoom[monitor->m_id] = current;
        setZoom(monitor, minimum, false);
      } else {
        const auto remembered = previousZoom.contains(monitor->m_id)
                                    ? previousZoom.at(monitor->m_id)
                                    : config.toggleFactor->value();
        setZoom(monitor, remembered, false);
      }
    }
    return {};
  }

  if (action == "set") {
    std::string factor;
    stream >> factor;
    const auto parsed = parseFloat(factor);
    if (!parsed)
      return failure("macos-zoom set requires a numeric factor");
    for (const auto &monitor : monitors)
      setZoom(monitor, *parsed);
    return {};
  }

  return failure("unknown macos-zoom action: " + action);
}

int luaAdjust(lua_State *state) {
  const char *arguments = luaL_checkstring(state, 1);
  const auto result = adjustZoom(arguments ? arguments : "");

  lua_newtable(state);
  lua_pushboolean(state, result.success);
  lua_setfield(state, -2, "ok");
  if (!result.success) {
    lua_pushstring(state, result.error.c_str());
    lua_setfield(state, -2, "error");
  }
  return 1;
}

void resetAllMonitors() {
  if (!State::monitorState())
    return;
  for (const auto &monitor : State::monitorState()->monitors()) {
    if (monitor && monitor->m_cursorZoom)
      monitor->m_cursorZoom->setValueAndWarp(1.F);
  }
}

} // namespace

APICALL EXPORT std::string PLUGIN_API_VERSION() { return HYPRLAND_API_VERSION; }

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
  pluginHandle = handle;

  const std::string runtimeHash = __hyprland_api_get_hash();
  const std::string headerHash = __hyprland_api_get_client_hash();
  if (runtimeHash != headerHash) {
    const auto mismatch = "macos-zoom: runtime ABI " + runtimeHash +
                          " does not match build ABI " + headerHash;
    HyprlandAPI::addNotification(
        handle, "[macos-zoom] Hyprland/header version mismatch",
        CHyprColor{1.F, 0.2F, 0.2F, 1.F}, 5000);
    throw std::runtime_error(mismatch);
  }

  config.step = makeShared<Config::Values::CFloatValue>(
      "plugin:macos_zoom:step", "multiplicative zoom per discrete input", 1.20F,
      Config::Values::SFloatValueOptions{.min = 1.001F, .max = 4.F});
  config.minimum = makeShared<Config::Values::CFloatValue>(
      "plugin:macos_zoom:min_factor", "minimum magnification", 1.F,
      Config::Values::SFloatValueOptions{.min = 1.F, .max = 40.F});
  config.maximum = makeShared<Config::Values::CFloatValue>(
      "plugin:macos_zoom:max_factor", "maximum magnification", 40.F,
      Config::Values::SFloatValueOptions{.min = 1.F, .max = 100.F});
  config.snapThreshold = makeShared<Config::Values::CFloatValue>(
      "plugin:macos_zoom:snap_threshold",
      "outward zoom below this snaps to minimum", 1.05F,
      Config::Values::SFloatValueOptions{.min = 1.F, .max = 4.F});
  config.toggleFactor = makeShared<Config::Values::CFloatValue>(
      "plugin:macos_zoom:toggle_factor", "initial factor restored by toggle",
      2.F, Config::Values::SFloatValueOptions{.min = 1.F, .max = 100.F});
  config.sensitivity = makeShared<Config::Values::CFloatValue>(
      "plugin:macos_zoom:sensitivity", "zoom factor change per raw axis unit",
      0.01F, Config::Values::SFloatValueOptions{.min = 0.0001F, .max = 1.F});
  config.independentDisplays = makeShared<Config::Values::CBoolValue>(
      "plugin:macos_zoom:independent_displays",
      "zoom only the display under the pointer", true);
  config.rawScroll = makeShared<Config::Values::CBoolValue>(
      "plugin:macos_zoom:raw_scroll", "consume continuous raw scroll deltas",
      true);
  config.consumeScroll = makeShared<Config::Values::CBoolValue>(
      "plugin:macos_zoom:consume_scroll",
      "prevent matching zoom scrolls from reaching applications", true);
  config.modifier = makeShared<Config::Values::CStringValue>(
      "plugin:macos_zoom:modifier", "modifier for continuous raw scroll",
      "CTRL");
  config.invertTouchpad = makeShared<Config::Values::CBoolValue>(
      "plugin:macos_zoom:invert_touchpad",
      "flip the zoom direction of touchpad scrolling", false);
  config.momentum = makeShared<Config::Values::CBoolValue>(
      "plugin:macos_zoom:momentum",
      "keep zooming and slow down after the fingers leave the touchpad", true);
  config.momentumDecay = makeShared<Config::Values::CFloatValue>(
      "plugin:macos_zoom:momentum_decay",
      "time constant of the momentum slowdown in milliseconds", 300.F,
      Config::Values::SFloatValueOptions{.min = 10.F, .max = 5000.F});

  HyprlandAPI::addConfigValueV2(handle, config.step);
  HyprlandAPI::addConfigValueV2(handle, config.minimum);
  HyprlandAPI::addConfigValueV2(handle, config.maximum);
  HyprlandAPI::addConfigValueV2(handle, config.snapThreshold);
  HyprlandAPI::addConfigValueV2(handle, config.toggleFactor);
  HyprlandAPI::addConfigValueV2(handle, config.sensitivity);
  HyprlandAPI::addConfigValueV2(handle, config.independentDisplays);
  HyprlandAPI::addConfigValueV2(handle, config.rawScroll);
  HyprlandAPI::addConfigValueV2(handle, config.consumeScroll);
  HyprlandAPI::addConfigValueV2(handle, config.modifier);
  HyprlandAPI::addConfigValueV2(handle, config.invertTouchpad);
  HyprlandAPI::addConfigValueV2(handle, config.momentum);
  HyprlandAPI::addConfigValueV2(handle, config.momentumDecay);

  momentumTimer =
      makeShared<CEventLoopTimer>(std::nullopt, onMomentumTick, nullptr);
  g_pEventLoopManager->addTimer(momentumTimer);

  if (!HyprlandAPI::addDispatcherV2(handle, "macos-zoom", adjustZoom))
    throw std::runtime_error("macos-zoom: failed to register dispatcher");
  if (!HyprlandAPI::addLuaFunction(handle, "macos_zoom", "adjust", luaAdjust))
    throw std::runtime_error("macos-zoom: failed to register Lua function");

  static auto axisListener = Event::bus()->m_events.input.mouse.axis.listen(
      [](IPointer::SAxisEvent event, Event::SCallbackInfo &info) {
        onRawAxis(event, info);
      });

  HyprlandAPI::reloadConfig();
  return {"macos-zoom", "macOS-like full-screen zoom controls for Hyprland",
          "Anders Åstrand", "0.1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
  stopMomentum();
  if (momentumTimer && g_pEventLoopManager)
    g_pEventLoopManager->removeTimer(momentumTimer);
  momentumTimer.reset();
  gestureSamples.clear();
  gestureMonitor.reset();
  resetAllMonitors();
  previousZoom.clear();
  pluginHandle = nullptr;
}
