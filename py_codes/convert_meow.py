"""
convert_meow.py — meow-sound/*.wav 를 앱 리소스용 16-bit PCM wav 로 변환해 assets/sounds/ 에 저장

Win32 PlaySound 는 WAVE_FORMAT_PCM 의 24-bit 재생이 보장되지 않으므로 모두 16-bit 로 맞춘다.
샘플레이트 / 채널 수는 그대로 둔다. 원본 폴더는 건드리지 않는다.

사용: .venv\\Scripts\\python py_codes/convert_meow.py [입력폴더] [출력폴더]
"""
import os
import sys
import wave

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read_pcm(path):
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
    return data, ch, rate, width


def to_int16(data32):
    # 반올림 후 16bit 로 (클리핑 방지)
    x = np.round(data32.astype(np.float64) / 65536.0)
    return np.clip(x, -32768, 32767).astype("<i2")


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "meow-sound")
    dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "assets", "sounds")
    os.makedirs(dst, exist_ok=True)
    for name in sorted(os.listdir(src)):
        if not name.lower().endswith(".wav"):
            continue
        data, ch, rate, width = read_pcm(os.path.join(src, name))
        out = to_int16(data)
        with wave.open(os.path.join(dst, name), "wb") as w:
            w.setnchannels(ch)
            w.setsampwidth(2)
            w.setframerate(rate)
            w.writeframes(out.tobytes())
        print(f"{name}: {width * 8}bit {ch}ch {rate}Hz {len(out) // ch / rate:.2f}s -> 16bit")


if __name__ == "__main__":
    main()
