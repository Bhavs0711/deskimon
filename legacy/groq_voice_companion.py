#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import base64
import io
import os
import re
import struct
import subprocess
import sys
import tempfile
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional

from bleak import BleakClient
from groq import BadRequestError, Groq

from mac_ble_agent import (
    DEFAULT_NAME,
    _decode_payload,
    find_target_device,
    resolve_characteristics,
)

DEFAULT_LLM_MODEL = "moonshotai/kimi-k2-instruct-0905"
DEFAULT_STT_MODEL = "whisper-large-v3"
DEFAULT_TTS_MODEL = "canopylabs/orpheus-v1-english"
DEFAULT_TTS_BACKEND = "edge"
DEFAULT_TTS_VOICE = "en-US-AnaNeural"
DEFAULT_RECORD_MS = 1600
MAX_REPLY_CHARS = 200
VOICE_CHUNK_BYTES = 90

SYSTEM_PROMPT = (
    "You are Bhavya, a tiny cute desk robot with expressive eyes. "
    "Reply warmly, playfully, and clearly. Keep every spoken reply short enough "
    "to fit in about one or two sentences, ideally under 180 characters. "
    "Do not use stage directions, roleplay actions, or asterisks. "
    "Only say the words that should be spoken aloud."
)


def _extract_field(text: str, name: str) -> Optional[str]:
    match = re.search(rf"{re.escape(name)}=([^ ]+)", text)
    return match.group(1) if match else None


def _emotion_for_text(text: str) -> str:
    lower = text.lower()
    if any(word in lower for word in ("sorry", "not sure", "unsure", "don't know", "cannot", "can't")):
        return "CONFUSED"
    if any(word in lower for word in ("wow", "amazing", "incredible", "surprise", "exciting")):
        return "WOW"
    if any(word in lower for word in ("love", "great", "awesome", "happy", "glad", "cute")):
        return "HAPPY"
    if "?" in text:
        return "WOW"
    return "FOCUSED"


def _tts_style_prefix(emotion: str) -> str:
    mapping = {
        "HAPPY": "[cheerful] ",
        "WOW": "[excited] ",
        "CONFUSED": "[gentle] ",
        "FOCUSED": "[warm] ",
    }
    return mapping.get(emotion, "[warm] ")


def _truncate_for_tts(text: str) -> str:
    clean = " ".join(text.strip().split())
    if len(clean) <= MAX_REPLY_CHARS:
        return clean

    for separator in (". ", "! ", "? "):
        head = clean.split(separator, 1)[0].strip()
        if 0 < len(head) <= MAX_REPLY_CHARS - 1:
            return head + separator.strip()

    return clean[: MAX_REPLY_CHARS - 3].rstrip() + "..."


def _pcm16_to_ulaw(sample: int) -> int:
    bias = 0x84
    if sample < 0:
        sample = -sample
        mask = 0x7F
    else:
        mask = 0xFF

    sample = min(sample, 32635)
    sample += bias

    segment = 0
    temp = sample >> 7
    while temp > 1 and segment < 7:
        segment += 1
        temp >>= 1

    return mask ^ ((segment << 4) | ((sample >> (segment + 3)) & 0x0F))


def _ulaw_to_pcm16(value: int) -> int:
    value = (~value) & 0xFF
    sign = -1 if (value & 0x80) else 1
    exponent = (value >> 4) & 0x07
    mantissa = value & 0x0F
    sample = (((mantissa << 3) + 0x84) << exponent) - 0x84
    return sign * sample


