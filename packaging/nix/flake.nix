{
  description = "Serika PhotoEdit native editor";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  outputs = { self, nixpkgs }: let
    systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
    eachSystem = f: nixpkgs.lib.genAttrs systems (system: f (import nixpkgs { inherit system; }));
  in {
    packages = eachSystem (pkgs: { default = pkgs.stdenv.mkDerivation {
      pname = "serika-photoedit"; version = "1.0.0";
      src = pkgs.lib.cleanSourceWith {
        src = ../..;
        filter = path: type: !(builtins.elem (builtins.baseNameOf path) [ ".git" ".tools" "build" "dist" ]);
      };
      nativeBuildInputs = with pkgs; [ cmake ninja pkg-config qt6.wrapQtAppsHook ];
      buildInputs = with pkgs; [ qt6.qtbase qt6.qtsvg qt6.qtimageformats libraw zlib ];
      cmakeFlags = [ "-DBUILD_TESTING=OFF" ];
    }; });
    devShells = eachSystem (pkgs: { default = pkgs.mkShell {
      inputsFrom = [ self.packages.${pkgs.system}.default ];
      packages = with pkgs; [ cmake ninja pkg-config ];
    }; });
  };
}
