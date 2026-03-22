#!/usr/bin/env python3
# ==vinput-asr-provider==
# @name ElevenLabs Speech to Text
# @description 通过 ElevenLabs API 调用云端 ASR
# @author xifan
# @version 1.0.0
# @env ELEVENLABS_API_KEY (required)
# @env ELEVENLABS_MODEL_ID (optional, default: scribe_v2)
# @env ELEVENLABS_LANGUAGE (optional)
# @env ELEVENLABS_URL (optional)
# @env ELEVENLABS_TIMEOUT (optional)
# ==/vinput-asr-provider==
"""vinput 的 ElevenLabs 语音转文字命令式 provider。

从 stdin 读取一段原始 PCM S16_LE 16 kHz 单声道音频，并将最终识别文本
写入 stdout。

环境变量：
    ELEVENLABS_API_KEY      必填，API Key。
    ELEVENLABS_MODEL_ID     可选，默认值为 "scribe_v2"。
    ELEVENLABS_LANGUAGE     可选，ISO-639 语言代码。
    ELEVENLABS_URL          可选，自定义接口地址。
    ELEVENLABS_TIMEOUT      可选，请求超时时间，单位为秒。

示例：
    ELEVENLABS_API_KEY=... \
    python3 data/asr-providers/elevenlabs_speech_to_text.py
"""

import json
import os
import sys
import uuid
from typing import Dict, Iterable, Optional, Tuple
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

DEFAULT_MODEL_ID = "scribe_v2"
DEFAULT_TIMEOUT = 60
DEFAULT_URL = "https://api.elevenlabs.io/v1/speech-to-text"


def env_flag(name: str, default: bool) -> bool:
    value = os.getenv(name)
    if value is None:
        return default
    return value.strip().lower() not in {"0", "false", "no", "off"}


def build_multipart(
    fields: Iterable[Tuple[str, str]],
    files: Iterable[Tuple[str, str, str, bytes]],
) -> Tuple[bytes, str]:
    boundary = f"----vinput-{uuid.uuid4().hex}"
    body = bytearray()

    for name, value in fields:
        body.extend(f"--{boundary}\r\n".encode())
        body.extend(
            f'Content-Disposition: form-data; name="{name}"\r\n\r\n'.encode()
        )
        body.extend(value.encode())
        body.extend(b"\r\n")

    for field_name, filename, content_type, content in files:
        body.extend(f"--{boundary}\r\n".encode())
        body.extend(
            (
                f'Content-Disposition: form-data; name="{field_name}"; '
                f'filename="{filename}"\r\n'
            ).encode()
        )
        body.extend(f"Content-Type: {content_type}\r\n\r\n".encode())
        body.extend(content)
        body.extend(b"\r\n")

    body.extend(f"--{boundary}--\r\n".encode())
    return bytes(body), boundary


def parse_error_payload(payload: bytes) -> str:
    text = payload.decode("utf-8", errors="replace").strip()
    if not text:
        return "Empty error response from ElevenLabs."

    try:
        data = json.loads(text)
    except json.JSONDecodeError:
        return text

    if isinstance(data, dict):
        detail = data.get("detail")
        if isinstance(detail, dict):
            message = detail.get("message")
            if isinstance(message, str) and message.strip():
                return message.strip()
        if isinstance(detail, str) and detail.strip():
            return detail.strip()
        message = data.get("message")
        if isinstance(message, str) and message.strip():
            return message.strip()
    return text


def transcribe(
    pcm_audio: bytes,
    api_key: str,
    model_id: str,
    language_code: Optional[str],
    timeout: int,
    endpoint: str,
    enable_logging: bool,
    tag_audio_events: bool,
) -> str:
    query = urlencode({"enable_logging": str(enable_logging).lower()})
    url = endpoint
    if query:
        url = f"{endpoint}?{query}"

    fields = [
        ("model_id", model_id),
        ("file_format", "pcm_s16le_16"),
        ("tag_audio_events", str(tag_audio_events).lower()),
    ]
    if language_code:
        fields.append(("language_code", language_code))

    body, boundary = build_multipart(
        fields=fields,
        files=[("file", "audio.pcm", "application/octet-stream", pcm_audio)],
    )

    request = Request(
        url,
        data=body,
        headers={
            "xi-api-key": api_key,
            "Content-Type": f"multipart/form-data; boundary={boundary}",
            "Accept": "application/json",
        },
        method="POST",
    )

    with urlopen(request, timeout=timeout) as response:
        data = json.loads(response.read())

    text = data.get("text")
    if not isinstance(text, str) or not text.strip():
        raise RuntimeError("ElevenLabs returned an empty transcript.")
    return text.strip()


def main() -> int:
    api_key = os.getenv("ELEVENLABS_API_KEY", "").strip()
    if not api_key:
        print("Missing ELEVENLABS_API_KEY.", file=sys.stderr)
        return 2

    model_id = os.getenv("ELEVENLABS_MODEL_ID", DEFAULT_MODEL_ID).strip()
    if not model_id:
        model_id = DEFAULT_MODEL_ID

    language_code = os.getenv("ELEVENLABS_LANGUAGE", "").strip() or None
    endpoint = os.getenv("ELEVENLABS_URL", DEFAULT_URL).strip() or DEFAULT_URL
    timeout = int(os.getenv("ELEVENLABS_TIMEOUT", str(DEFAULT_TIMEOUT)))
    enable_logging = env_flag("ELEVENLABS_ENABLE_LOGGING", True)
    tag_audio_events = env_flag("ELEVENLABS_TAG_AUDIO_EVENTS", False)

    pcm_audio = sys.stdin.buffer.read()
    if not pcm_audio:
        print("No audio received on stdin.", file=sys.stderr)
        return 2

    try:
        text = transcribe(
            pcm_audio=pcm_audio,
            api_key=api_key,
            model_id=model_id,
            language_code=language_code,
            timeout=timeout,
            endpoint=endpoint,
            enable_logging=enable_logging,
            tag_audio_events=tag_audio_events,
        )
    except HTTPError as exc:
        payload = exc.read()
        message = parse_error_payload(payload)
        print(f"ElevenLabs HTTP {exc.code}: {message}", file=sys.stderr)
        return 1
    except URLError as exc:
        print(f"Failed to reach ElevenLabs: {exc}", file=sys.stderr)
        return 1
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        return 1

    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
