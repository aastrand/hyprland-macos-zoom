-- Put this in ~/.config/hypr/bindings.lua after installing/enabling the plugin.
-- Raw modifier+scroll is configured in examples/config.lua. These are optional
-- discrete fallbacks plus keyboard actions; bindings remain user-owned.

if hl.plugin.macos_zoom ~= nil then
  o.bind("CTRL + mouse_up", "Zoom in", function()
    return hl.plugin.macos_zoom.adjust("in")
  end)

  o.bind("CTRL + mouse_down", "Zoom out", function()
    return hl.plugin.macos_zoom.adjust("out")
  end)

  o.bind("CTRL + ALT + Z", "Toggle zoom", function()
    return hl.plugin.macos_zoom.adjust("toggle")
  end)

  o.bind("CTRL + ALT + 0", "Reset zoom", function()
    return hl.plugin.macos_zoom.adjust("reset")
  end)
end
