# hyprland-macos-zoom

A native Hyprland plugin for macOS-like full-screen accessibility zoom. It uses Hyprland’s compositor-native zoom camera and leaves bindings in your configuration.

The [research report](docs/report-source.md) defines the parity target and evidence. The [implementation plan](docs/plan.md) separates the implemented full-screen core from later accessibility features.

## Requirements

- Hyprland 0.56.2 or a source-compatible build with matching installed headers.
- CMake 3.27+, a C++23 compiler, `pkg-config`, and Hyprland build dependencies.
- Hyprland’s Lua configuration for the provided examples.

Hyprland plugins are ABI-coupled to the compositor build. Rebuild after every Hyprland update.

## Build and test

```sh
make test
```

The plugin is written to `build/macos-zoom.so`.

For a local development load:

```sh
hyprctl plugin load "$PWD/build/macos-zoom.so"
hyprctl eval "hl.plugin.macos_zoom.adjust('in')"
hyprctl eval "hl.plugin.macos_zoom.adjust('out')"
hyprctl eval "hl.plugin.macos_zoom.adjust('toggle')"
hyprctl eval "hl.plugin.macos_zoom.adjust('reset')"
hyprctl eval "hl.plugin.macos_zoom.adjust('set 3.5')"
```

The native `macos-zoom` dispatcher is also registered for legacy Hyprland
configurations. Lua configurations should use the Lua API shown above.

Unload with an absolute path; unloading resets every output to 1×.

```sh
hyprctl plugin unload "$PWD/build/macos-zoom.so"
```

For a published repository, install through `hyprpm add <repository-url>` and `hyprpm enable macos-zoom`. Add release commit pins to `hyprpm.toml` when the repository has stable release commits.

## Hyprland configuration

Copy the relevant parts of [examples/config.lua](examples/config.lua) into your Hyprland configuration. With `raw_scroll = true`, the plugin reads high-resolution wheel/trackpad axis deltas directly while the configured modifier is held. This is the path that gives the responsive, linear macOS-like feel.

[examples/bindings.lua](examples/bindings.lua) contains optional discrete fallbacks plus toggle and reset bindings for `~/.config/hypr/bindings.lua`. You can replace those with any keys you prefer. The raw modifier is separately configurable as `CTRL`, `ALT`, `META`, `SHIFT`, or a `+`-separated combination.

After editing your Hyprland config:

```sh
hyprctl reload
hyprctl configerrors
```

## Tracking modes

Use exactly one of these pairs in `cursor` configuration:

- macOS “Continuously with Pointer”: `zoom_detached_camera = false`, `zoom_rigid = false`.
- macOS “When Pointer Reaches Edge”: `zoom_detached_camera = true`, `zoom_rigid = false`.
- macOS “To Keep Pointer Centered”: `zoom_detached_camera = false`, `zoom_rigid = true`.

## Plugin options

All options live below `plugin.macos_zoom` in Lua config:

- `raw_scroll`: use continuous axis deltas; default `true`.
- `modifier`: modifier for raw scrolling; default `CTRL`.
- `sensitivity`: linear factor change per raw axis unit; default `0.01`.
- `consume_scroll`: prevent the modified gesture from also scrolling the app; default `true`.
- `step`: multiplicative change used by the optional discrete `in`/`out` actions; default `1.20`.
- `min_factor`: lower bound; default `1.0`.
- `max_factor`: upper bound; default `40.0`.
- `snap_threshold`: values below this snap to the minimum when zooming out; default `1.05`.
- `toggle_factor`: initial toggle target before a previous factor exists; default `2.0`.
- `independent_displays`: affect only the output under the pointer; default `true`.

## Current scope

Version 0.1 targets macOS full-screen modifier-plus-wheel behavior with continuous raw input and conventional bindable actions. Split-screen and picture-in-picture lenses, keyboard-focus following, persistent zoom across login, temporary detachment, and capture policy are planned separately because they require different compositor integration.
