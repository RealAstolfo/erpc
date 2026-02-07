{
  description = "erpc — C++20 Remote Procedure Call Library";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    exstd = {
      url = "github:RealAstolfo/exstd";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.flake-utils.follows = "flake-utils";
    };
    enet = {
      url = "github:RealAstolfo/enet";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.flake-utils.follows = "flake-utils";
      inputs.exstd.follows = "exstd";
    };
  };

  outputs = { self, nixpkgs, flake-utils, exstd, enet }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        exstdPkg = exstd.packages.${system}.default;
        enetPkg = enet.packages.${system}.default;
      in {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "erpc";
          version = "0.1.0";
          src = ./.;

          nativeBuildInputs = with pkgs; [ zig gnumake pkg-config ];
          buildInputs = with pkgs; [ openssl zlib libmd ];

          postUnpack = ''
            rm -rf $sourceRoot/vendors
            mkdir -p $sourceRoot/vendors
            cp -r ${exstdPkg.passthru.src-with-vendors} $sourceRoot/vendors/exstd
            cp -r ${enetPkg.passthru.src-with-vendors} $sourceRoot/vendors/enet
            chmod -R u+w $sourceRoot/vendors
          '';

          buildPhase = ''
            make rpc-node.o
          '';

          installPhase = ''
            mkdir -p $out/include $out/lib
            cp -r include/* $out/include/
            cp rpc-node.o $out/lib/ 2>/dev/null || true
          '';

          passthru.src-with-vendors = pkgs.runCommand "erpc-src" {} ''
            cp -r ${self} $out
            chmod -R u+w $out
            rm -rf $out/vendors
            mkdir -p $out/vendors
            cp -r ${exstdPkg.passthru.src-with-vendors} $out/vendors/exstd
            cp -r ${enetPkg.passthru.src-with-vendors} $out/vendors/enet
            chmod -R u+w $out/vendors
          '';
        };

        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            zig
            gcc
            gnumake
            pkg-config
            openssl
            zlib
            libmd
          ];

          shellHook = ''
            mkdir -p vendors
            if [ ! -d vendors/exstd ] || [ -L vendors/exstd ]; then
              rm -rf vendors/exstd
              cp -r ${exstdPkg.passthru.src-with-vendors} vendors/exstd
              chmod -R u+w vendors/exstd
            fi
            if [ ! -d vendors/enet ] || [ -L vendors/enet ]; then
              rm -rf vendors/enet
              cp -r ${enetPkg.passthru.src-with-vendors} vendors/enet
              chmod -R u+w vendors/enet
            fi
            echo "erpc development environment"
            echo "  Build: make all"
          '';
        };
      }
    );
}
