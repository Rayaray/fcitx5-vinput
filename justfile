set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

build_dir := "build"

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
