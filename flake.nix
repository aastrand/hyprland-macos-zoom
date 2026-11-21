{
  description = "Continuous macOS-like modifier-scroll screen zoom for Hyprland";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-26.05";
  };

  outputs = {
    self,
    nixpkgs,
  }: let
    supportedSystems = [
      "x86_64-linux"
      "aarch64-linux"
    ];
    forAllSystems = f:
      nixpkgs.lib.genAttrs supportedSystems (system:
        f (import nixpkgs {
          inherit system;
          overlays = [self.overlays.default];
        }));
  in {
    # Plugins are ABI-coupled to the compositor: use the overlay so the plugin is
    # built against the Hyprland of the nixpkgs it is applied to
    overlays.default = final: prev: {
      hyprlandPlugins =
        prev.hyprlandPlugins
        // {
          macos-zoom = final.callPackage ./nix/package.nix {
            inherit (prev.hyprlandPlugins) mkHyprlandPlugin;
          };
        };
    };

    packages = forAllSystems (pkgs: {
      default = pkgs.hyprlandPlugins.macos-zoom;
      macos-zoom = pkgs.hyprlandPlugins.macos-zoom;
    });

    formatter = forAllSystems (pkgs: pkgs.alejandra);
  };
}
