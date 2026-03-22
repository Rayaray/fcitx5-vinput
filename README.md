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
- **Multiple LLM providers** — configure providers and assign them per scene
- **Hotword support** for compatible models
- **`vinput` CLI** — manage models, scenes, providers, and daemon state from the terminal

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

**Dependencies:** `cmake` `fcitx5` `pipewire` `libcurl` `nlohmann-json` `CLI11` `Qt6`

```bash
sudo bash scripts/build-sherpa-onnx.sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

If you use `just`, the repository now includes a thin wrapper around the same
commands:

```bash
just sherpa
just release
just build
sudo just install
```

The first step downloads the pre-built `sherpa-onnx` runtime used by local
builds and release packaging. Runtime libraries are bundled with the installed
artifacts instead of being declared as a separate system package dependency.
Source builds now default to the Fcitx5 system prefix (`/usr`) so the addon is
installed under the directories Fcitx5 scans. If you installed an older build
under `/usr/local`, reinstall from a clean build directory so `vinput.conf` and
`fcitx5-vinput.so` move to the Fcitx5 system paths.

## Quick Start

**1. Install a model**

```bash
vinput model list --remote      # Browse available models
vinput model add <model-name>   # Download and install
vinput model use <model-name>   # Set as active model
```

Or manually place a model directory in `~/.local/share/vinput/models/<model-name>/` containing:
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

Use `vinput --help` or `vinput <subcommand> --help` for the full current syntax.

> To use the Vinput CLI with a Flatpak installation, use `flatpak run --command=/app/addons/Vinput/bin/vinput org.fcitx.Fcitx5 --help`

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
vinput scene add --id <id>      # Add a scene
vinput scene use <ID>           # Switch active scene
vinput scene remove <ID>        # Remove scene
```

Scenes that use LLM must set `--provider`, `--model`, and `--prompt` together.

</details>

<details>
<summary>LLM Configuration</summary>

```bash
vinput llm list                 # List configured providers
vinput llm add <name> --base-url <url>
vinput llm remove <name>        # Remove provider
vinput adaptor list             # List built-in/user LLM adaptors
vinput adaptor start <id>       # Start an LLM adaptor
vinput adaptor stop <id>        # Stop an LLM adaptor
```

LLM providers are referenced by scenes; there is no separate active-provider
toggle. An LLM adaptor is only a local OpenAI-compatible bridge process that a
provider may point to.

</details>

<details>
<summary>ASR Providers</summary>

```bash
vinput asr list                 # List configured ASR providers
vinput asr add <name>           # Add a builtin or command provider
vinput asr use <name>           # Switch active ASR provider
vinput asr edit <name>          # Edit external provider script
vinput asr remove <name>        # Remove provider
```

Built-in ASR provider scripts can be referenced directly by script ID in
command providers, without hardcoding a full path.

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
<summary>Device Management</summary>

```bash
vinput device list              # List capture devices
vinput device use <name>        # Set active device
```

</details>

<details>
<summary>Config Helpers</summary>

```bash
vinput config set extra.hotwords_file <path> # Write a supported config value
vinput config edit extra                    # Edit core config file
vinput config edit fcitx                    # Edit Fcitx addon config
```

Registry fallback is configured directly in `config.json` with
`registry.sources`. Sources are tried in order until one succeeds.

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
vinput daemon start             # Start daemon
vinput daemon stop              # Stop daemon
vinput daemon restart           # Restart daemon
vinput daemon logs              # View logs
vinput status                   # Show overall vinput status
vinput init                     # Create default config and model directories
```

</details>

## Scenes

Scenes control how LLM processes recognition results. Switch between them at runtime using the scene menu key.

Each scene has:
- **ID** — unique identifier
- **Label** — display name in the menu
- **Prompt** — system prompt sent to the LLM

LLM is only used when a scene has `provider + model + prompt` configured.
The built-in `__raw__` scene bypasses LLM entirely.

## Command Mode

Select text → hold command key → speak your instruction → release → done.

If there is no active surrounding-text selection, command mode falls back to
the current primary-selection clipboard text. That keeps the workflow
convenient, but it also means stale or unrelated selected text may be sent into
the command rewrite flow if your primary selection is not what you expect.

**Examples:**
- Select Chinese text → say *"translate to English"* → replaced with translation
- Select code → say *"add comments"* → replaced with commented version

> Requires LLM to be configured and enabled.

## LLM Setup Example

Using local [Ollama](https://ollama.com):

```bash
vinput llm add ollama --base-url http://127.0.0.1:11434/v1
vinput scene add --id polish \
  --label "Polish" \
  --provider ollama \
  --model qwen2.5:7b \
  --prompt "Rewrite the recognized text into polished Chinese."
