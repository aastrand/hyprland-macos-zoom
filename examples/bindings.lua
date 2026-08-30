-- Add these to your Hyprland Lua configuration after enabling the plugin.
-- Raw modifier+scroll is configured in examples/config.lua. These are optional
-- discrete fallbacks plus keyboard actions; bindings remain user-owned.

if hl.plugin.macos_zoom ~= nil then
  hl.bind("CTRL + mouse_up", function()
    return hl.plugin.macos_zoom.adjust("in")
  end)

  hl.bind("CTRL + mouse_down", function()
    return hl.plugin.macos_zoom.adjust("out")
  end)

  hl.bind("CTRL + ALT + Z", function()
    return hl.plugin.macos_zoom.adjust("toggle")
  end)

  hl.bind("CTRL + ALT + 0", function()
    return hl.plugin.macos_zoom.adjust("reset")
  end)
end
