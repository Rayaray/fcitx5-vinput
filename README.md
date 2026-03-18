<div align="center">

# fcitx5-vinput

**Local offline voice input plugin for Fcitx5**

[![License](https://img.shields.io/github/license/xifan2333/fcitx5-vinput)](LICENSE)
[![CI](https://github.com/xifan2333/fcitx5-vinput/actions/workflows/ci.yml/badge.svg)](https://github.com/xifan2333/fcitx5-vinput/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/xifan2333/fcitx5-vinput)](https://github.com/xifan2333/fcitx5-vinput/releases)
[![AUR](https://img.shields.io/aur/version/fcitx5-vinput-bin)](https://aur.archlinux.org/packages/fcitx5-vinput-bin)
[![Downloads](https://img.shields.io/github/downloads/xifan2333/fcitx5-vinput/total)](https://github.com/xifan2333/fcitx5-vinput/releases)

[English](README.md) | [中文](README_zh.md)

https://github.com/user-attachments/assets/5a548a68-153c-4842-bab6-926f30bb720e

</div>

Powered by [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) for on-device speech recognition, with optional LLM post-processing via any OpenAI-compatible API.

## Features

- **Two trigger modes** — tap to toggle recording on/off, or hold to push-to-talk
- **Command mode** — select text, speak an instruction, release to apply
- **LLM post-processing** — error correction, formatting, translation, and more
- **Scene management** — switch post-processing prompts on the fly
- **Multiple LLM providers** — configure and switch between servers at runtime
- **Hotword support** for compatible models
- **`vinput` CLI** — manage models, scenes, and LLM config from the terminal

## Installation

### Arch Linux (AUR)

```bash
yay -S fcitx5-vinput-bin
```

### Fedora (COPR)

```bash
sudo dnf copr enable xifan/fcitx5-vinput-bin
sudo dnf install fcitx5-vinput
```

### Ubuntu 24.04 (PPA)

```bash
sudo add-apt-repository ppa:xifan233/ppa
sudo apt update
sudo apt install fcitx5-vinput
```

### Ubuntu / Debian (manual)

```bash
# Download latest .deb from GitHub Releases
sudo dpkg -i fcitx5-vinput_*.deb
sudo apt-get install -f
```

### Build from Source

**Dependencies:** `cmake` `fcitx5` `sherpa-onnx` `pipewire` `libcurl` `nlohmann-json` `CLI11` `Qt6`

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

## Quick Start

**1. Install a model**

```bash
vinput model list --remote      # Browse available models
vinput model add <model-name>   # Download and install
vinput model use <model-name>   # Set as active model
```

Or manually place a model directory in `~/.local/share/fcitx5-vinput/models/<model-name>/` containing:
- `vinput-model.json`
- `model.int8.onnx` or `model.onnx`
- `tokens.txt`

**2. Start the daemon**

```bash
systemctl --user enable --now vinput-daemon.service
```

**3. Enable in Fcitx5**

Open Fcitx5 Configuration → Addons → Find **Vinput** → Enable.

**4. Start using**

- **Tap** `Alt_R` to start recording, tap again to stop and recognize
- **Hold** `Alt_R` to record, release to recognize (push-to-talk)

## Key Bindings

| Key | Default | Function |
|-----|---------|----------|
| Trigger Key | `Alt_R` | Tap to toggle recording; hold to push-to-talk |
| Command Key | `Control_R` | Hold after selecting text to modify with voice |
| Scene Menu Key | `Shift_R` | Open scene switcher |
| Page Up / Down | `Page Up` / `Page Down` | Navigate candidate list |
| Move | `↑` / `↓` | Move cursor in candidate list |
| Confirm | `Enter` | Confirm selected candidate |
| Cancel | `Esc` | Close menu |
| Quick Select | `1`–`9` | Quick-pick candidate |

All keys can be customized in Fcitx5 configuration.

## Configuration

### GUI

```bash
vinput-gui
```

Or open the Vinput addon in Fcitx5 Configuration.

### CLI Reference

<details>
<summary>Model Management</summary>

```bash
vinput model list               # List installed models
vinput model list --remote      # List available remote models
vinput model add <name>         # Download and install
vinput model use <name>         # Switch active model
vinput model remove <name>      # Remove model
vinput model info <name>        # View model details
```

</details>

<details>
<summary>Scene Management</summary>

```bash
vinput scene list               # List all scenes
vinput scene add                # Add scene (interactive)
vinput scene edit               # Edit scene
vinput scene use <ID>           # Switch active scene
vinput scene remove <ID>        # Remove scene
```

</details>

<details>
<summary>LLM Configuration</summary>

```bash
vinput llm list                 # List all providers
vinput llm add                  # Add provider (interactive)
vinput llm edit                 # Edit provider
vinput llm use <name>           # Switch active provider
vinput llm remove <name>        # Remove provider
vinput llm enable               # Enable LLM post-processing
vinput llm disable              # Disable LLM post-processing
```

</details>

<details>
<summary>Hotword Management</summary>

```bash
vinput hotword get              # View current hotword file path
vinput hotword set <path>       # Set hotword file
vinput hotword edit             # Open hotword file in editor
vinput hotword clear            # Clear hotword configuration
```

</details>

<details>
<summary>Recording Control</summary>

```bash
vinput recording start              # Start recording
vinput recording stop               # Stop recording and recognize
vinput recording stop --scene <ID>  # Stop with specific scene
vinput recording toggle             # Toggle recording start/stop
vinput recording toggle --scene <ID># Toggle with specific scene
```

</details>

<details>
<summary>Daemon Management</summary>

```bash
vinput daemon status            # Check daemon status
vinput daemon start             # Start daemon
vinput daemon stop              # Stop daemon
vinput daemon restart           # Restart daemon
vinput daemon logs              # View logs
```

</details>

## Scenes

Scenes control how LLM processes recognition results. Switch between them at runtime using the scene menu key.

Each scene has:
- **ID** — unique identifier
- **Label** — display name in the menu
- **Prompt** — system prompt sent to the LLM

The `default` scene calls LLM like any other scene (when enabled). To bypass LLM entirely, disable it globally or set the scene's candidate count to `0`.

## Command Mode

Select text → hold command key → speak your instruction → release → done.

**Examples:**
- Select Chinese text → say *"translate to English"* → replaced with translation
- Select code → say *"add comments"* → replaced with commented version

> Requires LLM to be configured and enabled.

## LLM Setup Example

Using local [Ollama](https://ollama.com):

```bash
vinput llm add
# Name:     ollama
# Base URL: http://127.0.0.1:11434/v1
# API Key:  (leave empty)
# Model:    qwen2.5:7b

vinput llm use ollama
vinput llm enable
```

## Configuration Files

| File | Path |
|------|------|
| Plugin config (keybindings, etc.) | `~/.config/fcitx5/conf/vinput.conf` |
| Core config (model, LLM, scenes) | `~/.config/vinput/config.json` |
| Model directory | `~/.local/share/fcitx5-vinput/models/` |

## Release

Push a tag (e.g. `v0.1.0`) and GitHub Actions will automatically build and publish:

- Source tarball `fcitx5-vinput-<version>.tar.gz`
- Ubuntu 24.04 `.deb`
- Debian 12 `.deb`
- Arch Linux `.pkg.tar.zst`
- Fedora COPR (`xifan/fcitx5-vinput-bin`)
- Ubuntu PPA (`ppa:xifan233/ppa`)
