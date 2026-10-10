"""
trim_meow.py — meow-sound/*.wav 의 끝부분을 잘라내고 짧은 페이드아웃을 적용

  - 끝에서 TRIM_SEC(0.01초) 만큼 잘라냄
  - 잘린 끝의 마지막 FADE_SEC(5ms)를 선형으로 줄여 클릭음 방지
  - 결과는 meow-sound/trimmed/ 에 같은 이름으로 저장 (원본은 유지)
  - 16bit / 24bit PCM WAV 지원

사용: .venv/Scripts/python py_codes/trim_meow.py [입력폴더] [출력폴더]
"""
import os
import sys
import glob
import wave
import numpy as np

TRIM_SEC = 0.01
FADE_SEC = 0.005

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_IN = os.path.join(ROOT, "meow-sound")


def bytes_to_samples(raw, sampwidth):
    """PCM 바이트 → int32 샘플 배열"""
    if sampwidth == 2:
        return np.frombuffer(raw, dtype="<i2").astype(np.int32)
    if sampwidth == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        return np.where(v & 0x800000, v - 0x1000000, v)
    raise ValueError(f"지원하지 않는 샘플 폭: {sampwidth * 8}bit")


def samples_to_bytes(samples, sampwidth):
    """int32 샘플 배열 → PCM 바이트"""
    if sampwidth == 2:
        return samples.astype("<i2").tobytes()
    v = samples.astype(np.int32) & 0xFFFFFF
    b = np.stack([v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF], axis=1)
    return b.astype(np.uint8).tobytes()


def trim_file(src, dst):
    with wave.open(src, "rb") as w:
        params = w.getparams()
        raw = w.readframes(params.nframes)

    ch, sw, sr = params.nchannels, params.sampwidth, params.framerate
    trim = int(round(TRIM_SEC * sr))
    fade = int(round(FADE_SEC * sr))

    data = bytes_to_samples(raw, sw).reshape(-1, ch)
    if len(data) <= trim + fade:
        raise ValueError(f"파일이 너무 짧음: {len(data)} 프레임")
    data = data[:-trim]

    # 마지막 fade 프레임에 1 → 0 선형 램프 적용
    ramp = np.linspace(1.0, 0.0, fade)[:, None]
    data[-fade:] = np.round(data[-fade:] * ramp).astype(np.int32)

    with wave.open(dst, "wb") as w:
        w.setnchannels(ch)
        w.setsampwidth(sw)
        w.setframerate(sr)
        w.writeframes(samples_to_bytes(data.reshape(-1), sw))

    return params.nframes, len(data)


def main():
    in_dir = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_IN
    out_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(in_dir, "trimmed")
    os.makedirs(out_dir, exist_ok=True)

    files = sorted(glob.glob(os.path.join(in_dir, "*.wav")))
    if not files:
        print(f"WAV 파일 없음: {in_dir}")
        return

    for src in files:
        dst = os.path.join(out_dir, os.path.basename(src))
        before, after = trim_file(src, dst)
        print(f"{os.path.basename(src)}: {before} → {after} 프레임  → {dst}")


if __name__ == "__main__":
    main()
