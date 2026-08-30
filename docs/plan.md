# Implementation plan

## Goal and acceptance criteria

Build a native Hyprland plugin that makes full-screen zoom feel like macOS while leaving input choices in user configuration.

Version 0.1 is acceptable when it:

- Zooms the complete compositor output, including panels and overlays.
- Keeps the content under the pointer stationary while magnification changes.
- Supports continuous, edge-panning, and centered tracking through documented Hyprland settings.
- Maps high-resolution axis deltas linearly and responsively, with a 1× floor, configurable 40× ceiling, and a clean snap back to 1×.
- Tracks monitors independently by default.
- Exposes `in`, `out`, `reset`, `toggle`, and `set` actions to Lua bindings and `hyprctl`.
- Builds against the installed Hyprland headers, refuses an ABI hash mismatch, and resets zoom safely on unload.

## Architecture

The native plugin owns only zoom state transitions and the modified raw-axis listener. Each gesture or action selects the monitor under the pointer, computes a bounded target factor, and warps Hyprland’s existing `m_cursorZoom` animated variable. Immediate warping preserves one-to-one input response while Hyprland continues to own rendering, camera transforms, damage, cursor anchoring, and panning.

The plugin owns no user keybindings. The raw gesture modifier is configurable, and any `hl.bind` can call `hl.plugin.macos_zoom.adjust(...)` for discrete fallbacks and keyboard actions.

## Delivery phases

1. Full-screen core: continuous raw-axis input, stateful commands, per-display state, Hyprland configuration examples, and unit-tested scale math. Implemented in 0.1.
2. Hardware calibration: record macOS wheel and trackpad input at known deltas and tune the linear sensitivity without changing the public actions.
3. Input refinement: add optional device-specific transfer curves only if measurements show that linear mapping is insufficient. Keep dotfile dispatch mode available.
4. Accessibility completeness: keyboard-focus following, temporary detach/disable-pan modifiers, persistence, and notification cues.
5. Alternate view styles: split-screen and picture-in-picture only if Hyprland gains a stable render-pass/lens API or a carefully isolated renderer extension proves maintainable.

## Risks and mitigations

- Hyprland plugins have strict ABI coupling. `hyprpm.toml`, the runtime/header hash check, and release commit pins after publication mitigate this.
- Private renderer access changes between Hyprland releases. Version 0.1 touches only public monitor state already used by Hyprland’s own cursor zoom gesture.
- Raw axis events vary by device. Linear sensitivity is configurable, and discrete bind actions remain available as a fallback.
- Users can choose conflicting tracking booleans. The README documents the three valid pairs.
- Unloading while zoomed could strand a transformed desktop. `PLUGIN_EXIT` warps every monitor back to 1×.

## Verification

- Compile with warnings against Hyprland 0.56.2 headers.
- Run unit tests for multiplication, clamping, snapping, and invalid numeric input.
- Verify exported plugin entry points and shared-library dependencies.
- In a live Hyprland session: load, check `hyprctl plugin list`, exercise every action on each monitor, confirm edge clamping and pointer anchoring, unload, and confirm 1× restoration.
- After installing bindings: run `hyprctl reload` and `hyprctl configerrors`.
