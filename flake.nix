{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
  };

  outputs = {
    self,
    nixpkgs,
    ...
  }: {
    devShells.x86_64-linux.default = let
      pkgs = import nixpkgs {system = "x86_64-linux";};
    in
      pkgs.mkShell {
        nativeBuildInputs = [
          pkgs.gcc-arm-embedded
          pkgs.git
          pkgs.python3
          pkgs.gnumake
          pkgs.clang
          pkgs.llvm
          pkgs.lld
          pkgs.cmake
        ];

        shellHook = ''
          export CROSS_COMPILE=arm-none-eabi-
        '';
      };
  };
}
