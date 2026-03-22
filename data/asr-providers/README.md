# ASR Provider 脚本

这个目录保存项目内置的 ASR provider 脚本。构建和安装时，会将它们复制到
运行时数据目录。

- 内置安装路径：`/usr/share/fcitx5-vinput/asr-providers/`
- 用户覆盖路径：`~/.config/vinput/asr-providers/`

受管理的 provider 应使用显式命令配置：

- `command`：可执行文件或解释器
- `args`：脚本路径和额外参数
- `env`：环境变量覆盖

可选的元数据块格式如下：

```text
# ==vinput-asr-provider==
# @name         ElevenLabs Speech to Text
# @description  通过 ElevenLabs API 调用云端 ASR
# @author       xifan
# @version      1.0.0
# ==/vinput-asr-provider==
```