def _decode_pcm_frames(raw: bytes, sample_width: int, channels: int) -> list[int]:
    if sample_width not in (1, 2, 3, 4):
        raise RuntimeError(f"Unsupported WAV sample width: {sample_width}")

    frame_size = sample_width * channels
    if frame_size <= 0 or len(raw) % frame_size != 0:
        raise RuntimeError("Unexpected WAV frame layout.")

    samples: list[int] = []
    for frame_offset in range(0, len(raw), frame_size):
        channel_values: list[int] = []
        for channel_index in range(channels):
            start = frame_offset + (channel_index * sample_width)
            chunk = raw[start : start + sample_width]

            if sample_width == 1:
                value = (chunk[0] - 128) << 8
            elif sample_width == 2:
                value = struct.unpack_from("<h", chunk)[0]
            elif sample_width == 3:
                extended = chunk + (b"\xff" if (chunk[2] & 0x80) else b"\x00")
                value = struct.unpack("<i", extended)[0] >> 8
            else:
                value = struct.unpack_from("<i", chunk)[0] >> 16

            channel_values.append(value)

        samples.append(sum(channel_values) // len(channel_values))

    return samples


def _resample_samples(samples: list[int], src_rate: int, dst_rate: int) -> list[int]:
    if src_rate == dst_rate or len(samples) < 2:
        return samples[:]

    out_len = max(1, round(len(samples) * dst_rate / src_rate))
    result: list[int] = []
    for index in range(out_len):
        position = (index * (len(samples) - 1)) / max(1, out_len - 1)
        left = int(position)
        right = min(left + 1, len(samples) - 1)
        mix = position - left
        value = int((samples[left] * (1.0 - mix)) + (samples[right] * mix))
        result.append(max(-32768, min(32767, value)))
    return result


def _samples_to_pcm16_bytes(samples: list[int]) -> bytes:
    clipped = [max(-32768, min(32767, sample)) for sample in samples]
    return struct.pack("<" + ("h" * len(clipped)), *clipped)


def _trim_silence(samples: list[int], threshold: int = 480, pad_samples: int = 640) -> list[int]:
    if not samples:
        return samples

    start = 0
    end = len(samples)

    while start < end and abs(samples[start]) < threshold:
        start += 1

    while end > start and abs(samples[end - 1]) < threshold:
        end -= 1

    if start >= end:
        return samples

    start = max(0, start - pad_samples)
    end = min(len(samples), end + pad_samples)
    return samples[start:end]


def _normalize_samples(samples: list[int], target_peak: int = 24000) -> list[int]:
    if not samples:
        return samples

    peak = max(abs(sample) for sample in samples)
    if peak < 400:
        return samples

    gain = min(4.0, target_peak / peak)
    if gain <= 1.05:
        return samples

    return [max(-32768, min(32767, int(sample * gain))) for sample in samples]


def _ulaw_to_wav_bytes(payload: bytes) -> bytes:
    samples_8k = [_ulaw_to_pcm16(byte) for byte in payload]
    samples_8k = _trim_silence(samples_8k)
    samples_16k = _resample_samples(samples_8k, 8000, 16000)
    samples_16k = _normalize_samples(samples_16k)
    linear_16k = _samples_to_pcm16_bytes(samples_16k)

    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(16000)
        wav_file.writeframes(linear_16k)
    return buffer.getvalue()


def _wav_file_to_ulaw(path: str) -> bytes:
    with wave.open(path, "rb") as wav_file:
        channels = wav_file.getnchannels()
        sample_width = wav_file.getsampwidth()
        frame_rate = wav_file.getframerate()
        raw = wav_file.readframes(wav_file.getnframes())

    samples = _decode_pcm_frames(raw, sample_width, channels)
    samples_8k = _resample_samples(samples, frame_rate, 8000)
    return bytes(_pcm16_to_ulaw(sample) for sample in samples_8k)


@dataclass
class GroqVoiceConfig:
    llm_model: str
    stt_model: str
    tts_backend: str
    tts_model: str
    tts_voice: str


class BoardVoiceLink:
    def __init__(self, client: BleakClient, chars, settle: float, response_timeout: float) -> None:
        self.client = client
        self.chars = chars
        self.settle = settle
        self.response_timeout = response_timeout
        self._notifications: list[str] = []
        self._event = asyncio.Event()

    def _notification_handler(self, _: str, data: bytearray) -> None:
        text = _decode_payload(data)
        if text:
            self._notifications.append(text)
            self._event.set()

    async def connect(self) -> None:
        if self.chars.notify_uuid:
            await self.client.start_notify(self.chars.notify_uuid, self._notification_handler)
            await asyncio.sleep(0.2)

    async def close(self) -> None:
        if self.chars.notify_uuid:
            await self.client.stop_notify(self.chars.notify_uuid)

    async def send_command(self, command: str, *, timeout: Optional[float] = None) -> str:
        baseline = len(self._notifications)
        self._event.clear()

        await self.client.write_gatt_char(
            self.chars.write_uuid,
            command.encode("utf-8"),
            response=self.chars.write_with_response,
        )

        wait_timeout = self.response_timeout if timeout is None else timeout
        if self.chars.notify_uuid:
            try:
                await asyncio.wait_for(self._event.wait(), timeout=wait_timeout)
            except asyncio.TimeoutError:
                pass

        await asyncio.sleep(self.settle)

        latest = ""
        new_notifications = self._notifications[baseline:]
        for item in new_notifications:
            latest = item

        if self.chars.read_uuid:
            try:
                payload = await self.client.read_gatt_char(self.chars.read_uuid)
                text = _decode_payload(payload)
                if text:
                    latest = text
            except Exception:
                pass

        return latest

    async def set_emotion(self, command: str, hold_ms: Optional[int] = None) -> None:
        if hold_ms is None:
            await self.send_command(f"SET {command}")
        else:
            await self.send_command(f"HOLD {command} {hold_ms}")

    async def capture_ulaw(self, duration_ms: int) -> bytes:
        reply = await self.send_command(f"VOICE REC {duration_ms}")
        if not reply.startswith("OK VOICE REC"):
            raise RuntimeError(f"Board did not start capture: {reply or 'no reply'}")

        for _ in range(60):
            await asyncio.sleep(0.10)
            status = await self.send_command("VOICE STATUS")
            if "STATE=READY" in status:
                break
        else:
            raise RuntimeError("Timed out waiting for board capture to finish.")

        chunks = bytearray()
        while True:
            reply = await self.send_command("VOICE READ")
            if "OK VOICE EMPTY" in reply:
                break
            data = _extract_field(reply, "DATA")
            if not data:
                raise RuntimeError(f"Unexpected VOICE READ reply: {reply or 'no reply'}")
            chunks.extend(base64.b64decode(data))
            if _extract_field(reply, "MORE") == "0":
                break

        if not chunks:
            raise RuntimeError("No microphone audio was received from the board.")
        return bytes(chunks)

    async def play_ulaw(self, payload: bytes) -> None:
        reply = await self.send_command(f"VOICE PLAY BEGIN {len(payload)}")
        if not reply.startswith("OK VOICE PLAY BEGIN"):
            raise RuntimeError(f"Board did not accept playback begin: {reply or 'no reply'}")

        for offset in range(0, len(payload), VOICE_CHUNK_BYTES):
            chunk = payload[offset : offset + VOICE_CHUNK_BYTES]
            encoded = base64.b64encode(chunk).decode("ascii")
            reply = await self.send_command(f"VOICE PLAY DATA {encoded}")
            if not reply.startswith("OK VOICE PLAY DATA"):
                raise RuntimeError(f"Board rejected playback chunk: {reply or 'no reply'}")

        reply = await self.send_command("VOICE PLAY END", timeout=12.0)
        if not reply.startswith("OK VOICE PLAY END"):
            raise RuntimeError(f"Board did not finish playback cleanly: {reply or 'no reply'}")


class GroqVoiceCompanion:
    def __init__(self, client: Groq, config: GroqVoiceConfig) -> None:
        self.client = client
        self.config = config
        self.history: list[dict[str, str]] = []

    def transcribe(self, wav_bytes: bytes) -> str:
        with tempfile.NamedTemporaryFile(suffix=".wav") as temp_file:
            temp_file.write(wav_bytes)
            temp_file.flush()
            with open(temp_file.name, "rb") as audio_file:
                result = self.client.audio.transcriptions.create(
                    file=(Path(temp_file.name).name, audio_file.read()),
                    model=self.config.stt_model,
                    response_format="json",
                    temperature=0.0,
                    language="en",
                    prompt="Short casual English speech for a cute desk robot. Common phrases include hello, hi, hey, yes, no, okay, thanks, thank you.",
                )
        return (result.text or "").strip()

    def reply(self, user_text: str) -> str:
        messages = [{"role": "system", "content": SYSTEM_PROMPT}, *self.history, {"role": "user", "content": user_text}]
        stream = self.client.chat.completions.create(
            model=self.config.llm_model,
            messages=messages,
            temperature=1,
            max_completion_tokens=256,
            top_p=1,
            stream=True,
            stop=None,
        )

        parts: list[str] = []
        print("\nassistant> ", end="", flush=True)
        for chunk in stream:
            delta = chunk.choices[0].delta.content or ""
            if delta:
                print(delta, end="", flush=True)
                parts.append(delta)
        print()

        text = "".join(parts).strip()
        if not text:
            raise RuntimeError("Groq returned an empty assistant response.")

        self.history.extend(
            [
                {"role": "user", "content": user_text},
                {"role": "assistant", "content": text},
            ]
        )
        self.history = self.history[-8:]
        return text

    def synthesize_to_ulaw(self, text: str, emotion: str) -> bytes:
        spoken = _truncate_for_tts(text)
        if self.config.tts_backend == "edge":
            return self._synthesize_with_edge_tts(spoken)
        if self.config.tts_backend == "system":
            return self._synthesize_with_system_tts(spoken)

        styled = _tts_style_prefix(emotion) + spoken
        return self._synthesize_with_groq_tts(styled, spoken)

    def _synthesize_with_groq_tts(self, styled_text: str, plain_text: str) -> bytes:
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as temp_file:
            temp_path = temp_file.name

        try:
            try:
                groq_voice = self.config.tts_voice
                if "Neural" in groq_voice or "/" in groq_voice:
                    groq_voice = "hannah"
                response = self.client.audio.speech.create(
                    model=self.config.tts_model,
                    voice=groq_voice,
                    input=styled_text,
                    response_format="wav",
                )
            except BadRequestError as exc:
                message = str(exc)
                if "model_terms_required" not in message:
                    raise
                print("Groq TTS terms are not accepted for this org, falling back to Edge speech.")
                return self._synthesize_with_edge_tts(plain_text)
            response.write_to_file(temp_path)
            return _wav_file_to_ulaw(temp_path)
        finally:
            try:
                os.remove(temp_path)
            except FileNotFoundError:
                pass

    def _synthesize_with_edge_tts(self, text: str) -> bytes:
        with tempfile.NamedTemporaryFile(suffix=".mp3", delete=False) as mp3_file:
            mp3_path = mp3_file.name

        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as wav_file:
            wav_path = wav_file.name

        try:
            edge_cmd = [
                sys.executable,
                "-m",
                "edge_tts",
                "--voice",
                self.config.tts_voice,
                "--text",
                text,
                "--write-media",
                mp3_path,
            ]
            subprocess.run(edge_cmd, check=True, capture_output=True)

            ffmpeg_cmd = ["ffmpeg", "-y", "-i", mp3_path, "-ar", "16000", "-ac", "1", "-c:a", "pcm_s16le", wav_path]
            subprocess.run(ffmpeg_cmd, check=True, capture_output=True)
            return _wav_file_to_ulaw(wav_path)
        except subprocess.CalledProcessError as exc:
            stderr = exc.stderr.decode("utf-8", errors="ignore").strip() if exc.stderr else str(exc)
            raise RuntimeError(f"Edge speech synthesis failed: {stderr}") from exc
        finally:
            for path in (mp3_path, wav_path):
                try:
                    os.remove(path)
                except FileNotFoundError:
                    pass

    def _synthesize_with_system_tts(self, text: str) -> bytes:
        with tempfile.NamedTemporaryFile(suffix=".aiff", delete=False) as aiff_file:
            aiff_path = aiff_file.name

        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as wav_file:
            wav_path = wav_file.name

        try:
            try:
                say_cmd = ["say", "-v", self.config.tts_voice, "-o", aiff_path, text]
                subprocess.run(say_cmd, check=True, capture_output=True)
            except subprocess.CalledProcessError:
                fallback_cmd = ["say", "-o", aiff_path, text]
                subprocess.run(fallback_cmd, check=True, capture_output=True)

            afconvert_cmd = ["afconvert", "-f", "WAVE", "-d", "LEI16@16000", aiff_path, wav_path]
            subprocess.run(afconvert_cmd, check=True, capture_output=True)
            return _wav_file_to_ulaw(wav_path)
        except subprocess.CalledProcessError as exc:
            stderr = exc.stderr.decode("utf-8", errors="ignore").strip() if exc.stderr else str(exc)
            raise RuntimeError(f"System speech synthesis failed: {stderr}") from exc
        finally:
            for path in (aiff_path, wav_path):
                try:
                    os.remove(path)
                except FileNotFoundError:
                    pass


async def run_chat(args: argparse.Namespace) -> int:
    api_key = os.environ.get("GROQ_API_KEY")
    if not api_key:
        raise RuntimeError("Set GROQ_API_KEY before running the voice companion.")

    target = await find_target_device(args.name, args.address, args.timeout)
    print(f"Connecting to {target.name} [{target.address}]")

    groq_client = Groq(api_key=api_key)
    companion = GroqVoiceCompanion(
        groq_client,
        GroqVoiceConfig(
            llm_model=args.llm_model,
            stt_model=args.stt_model,
            tts_backend=args.tts_backend,
            tts_model=args.tts_model,
            tts_voice=args.tts_voice,
        ),
    )

    async with BleakClient(target.address, timeout=args.timeout) as client:
        if not client.is_connected:
            raise RuntimeError("BLE connection failed.")

        chars = resolve_characteristics(client)
        link = BoardVoiceLink(client, chars, args.settle, args.response_timeout)
        await link.connect()

        try:
            for turn_index in range(args.turns):
                print(f"\nturn {turn_index + 1}: speak into the T5-E1 mic now...")
                await link.set_emotion("FOCUSED")
                ulaw_audio = await link.capture_ulaw(args.record_ms)
                wav_bytes = _ulaw_to_wav_bytes(ulaw_audio)

                print("transcribing...")
                user_text = companion.transcribe(wav_bytes)
                if not user_text:
                    print("assistant> I didn't catch that.")
                    await link.set_emotion("CONFUSED", 1200)
                    continue

                print(f"user> {user_text}")
                await link.set_emotion("CALL", 1200)

                reply_text = companion.reply(user_text)
                emotion = _emotion_for_text(reply_text)
                await link.set_emotion(emotion, 1800 if emotion != "FOCUSED" else 1200)

                print("speaking through the board...")
                speech_ulaw = companion.synthesize_to_ulaw(reply_text, emotion)
                await link.play_ulaw(speech_ulaw)
                await link.set_emotion("IDLE")

            return 0
        finally:
            await link.close()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Groq-powered voice companion for the T5-E1 board.")
    sub = parser.add_subparsers(dest="command", required=True)

    chat = sub.add_parser("chat", help="Record from the board mic, run Groq STT/LLM/TTS, and play back on the board.")
    chat.add_argument("--name", default=DEFAULT_NAME, help="Advertised BLE name to target.")
    chat.add_argument("--address", help="Specific BLE address/identifier to connect to.")
    chat.add_argument("--timeout", type=float, default=10.0, help="Scan/connect timeout in seconds.")
    chat.add_argument("--settle", type=float, default=0.10, help="Delay after each BLE write before readback.")
    chat.add_argument("--response-timeout", type=float, default=2.0, help="How long to wait for a BLE reply.")
    chat.add_argument("--record-ms", type=int, default=DEFAULT_RECORD_MS, help="How long the board should record per turn.")
    chat.add_argument("--turns", type=int, default=1, help="How many conversation turns to run before exiting.")
    chat.add_argument("--llm-model", default=DEFAULT_LLM_MODEL, help="Groq chat model to use.")
    chat.add_argument("--stt-model", default=DEFAULT_STT_MODEL, help="Groq transcription model to use.")
    chat.add_argument(
        "--tts-backend",
        choices=("edge", "groq", "system"),
        default=DEFAULT_TTS_BACKEND,
        help="Use Edge speech by default, Groq TTS if your org has accepted model terms, or macOS speech as a fallback.",
    )
    chat.add_argument("--tts-model", default=DEFAULT_TTS_MODEL, help="Groq text-to-speech model to use.")
    chat.add_argument("--tts-voice", default=DEFAULT_TTS_VOICE, help="Voice name for the selected speech backend.")
    chat.set_defaults(func=run_chat)

    return parser


def main(argv: Optional[Iterable[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(list(argv) if argv is not None else None)
    return asyncio.run(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
