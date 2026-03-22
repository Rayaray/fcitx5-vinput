#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <ubuntu24.04|debian12|fedora43|archlinux>" >&2
    exit 1
fi

target=$1
sudo_cmd=()
if [[ ${EUID} -ne 0 ]]; then
    sudo_cmd=(sudo)
fi

case "${target}" in
    ubuntu24.04)
        "${sudo_cmd[@]}" apt-get update
        "${sudo_cmd[@]}" apt-get install -y \
            cmake \
            ninja-build \
            pkg-config \
            gettext \
            curl \
            libcurl4-openssl-dev \
            libssl-dev \
            libarchive-dev \
            libpipewire-0.3-dev \
            libsystemd-dev \
            libfcitx5core-dev \
            libfcitx5config-dev \
            libfcitx5utils-dev \
            fcitx5-modules-dev \
            nlohmann-json3-dev \
            qtbase5-dev \
            qttools5-dev
        ;;
    debian12)
        apt-get update
        apt-get install -y \
            bzip2 \
            cmake \
            g++ \
            ninja-build \
            pkg-config \
            gettext \
            file \
            git \
            curl \
            libcurl4-openssl-dev \
            libssl-dev \
            libarchive-dev \
            libpipewire-0.3-dev \
            libsystemd-dev \
            libfcitx5core-dev \
            libfcitx5config-dev \
            libfcitx5utils-dev \
            fcitx5-modules-dev \
            nlohmann-json3-dev \
            qtbase5-dev \
            qttools5-dev
        ;;
    fedora43)
        "${sudo_cmd[@]}" dnf install -y \
            cmake \
            ninja-build \
            pkgconf-pkg-config \
            gettext \
            gcc-c++ \
            curl \
            libcurl-devel \
            openssl-devel \
            libarchive-devel \
            pipewire-devel \
            systemd-devel \
            fcitx5-devel \
            nlohmann-json-devel \
            qt5-qtbase-devel \
            qt5-qttools-devel \
            cli11-devel
        ;;
    archlinux)
        pacman -Syu --noconfirm --needed \
            cli11 \
            cmake \
            curl \
            fcitx5 \
            git \
            libarchive \
            ninja \
            nlohmann-json \
            openssl \
            pipewire \
            pkgconf \
            qt5-base \
            qt5-tools \
            systemd
        ;;
    *)
        echo "unsupported target: ${target}" >&2
        exit 1
        ;;
esac
