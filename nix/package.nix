{
  lib,
  stdenv,
  meson,
  ninja,
  pkg-config,
  scdoc,
  wayland-scanner,
  versionCheckHook,
  wayland,
  wayland-protocols,
  libxkbcommon,
  libpng,
  libjpeg_turbo,
  libwebp,
}:
stdenv.mkDerivation {
  pname = "matuwall";
  version = builtins.head (
    builtins.match ".*version: '([0-9.]+)'.*" (builtins.readFile ../meson.build)
  );

  # Only what the build needs
  src = lib.fileset.toSource {
    root = ../.;
    fileset = lib.fileset.unions [
      ../meson.build
      ../src
      ../protocol
      ../doc
    ];
  };

  strictDeps = true;
  depsBuildBuild = [pkg-config];

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
    scdoc
    wayland-scanner
  ];

  buildInputs = [
    wayland
    wayland-protocols
    libxkbcommon
    libpng
    libjpeg_turbo
    libwebp
  ];

  nativeInstallCheckInputs = [versionCheckHook];
  doInstallCheck = true;

  meta = {
    description = "Fast and lightweight wallpaper picker for Wayland";
    homepage = "https://github.com/naurissteins/Matuwall";
    license = lib.licenses.gpl3Plus;
    mainProgram = "matuwall";
    platforms = lib.platforms.linux;
  };
}
