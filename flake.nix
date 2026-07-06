{
  description = "xpng — a modern xteddy replacement with real RGBA alpha compositing";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        # ── package ──────────────────────────────────────────────────────────
        packages.default = pkgs.stdenv.mkDerivation {
          pname   = "xpng";
          version = "1.0.0";

          src = ./.;

          nativeBuildInputs = with pkgs; [
            pkg-config
          ];

          buildInputs = with pkgs; [
            libX11
            libXrender
            libXext
            libXcomposite
            libXfixes      # pulled in by libXcomposite; listed explicitly
            libpng
          ];

          buildPhase = ''
            gcc -O2 -Wall -Wextra -std=c11 \
              $(pkg-config --cflags x11 xrender xext xcomposite libpng) \
              -o xpng xpng.c \
              $(pkg-config --libs   x11 xrender xext xcomposite libpng) \
              -lm
          '';

          installPhase = ''
            install -Dm755 xpng $out/bin/xpng
          '';

          meta = with pkgs.lib; {
            description     = "Display a PNG with real 8-bit RGBA alpha on X11 (modern xteddy)";
            longDescription = ''
              xpng is a lightweight X11 desktop companion that uses XRender to
              display any PNG file with full 8-bit per-channel alpha compositing.
              Transparent and semi-transparent pixels let the desktop show through.
              Supports live bilinear rescaling via the scroll wheel, drag-to-move,
              and an optional always-on-top mode.
            '';
            homepage    = "https://github.com/example/xpng";
            license     = licenses.mit;
            maintainers = [ ];
            platforms   = platforms.linux;
          };
        };

        # ── app (nix run) ─────────────────────────────────────────────────
        apps.default = {
          type    = "app";
          program = "${self.packages.${system}.default}/bin/xpng";
        };

        # ── dev shell ─────────────────────────────────────────────────────
        devShells.default = pkgs.mkShell {
          name = "xpng-dev";

          packages = with pkgs; [
            gcc
            pkg-config
            gnumake
            # runtime X libs
            xorg.libX11
            xorg.libXrender
            xorg.libXext
            xorg.libXcomposite
            xorg.libXfixes
            libpng
            # handy extras
            gdb
            valgrind
          ];

          shellHook = ''
            echo "xpng dev shell — build with: make"
          '';
        };
      }
    )

    # ── NixOS module (optional system-wide install) ───────────────────────
    // {
      nixosModules.default = { config, lib, pkgs, ... }:
        let
          cfg = config.programs.xpng;
        in {
          options.programs.xpng = {
            enable = lib.mkEnableOption "xpng — RGBA PNG viewer for X11";
          };

          config = lib.mkIf cfg.enable {
            environment.systemPackages = [
              self.packages.${pkgs.system}.default
            ];
          };
        };
    };
}
