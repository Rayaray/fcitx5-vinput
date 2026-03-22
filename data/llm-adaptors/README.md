# LLM Adaptor 脚本

这个目录保存项目内置的 LLM adaptor 脚本。构建和安装时，会将它们复制到
运行时数据目录。adaptor 是本地的 OpenAI 兼容桥接进程，它和 `config.json`
里的 LLM provider 不同，后者指向场景实际调用的 API 端点。

- 内置安装路径：`/usr/share/fcitx5-vinput/llm-adaptors/`
- 用户覆盖路径：`~/.config/vinput/llm-adaptors/`
- 运行时状态路径：`${XDG_RUNTIME_DIR:-/tmp}/vinput/adaptors/`

可通过以下命令管理内置和用户 adaptor：

- `vinput adaptor list`
- `vinput adaptor start <id>`
- `vinput adaptor stop <id>`

运行时调用使用显式命令配置：

- `command`：可执行文件或解释器
- `args`：脚本路径和额外参数
- `env`：环境变量覆盖

元数据块格式如下：

```text
# ==vinput-adaptor==
# @name         MTranServer Proxy
# @description  为 MTranServer 提供 OpenAI 兼容代理
# @author       xifan
# @version      1.0.0
# ==/vinput-adaptor==
```
