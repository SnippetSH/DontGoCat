"""
convert_meow.py — meow-sound/ 의 wav / mp3 를 앱 리소스용 16-bit PCM wav 로 변환해 assets/sounds/ 에 저장

Win32 PlaySound 는 WAVE_FORMAT_PCM 의 24-bit 재생이 보장되지 않고 mp3 는 재생하지 못하므로 모두 16-bit wav 로 맞춘다.
샘플레이트 / 채널 수는 그대로 둔다. 원본 폴더는 건드리지 않는다.
  - mp3 는 miniaudio(.venv 에 설치)로 디코딩한다.
  - 앞 무음이 LEAD_TRIM_MIN_SEC 보다 길면 소리 시작 LEAD_KEEP_SEC 전까지 잘라내고 FADE_IN_SEC 페이드인
    (잡는 순간 바로 소리가 나도록. 기존 meow1~4 는 앞 무음이 짧아 그대로 유지된다)

사용: .venv\\Scripts\\python py_codes/convert_meow.py [입력폴더] [출력폴더]
"""
import os
import sys
import wave

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SILENCE_LEVEL = 300          # 16-bit 기준, 이보다 작으면 무음으로 본다
LEAD_TRIM_MIN_SEC = 0.05     # 앞 무음이 이보다 길 때만 자른다
LEAD_KEEP_SEC = 0.01         # 소리 시작 전 남겨 둘 여유
FADE_IN_SEC = 0.005


def read_wav(path):
    with wave.open(path, "rb") as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if width == 2:
        data = np.frombuffer(raw, dtype="<i2").astype(np.int32) << 16
    elif width == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        data = (b[:, 0] << 8) | (b[:, 1] << 16) | (b[:, 2] << 24)   # 상위 24bit 에 채워 부호 유지
    elif width == 4:
        data = np.frombuffer(raw, dtype="<i4").astype(np.int32)
    else:
        raise ValueError(f"{path}: 지원하지 않는 샘플 폭 {width * 8}bit")
    return to_int16(data), ch, rate, width * 8


def read_mp3(path):
    import miniaudio   # mp3 가 있을 때만 필요

    d = miniaudio.decode_file(path, output_format=miniaudio.SampleFormat.SIGNED16)
    return np.frombuffer(d.samples, dtype="<i2").copy(), d.nchannels, d.sample_rate, 16


def to_int16(data32):
    # 반올림 후 16bit 로 (클리핑 방지)
    x = np.round(data32.astype(np.float64) / 65536.0)
    return np.clip(x, -32768, 32767).astype("<i2")


def trim_lead(samples, ch, rate):
    frames = samples.reshape(-1, ch)
    loud = np.nonzero(np.abs(frames.astype(np.int32)).max(axis=1) > SILENCE_LEVEL)[0]
    if len(loud) == 0 or loud[0] < LEAD_TRIM_MIN_SEC * rate:
        return samples, 0.0
    start = max(0, loud[0] - int(LEAD_KEEP_SEC * rate))
    out = frames[start:].astype(np.float64)
    fade = min(len(out), int(FADE_IN_SEC * rate))
    out[:fade] *= np.linspace(0.0, 1.0, fade)[:, None]
    return np.round(out).astype("<i2").reshape(-1), start / rate


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "meow-sound")
    dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "assets", "sounds")
    os.makedirs(dst, exist_ok=True)
    for name in sorted(os.listdir(src)):
        stem, ext = os.path.splitext(name)
        if ext.lower() == ".wav":
            out, ch, rate, bits = read_wav(os.path.join(src, name))
        elif ext.lower() == ".mp3":
            out, ch, rate, bits = read_mp3(os.path.join(src, name))
        else:
            continue
        out, cut = trim_lead(out, ch, rate)
        with wave.open(os.path.join(dst, stem + ".wav"), "wb") as w:
            w.setnchannels(ch)
            w.setsampwidth(2)
            w.setframerate(rate)
            w.writeframes(out.tobytes())
        note = f", 앞 무음 {cut:.2f}s 제거" if cut else ""
        print(f"{name}: {bits}bit {ch}ch {rate}Hz {len(out) // ch / rate:.2f}s -> {stem}.wav 16bit{note}")


if __name__ == "__main__":
    main()
