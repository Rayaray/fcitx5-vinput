<div align="center">

# fcitx5-vinput

**基于 Fcitx5 的本地离线语音输入插件**

[![License](https://img.shields.io/github/license/xifan2333/fcitx5-vinput)](LICENSE)
[![CI](https://github.com/xifan2333/fcitx5-vinput/actions/workflows/ci.yml/badge.svg)](https://github.com/xifan2333/fcitx5-vinput/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/xifan2333/fcitx5-vinput)](https://github.com/xifan2333/fcitx5-vinput/releases)
[![AUR](https://img.shields.io/aur/version/fcitx5-vinput-bin)](https://aur.archlinux.org/packages/fcitx5-vinput-bin)
[![Downloads](https://img.shields.io/github/downloads/xifan2333/fcitx5-vinput/total)](https://github.com/xifan2333/fcitx5-vinput/releases)

[English](README.md) | [中文](README_zh.md)

https://github.com/user-attachments/assets/5a548a68-153c-4842-bab6-926f30bb720e

</div>

基于 [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) 实现本地离线语音识别，支持通过任意 OpenAI 兼容 API 进行 LLM 后处理。

## 功能特性

- **两种触发模式** — 短按切换录音开关，长按即说即停（push-to-talk）
- **命令模式** — 选中文本，语音指令直接修改
- **LLM 后处理** — 纠错、格式化、翻译等
- **场景管理** — 运行时随时切换后处理 prompt
- **多 LLM provider** — 配置多个服务端，并按场景绑定
- **热词支持**（部分模型）
- **`vinput` CLI** — 在终端管理模型、场景、provider 和 daemon 状态

## 安装

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

### Ubuntu / Debian（手动安装）

```bash
# 从 GitHub Releases 下载最新 .deb
sudo dpkg -i fcitx5-vinput_*.deb
sudo apt-get install -f
```

### 从源码编译

**依赖：** `cmake` `fcitx5` `pipewire` `libcurl` `nlohmann-json` `CLI11` `Qt6`

```bash
sudo bash scripts/build-sherpa-onnx.sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

如果你使用 `just`，仓库里现在也提供了一层很薄的命令包装，底层仍然是同一套
CMake / 脚本入口：

```bash
just sherpa
just release
just build
sudo just install
```

第一步会下载本地构建和发布打包所用的预编译 `sherpa-onnx` 运行库。
这些运行库会随安装产物一起打包，而不是作为单独的系统依赖项声明。
从源码构建时现在默认安装到 Fcitx5 的系统前缀（`/usr`），这样 addon
会落到 Fcitx5 会扫描的目录里。若你之前的旧构建已经安装到 `/usr/local`，
请清理旧 build 目录后重新安装，让 `vinput.conf` 和 `fcitx5-vinput.so`
进入 Fcitx5 的系统路径。

## 快速开始

**1. 安装模型**

```bash
vinput model list --remote      # 浏览可用模型
vinput model add <模型名>        # 下载并安装
vinput model use <模型名>        # 设置为当前模型
```

也可手动将模型目录放到 `~/.local/share/vinput/models/<模型名>/`，目录内需包含：
- `vinput-model.json`
- `model.int8.onnx` 或 `model.onnx`
- `tokens.txt`

**2. 启动守护进程**

```bash
systemctl --user enable --now vinput-daemon.service
```

**3. 在 Fcitx5 中启用**

打开 Fcitx5 配置 → 附加组件 → 找到 **Vinput** → 启用。

**4. 开始使用**

- **短按** `Alt_R` 开始录音，再按一次停止并识别
- **长按** `Alt_R` 录音，松开自动识别上屏（push-to-talk）

## 按键说明

| 按键 | 默认 | 功能 |
|------|------|------|
| 触发键 | `Alt_R` | 短按切换录音；长按即说即停 |
| 命令键 | `Control_R` | 选中文本后按住，语音指令修改选中内容 |
| 场景菜单键 | `Shift_R` | 打开场景切换菜单 |
| 翻页 | `Page Up` / `Page Down` | 候选列表翻页 |
| 移动 | `↑` / `↓` | 候选列表移动光标 |
| 确认 | `Enter` | 确认选中候选 |
| 取消 | `Esc` | 关闭菜单 |
| 快选 | `1`–`9` | 快速选择候选 |

所有按键均可在 Fcitx5 配置界面中自定义。

## 配置

### 图形界面

```bash
vinput-gui
```

或在 Fcitx5 配置中打开 Vinput 附加组件。

### CLI 参考

完整当前语法可使用 `vinput --help` 或 `vinput <子命令> --help` 查看。

> 如果要在 Flatpak 安装下使用 Vinput CLI，请使用 `flatpak run --command=/app/addons/Vinput/bin/vinput org.fcitx.Fcitx5 --help`

<details>
<summary>模型管理</summary>

```bash
vinput model list               # 列出已安装模型
vinput model list --remote      # 列出可用远程模型
vinput model add <名称>          # 下载安装模型
vinput model use <名称>          # 切换当前模型
vinput model remove <名称>       # 删除模型
vinput model info <名称>         # 查看模型详情
```

</details>

<details>
<summary>场景管理</summary>

```bash
vinput scene list               # 列出所有场景
vinput scene add --id <id>      # 添加场景
vinput scene use <ID>           # 切换当前场景
vinput scene remove <ID>        # 删除场景
```

如果场景要调用 LLM，`--provider`、`--model`、`--prompt` 需要同时提供。

</details>

<details>
<summary>LLM 配置</summary>

```bash
vinput llm list                 # 列出已配置 provider
vinput llm add <名称> --base-url <url>
vinput llm remove <名称>         # 删除 provider
vinput adaptor list             # 列出内建/用户 LLM adaptor
vinput adaptor start <id>       # 启动 LLM adaptor
vinput adaptor stop <id>        # 停止 LLM adaptor
```

LLM provider 由场景引用，不再有单独的“当前 provider”开关。LLM adaptor
只是本地 OpenAI 兼容桥接进程，provider 可以指向它。

</details>

<details>
<summary>ASR Provider</summary>

```bash
vinput asr list                 # 列出已配置 ASR provider
vinput asr add <名称>            # 添加 builtin 或 command provider
vinput asr use <名称>            # 切换当前 ASR provider
vinput asr edit <名称>           # 编辑外部 provider 脚本
vinput asr remove <名称>         # 删除 provider
```

内建 ASR provider 脚本可直接用脚本 ID 引用，不必手写完整路径。

</details>

<details>
<summary>热词管理</summary>

```bash
vinput hotword get              # 查看当前热词文件路径
vinput hotword set <路径>        # 设置热词文件
vinput hotword edit             # 用编辑器打开热词文件
vinput hotword clear            # 清除热词文件配置
```

</details>

<details>
<summary>设备管理</summary>

```bash
vinput device list              # 列出录音设备
vinput device use <名称>         # 设置当前设备
```

</details>

<details>
<summary>配置辅助</summary>

```bash
vinput config set extra.hotwords_file <路径>  # 写入支持的配置项
vinput config edit extra                     # 编辑核心配置文件
vinput config edit fcitx                     # 编辑 Fcitx 插件配置
```

高级注册源回退可直接在 `config.json` 里配置 `registry.sources`。
程序会按顺序依次尝试，直到某个源成功。

</details>

<details>
<summary>录音控制</summary>

```bash
vinput recording start              # 开始录音
vinput recording stop               # 停止录音并识别
vinput recording stop --scene <ID>  # 使用指定场景停止录音
vinput recording toggle             # 切换录音开始/停止
vinput recording toggle --scene <ID># 使用指定场景切换录音
```

</details>

<details>
<summary>守护进程管理</summary>

```bash
vinput daemon start             # 启动
vinput daemon stop              # 停止
vinput daemon restart           # 重启
vinput daemon logs              # 查看日志
vinput status                   # 查看整体状态
vinput init                     # 创建默认配置和模型目录
```

</details>

## 场景

场景控制 LLM 对识别结果的处理方式，通过场景菜单键在运行时切换。

每个场景包含：
- **ID** — 唯一标识符
- **标签** — 菜单中显示的名称
- **Prompt** — 发送给 LLM 的系统提示

只有同时配置了 `provider + model + prompt` 的场景才会调用 LLM。
内置的 `__raw__` 场景会完全跳过 LLM。

## 命令模式

选中文本 → 按住命令键 → 说出指令 → 松开 → 完成。

如果当前没有可用的 surrounding-text 选区，命令模式会回退到当前的
primary selection 剪贴板内容。这样操作上更顺手，但也意味着如果 primary
selection 里残留的是旧文本或无关文本，它也可能被送入命令改写流程。

**示例：**
- 选中中文 → 说 *"翻译成英文"* → 替换为英文译文
- 选中代码 → 说 *"加上注释"* → 替换为加注释版本

> 需要先配置并启用 LLM。

## LLM 配置示例

以本地 [Ollama](https://ollama.com) 为例：

```bash
vinput llm add ollama --base-url http://127.0.0.1:11434/v1
vinput scene add --id polish \
  --label "Polish" \
  --provider ollama \
  --model qwen2.5:7b \
  --prompt "将识别结果润色为自然的中文。"
vinput scene use polish
```

## Provider 脚本与 Adaptor 约定

仓库里的可选集成脚本现在统一放在 `data/` 下的两个平铺目录：

- `data/asr-providers/`：外部 ASR provider 脚本
- `data/llm-adaptors/`：LLM OpenAI 兼容 adaptor 脚本

`scripts/` 目录只保留构建、检查、打包之类的项目维护脚本。

### ASR command provider 协议

外部 ASR 脚本应遵循传统 Unix 命令语义：

- `stdin`：一段完整语音的原始音频流
- `stdout`：最终识别文本
- `stderr`：可读的错误信息
- 退出码 `0`：成功
- 非 `0` 退出码：失败

当前 `vinput` 传给 command provider 的音频格式是：

- PCM `S16_LE`
- 单声道
- `16000 Hz`

也就是脚本收到的是裸 PCM 字节流，需要脚本自己决定是否封装成 WAV 或转发到云接口。

一个最小的 provider 配置示例：

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

内建 ASR provider 脚本默认安装到
`/usr/share/fcitx5-vinput/asr-providers/`。用户覆盖脚本放到
`~/.config/vinput/asr-providers/` 即可；同名文件会优先覆盖内建脚本。
`command` 应该填写可执行命令或解释器，脚本路径放在 `args` 里。

### LLM Adaptor 协议

如果你要自己写 LLM adaptor，它需要提供 OpenAI 兼容接口，至少实现：

- `GET /v1/models`
- `POST /v1/chat/completions`

`vinput` 当前对 `POST /v1/chat/completions` 的要求是：

- 非流式：`"stream": false`
- 返回标准的 OpenAI chat completion JSON
- `choices[0].message.content` 必须是一个字符串
- 这个字符串本身必须是 JSON，格式如下：

```json
{
  "candidates": [
    "候选结果 1",
    "候选结果 2"
  ]
}
```

也就是说，外层是 OpenAI 兼容响应，内层 `content` 是 `vinput` 目前消费的结构化 JSON 字符串。

`GET /v1/models` 只需要返回 OpenAI 常见格式即可，例如：

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

内建 LLM adaptor 默认安装到 `/usr/share/fcitx5-vinput/llm-adaptors/`。
用户覆盖脚本放到 `~/.config/vinput/llm-adaptors/` 即可。

对于内建托管的 LLM adaptor，运行时配置建议统一走环境变量，不要依赖 CLI
位置参数。`vinput adaptor start/stop` 直接启动脚本，不会为它注入额外位置参数。

参考实现：

- `data/llm-adaptors/mtranserver_proxy.py`
- `data/asr-providers/elevenlabs_speech_to_text.py`

## 配置文件位置

| 文件 | 路径 |
|------|------|
| Fcitx5 插件配置（按键等） | `~/.config/fcitx5/conf/vinput.conf` |
| 核心配置（模型、LLM、场景） | `~/.config/vinput/config.json` |
| 模型目录 | `~/.local/share/fcitx5-vinput/models/` |

## Flatpak

Flatpak 下，Vinput 是作为 Fcitx5 Add-on 的形式安装的

### 额外的权限

Vinput Add-on 的权限取决于 Fcitx5 本体，因此安装后需要执行以下命令，来为 Fcitx5 提供权限

```bash
# 提供基于 pipewire 的麦克风访问
flatpak override --user --filesystem=xdg-run/pipewire-0 org.fcitx.Fcitx5
# 让 vinput-daemon.service 能被创建
flatpak override --user --filesystem=xdg-config/systemd:create org.fcitx.Fcitx5
```

执行后，必须重启 Fcitx5。可以：

```bash
flatpak kill org.fcitx.Fcitx5
```

### Flatpak 配置文件位置

Flatpak 下配置文件通常在 `.var/app` 中

| 文件 | 路径 |
|------|------|
| Fcitx5 插件配置（按键等） | `~/.var/app/org.fcitx.Fcitx5/config/fcitx5/conf/vinput.conf` |
| 核心配置（模型、LLM、场景） | `~/.var/app/org.fcitx.Fcitx5/config/vinput/config.json` |
| 模型目录 | `~/.var/app/org.fcitx.Fcitx5/data/vinput/models/` |

## 打包发布

推送形如 `v0.1.0` 的 tag 后，GitHub Actions 会自动构建并发布：

- 源码包 `fcitx5-vinput-<version>.tar.gz`
- Ubuntu 24.04 `.deb`
- Debian 12 `.deb`
- Arch Linux `.pkg.tar.zst`
- Fedora COPR (`xifan/fcitx5-vinput-bin`)
- Ubuntu PPA (`ppa:xifan233/ppa`)
