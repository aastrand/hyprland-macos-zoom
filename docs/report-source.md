# macOS Accessibility Zoom: behavior and implementation findings

Audience: Omarchy and Hyprland plugin implementers  
Research date: 2026-08-30  
Scope: macOS system-level Accessibility Zoom, with full-screen modifier-plus-scroll behavior as the first implementation target.

## Executive answer

The familiar macOS feature is a compositor-level screen magnifier, not application zoom. With “Use scroll gesture with modifier keys to zoom” enabled, holding Control, Option, or Command while scrolling changes magnification. In full-screen mode the entire rendered display is enlarged, and pointer position determines the viewed region according to one of three tracking policies. Apple also supports split-screen and picture-in-picture lenses, keyboard and trackpad activation, keyboard-focus following, temporary detachment, multi-display policies, smoothing, saved zoom, and a screen-sharing policy.

For Omarchy on Hyprland 0.56.2, the correct first implementation is a small native plugin that controls Hyprland’s existing per-monitor animated compositor zoom. It should not re-render the desktop or capture the screen. Hyprland already preserves the world-space point under the cursor while the scale changes, clamps the camera to output bounds, and implements both continuous pointer anchoring and edge-triggered detached-camera panning.

## Documented macOS behavior

Apple’s current user guide describes three activation families: Option-Command-plus/minus and Option-Command-8; a three-finger double-tap/drag gesture; and a configurable modifier plus mouse or trackpad scrolling. The modifier choices are Control, Option, and Command. See [Zoom in on your Mac screen](https://support.apple.com/guide/mac-help/zoom-in-on-your-screen-mchl779716b8/mac).

The same guide defines three view styles:

- Full Screen magnifies an entire display.
- Split Screen places a resizable magnified region at an edge.
- Picture-in-Picture places a resizable magnified box around the pointer.

Apple’s advanced settings define three distinct pointer tracking policies:

- “Continuously with Pointer” moves the zoomed image whenever the pointer moves.
- “When Pointer Reaches Edge” holds the camera until the pointer reaches an edge.
- “To Keep Pointer Centered” keeps the zoomed view centered near the pointer.

They also document smoothing, restoring the factor at startup, independent per-display zoom, notification flashing, optional inclusion in screen sharing, a configurable rapid-zoom range, keyboard-focus following, and temporary modifiers for toggling, detaching the view from the pointer, and disabling panning. See [Change Zoom advanced options](https://support.apple.com/guide/mac-help/change-zoom-advanced-options-accessibility-mh35715/mac).

Apple historically specifies a maximum magnification of 40×. The current UI exposes a configurable min/max range without publishing numeric defaults, while Apple’s own accessibility material states “up to 40x”; 40× is therefore a defensible compatibility maximum, not a claim about every current machine’s default. See Apple’s [Special Education accessibility data sheet](https://www.apple.com/education/docs/Apple-Accessibility_DS_L417450A.pdf). Apple’s deployment settings also expose `closeViewFarPoint` and `closeViewNearPoint` as managed minimum and maximum zoom levels; see [remote configuration of accessibility](https://education.apple.com/story/250015089).

The geometric invariant is well supported: scale changes preserve a chosen center of magnification. A peer-reviewed screen-magnification study models macOS’s continuous mode as a scale transform around the magnification center, and its centered mode as an additional translation of that center to the screen center. See [Reading with Screen Magnification: Eye Movement Analysis Using Compensated Gaze Tracks](https://pmc.ncbi.nlm.nih.gov/articles/PMC11257655/).

## What Apple does not publish

Apple does not publish the private scroll-delta-to-scale curve, event coalescing, easing curve, animation duration, or subpixel filtering implementation. “Exactly” reproducing those internals would require controlled measurements on specific macOS hardware and releases. Those values can also differ between a notched wheel and a high-resolution trackpad.

The implementation therefore treats the following as explicit, tuneable approximations:

- A direct linear mapping from each raw axis delta to zoom factor, with configurable sensitivity defaulting to 0.01.
- Immediate updates to Hyprland’s animated zoom value, avoiding animation lag between input samples.
- A 1.05 snap threshold to avoid leaving the display imperceptibly above 1×.

The plugin also retains multiplicative `in` and `out` actions for keyboards and discrete binding systems, but those are not the primary macOS-like input path.

The observable interaction contract—system-wide compositing, cursor anchoring, output-edge clamping, three tracking modes, 1× floor, 40× compatibility ceiling, and independent displays—is the parity target.

## Hyprland and Omarchy mapping

Hyprland’s official configuration reference defines `cursor:zoom_factor` as compositor zoom around the cursor, `zoom_rigid` as centered versus loose tracking, and in 0.56.2 also provides `zoom_detached_camera`. See the [Hyprland 0.56 variables reference](https://wiki.hypr.land/0.56.0/Configuring/Basics/Variables/).

The pinned Hyprland 0.56.2 source confirms the mechanics. `CMonitorZoomController` preserves the world-space point under the pointer when magnification changes, clamps the transformed view to monitor bounds, and, in detached-camera mode, pans only after the pointer exits a padded safe area. See [`MonitorZoomController.cpp` at v0.56.2](https://github.com/hyprwm/Hyprland/blob/v0.56.2/src/output/MonitorZoomController.cpp). Per-monitor animated state lives in `CMonitor::m_cursorZoom`; see [`Monitor.cpp` at v0.56.2](https://github.com/hyprwm/Hyprland/blob/v0.56.2/src/output/Monitor.cpp).

The macOS modes map to Hyprland as follows:

- Continuously with Pointer: `zoom_detached_camera = false`, `zoom_rigid = false`.
- When Pointer Reaches Edge: `zoom_detached_camera = true`, `zoom_rigid = false`.
- To Keep Pointer Centered: `zoom_detached_camera = false`, `zoom_rigid = true`.

Current Omarchy user configuration is Lua, and user bindings belong in `~/.config/hypr/bindings.lua`. The plugin exposes both a Hyprland dispatcher and a Lua function so bindings can stay conventional and user-selectable.

## Limitations and stopping rule

Version 0.1 implements full-screen modifier-plus-raw-scroll behavior and generic commands suitable for other bindings. It preserves high-resolution axis magnitude, supports a configurable modifier and passthrough policy, and updates zoom without per-notch animation. It does not yet implement split-screen or picture-in-picture lenses, keyboard-focus following, persistent factor storage across login, temporary view detachment, or a policy for whether capture clients receive the transformed frame.

Research stopped after Apple’s documented behavior matrix was covered, Hyprland’s exact 0.56.2 render path was inspected, and the remaining gap was confined to unpublished Apple transfer and animation parameters. More web searching is unlikely to resolve those private values; a later calibration pass should use instrumented macOS recordings.

## Claim-to-source ledger

- Activation methods, modifier choices, view styles: Apple Support, “Zoom in on your Mac screen,” current guide, accessed 2026-08-30.
- Tracking, focus, multi-display, temporary actions, sharing, and appearance controls: Apple Support, “Change Zoom advanced options for accessibility on Mac,” current guide, accessed 2026-08-30.
- 40× capability: Apple, “Special Education” accessibility data sheet, historical first-party PDF, accessed 2026-08-30.
- Managed min/max fields: Apple Education Community, “How To: Enable remote configuration of accessibility,” 2026, accessed 2026-08-30.
- Transform model: Zhang et al., “Reading with Screen Magnification,” peer-reviewed open-access article, 2024, accessed 2026-08-30.
- Hyprland option semantics and plugin ABI guidance: Hyprland Wiki and Hyprland v0.56.2 source, accessed 2026-08-30.
