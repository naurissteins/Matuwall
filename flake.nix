{
  description = "Matuwall - minimal, instant-open wallpaper picker for Wayland";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = {
    self,
    nixpkgs,
  }: let
    systems = [
      "x86_64-linux"
      "aarch64-linux"
    ];
    forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
  in {
    packages = forAllSystems (pkgs: {
      matuwall = pkgs.callPackage ./nix/package.nix {};
      default = self.packages.${pkgs.stdenv.hostPlatform.system}.matuwall;
    });

    overlays.default = final: _prev: {
      matuwall = final.callPackage ./nix/package.nix {};
    };

    # nix develop: meson, ninja, the C libraries, and the quality-gate tools
    devShells = forAllSystems (pkgs: {
      default = pkgs.mkShell {
        inputsFrom = [self.packages.${pkgs.stdenv.hostPlatform.system}.matuwall];
        packages = with pkgs; [
          # Newest LLVM, to match the rolling Arch CI image
          llvmPackages_latest.clang-tools
          valgrind
          shellcheck
          actionlint
        ];
        # The -O0 debug gates use -Werror, and fortify warns without -O
        hardeningDisable = ["fortify"];
      };
    });

    formatter = forAllSystems (pkgs: pkgs.alejandra);
  };
}
