{
  lib,
  mkHyprlandPlugin,
  cmake,
}:
mkHyprlandPlugin {
  pluginName = "macos-zoom";
  version = "0.1.0";

  src = lib.cleanSourceWith {
    src = ../.;
    filter = path: _type: let
      name = baseNameOf path;
    in
      !(builtins.elem name ["nix" "flake.nix" "flake.lock" "assets" "build"]);
  };

  nativeBuildInputs = [cmake];
  cmakeFlags = ["-DBUILD_TESTING=OFF"];

  meta = {
    description = "Continuous macOS-like modifier-scroll screen zoom for Hyprland";
    homepage = "https://github.com/aastrand/hyprland-macos-zoom";
    license = lib.licenses.bsd3;
    platforms = lib.platforms.linux;
  };
}