vinput scene use polish
```

## Provider Scripts And Adaptor Contracts

Optional integration scripts now live under two flat directories inside
`data/`:

- `data/asr-providers/`: external ASR provider scripts
- `data/llm-adaptors/`: LLM OpenAI-compatible adaptor scripts

The `scripts/` directory is reserved for project maintenance tasks such as
build, packaging, and checks.

### ASR Command Provider Contract

External ASR scripts should follow normal Unix command semantics:

- `stdin`: one complete utterance audio stream
- `stdout`: final transcript text
- `stderr`: human-readable error output
- exit code `0`: success
- non-zero exit code: failure

Today `vinput` sends audio to command providers as:

- PCM `S16_LE`
- mono
- `16000 Hz`

That means the script receives raw PCM bytes and is responsible for wrapping
them as WAV or forwarding them to a cloud API if needed.

A minimal provider config looks like this:

```json
{
  "name": "elevenlabs",
  "type": "command",
  "command": "python3",
  "args": [
    "/usr/share/fcitx5-vinput/asr-providers/elevenlabs_speech_to_text.py"
  ],
  "env": {
    "ELEVENLABS_API_KEY": "..."
  },
  "timeout_ms": 60000
}
```

Built-in ASR provider scripts are installed under
`/usr/share/fcitx5-vinput/asr-providers/` by default. User overrides can be
placed under `~/.config/vinput/asr-providers/`; user files take precedence over
built-in scripts with the same script name. `command` should be the executable
or interpreter, and the script path should live in `args`.

### LLM Adaptor Contract

If you want to write your own LLM adaptor, it should expose an OpenAI-compatible
API and implement at least:

- `GET /v1/models`
- `POST /v1/chat/completions`

For `POST /v1/chat/completions`, `vinput` currently expects:

- non-streaming responses: `"stream": false`
- a standard OpenAI chat completion JSON envelope
- `choices[0].message.content` must be a string
- that string itself must be JSON in this shape:

```json
{
  "candidates": [
    "candidate 1",
    "candidate 2"
  ]
}
```

So the outer response is OpenAI-compatible, while the inner `content` string is
the structured payload currently consumed by `vinput`.

`GET /v1/models` only needs to return the usual OpenAI list shape, for example:

```json
{
  "object": "list",
  "data": [
    {
      "id": "my-proxy-model",
      "object": "model",
      "owned_by": "my-proxy"
    }
  ]
}
```

Built-in LLM adaptors are installed under
`/usr/share/fcitx5-vinput/llm-adaptors/` by default. User overrides can be
placed under `~/.config/vinput/llm-adaptors/`.

For built-in managed LLM adaptors, prefer environment variables over CLI
arguments for runtime configuration. `vinput adaptor start/stop` starts the
script directly and does not inject positional arguments.

Reference implementations:

- `data/llm-adaptors/mtranserver_proxy.py`
- `data/asr-providers/elevenlabs_speech_to_text.py`

## Configuration Files

| File | Path |
|------|------|
| Plugin config (keybindings, etc.) | `~/.config/fcitx5/conf/vinput.conf` |
| Core config (model, LLM, scenes) | `~/.config/vinput/config.json` |
| Model directory | `~/.local/share/fcitx5-vinput/models/` |

## Flatpak

In Flatpak, Vinput is installed as an Fcitx5 Add-on.

### Additional Permissions

The permissions for the Vinput Add-on depend on the Fcitx5 instance. So, after installation, the following commands need to be executed:

```bash
# Provide pipewire microphone access
flatpak override --user --filesystem=xdg-run/pipewire-0 org.fcitx.Fcitx5
# Enable vinput-daemon.service to be created
flatpak override --user --filesystem=xdg-config/systemd:create org.fcitx.Fcitx5
```

After execution, Fcitx5 must be restarted. For example:

```bash
flatpak kill org.fcitx.Fcitx5
```

### Flatpak Configuration Files

Flatpak configuration files are usually located in `.var/app`

| File | Path |
|------|------|
| Plugin config (keybindings, etc.) | `~/.var/app/org.fcitx.Fcitx5/config/fcitx5/conf/vinput.conf` |
| Core config (model, LLM, scenes) | `~/.var/app/org.fcitx.Fcitx5/config/vinput/config.json` |
| Model directory | `~/.var/app/org.fcitx.Fcitx5/data/vinput/models/` |

## Release

Push a tag (e.g. `v0.1.0`) and GitHub Actions will automatically build and publish:

- Source tarball `fcitx5-vinput-<version>.tar.gz`
- Ubuntu 24.04 `.deb`
- Debian 12 `.deb`
- Arch Linux `.pkg.tar.zst`
- Fedora COPR (`xifan/fcitx5-vinput-bin`)
- Ubuntu PPA (`ppa:xifan233/ppa`)
