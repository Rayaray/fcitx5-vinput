set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

build_dir := "build"
flatpak_build_dir := "builddir"
flatpak_repo := "repo"
flatpak_bundle := "fcitx5-vinput.flatpak"
flatpak_app_id := "org.fcitx.Fcitx5.Addon.Vinput"
flatpak_host_app_id := "org.fcitx.Fcitx5"
flatpak_manifest := "packaging/flatpak/ci-manifest.yaml"
flatpak_branch := "stable"

default:
  @just --list

configure type="Release" *cmake_args:
  cmake -B {{build_dir}} -DCMAKE_BUILD_TYPE={{type}} {{cmake_args}}

dev *cmake_args:
  cmake -B {{build_dir}} -DCMAKE_BUILD_TYPE=Debug {{cmake_args}}

release *cmake_args:
  cmake -B {{build_dir}} -DCMAKE_BUILD_TYPE=Release {{cmake_args}}

build:
  cmake --build {{build_dir}}

install:
  cmake --install {{build_dir}}

clean:
  rm -rf {{build_dir}}

rebuild type="Release" *cmake_args: clean
  cmake -B {{build_dir}} -DCMAKE_BUILD_TYPE={{type}} {{cmake_args}}
  cmake --build {{build_dir}}

sherpa version="" prefix="/usr" archive="":
  bash scripts/build-sherpa-onnx.sh "{{version}}" "{{prefix}}" "{{archive}}"

check-i18n:
  python3 scripts/check-i18n.py

source-archive:
  bash scripts/create-source-archive.sh

flatpak-build manifest=flatpak_manifest:
  flatpak-builder --user --force-clean --repo={{flatpak_repo}} --ccache {{flatpak_build_dir}} {{manifest}}

flatpak-bundle repo=flatpak_repo bundle=flatpak_bundle app_id=flatpak_app_id branch=flatpak_branch:
  flatpak build-bundle --runtime {{repo}} {{bundle}} {{app_id}} {{branch}}

flatpak-install bundle=flatpak_bundle:
  flatpak install --user -y {{bundle}}

flatpak-permissions app_id=flatpak_host_app_id:
  flatpak override --user --filesystem=xdg-run/pipewire-0 {{app_id}}
  flatpak override --user --filesystem=xdg-config/systemd:create {{app_id}}
