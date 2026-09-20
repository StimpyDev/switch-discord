#!/usr/bin/env bash
set -euo pipefail

PACMAN_CONF=/etc/pacman.conf
if ! grep -q '\[dkp-libs\]' "$PACMAN_CONF"; then
  cat >>"$PACMAN_CONF" <<'EOF'

[dkp-libs]
Server = https://pkg.devkitpro.org/packages
SigLevel = Optional TrustAll

[dkp-windows]
Server = https://pkg.devkitpro.org/packages/windows/$arch/
SigLevel = Optional TrustAll
EOF
fi

pacman-key --recv BC26F752D25B92CE272E0F44F7FD5492264BB9D0 --keyserver keyserver.ubuntu.com || true
pacman-key --lsign-key BC26F752D25B92CE272E0F44F7FD5492264BB9D0 || true

pacman -Syu --noconfirm
pacman -U --noconfirm https://pkg.devkitpro.org/devkitpro-keyring.pkg.tar.xz
pacman -Sy --noconfirm

pacman -S --noconfirm --needed \
  switch-dev \
  devkit-env \
  dkp-toolchain-vars \
  switch-curl \
  switch-jansson \
  switch-sdl2 \
  switch-sdl2_ttf \
  cmake \
  make \
  git

echo "devkitPro setup complete."
