"""
meow_synth.py — 포먼트 합성(소스-필터 모델) 기반 고양이 울음소리 생성기

원리
  - 소스(성대): 기본 주파수 f0(t)를 갖는 하모닉 계열 (k * f0)
  - 필터(성도): 시간에 따라 움직이는 포먼트 F1~F4의 캐스케이드 공명 응답
  - 각 하모닉의 진폭 = 소스 기울기(1/k^tilt) × 포먼트 응답(k*f0 위치)
  - "m → i → a → u" 순으로 포먼트를 이동시키면 "야-옹(me-ow)" 모양이 나옴

per-sample IIR 필터 대신 '하모닉 가산 합성'으로 구현 → 완전 벡터화, 에일리어싱 없음

사용: python3 meow_synth.py [출력폴더]
"""
import os
import sys
import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, sosfilt

SR = 44100
rng = np.random.default_rng(7)  # 재현성 고정 (값 바꾸면 미세 변주)

# ── 모음 프리셋: 고양이는 성도가 짧아서 사람보다 포먼트가 1.5~2배 높음 ──
#            F1     F2     F3     F4
VOWEL = {
    "m": (450, 1500, 3400, 4800),  # 입 닫힘(비음 시작)
    "i": (900, 2700, 4200, 5600),  # "이/에" — 입 살짝 벌림
    "a": (1350, 2200, 3900, 5400),  # "아" — 최대 개방
    "u": (700, 1300, 3600, 5000),  # "우/오" — 입 오므림
}
BANDWIDTH = np.array([220.0, 260.0, 380.0, 500.0])  # 포먼트 대역폭(Hz)


def interp_curve(t_norm, keys):
    """keys: [(시간비율 0~1, 값), ...] → t_norm 위치에서 선형 보간"""
    xs, ys = zip(*keys)
    return np.interp(t_norm, xs, ys)


def formant_tracks(t_norm, vowel_keys):
    """vowel_keys: [(시간비율, '모음'), ...] → (4, N) 포먼트 궤적"""
    xs = [k[0] for k in vowel_keys]
    tracks = []
    for i in range(4):
        ys = [VOWEL[k[1]][i] for k in vowel_keys]
        tracks.append(np.interp(t_norm, xs, ys))
    return np.array(tracks)


def cascade_response(freq, formants):
    """
    2차 공명기 캐스케이드의 크기 응답 (DC 이득 = 1로 정규화)
    |H(f)| = Π Fi^2 / sqrt((Fi^2 - f^2)^2 + (Bi*f)^2)
    """
    resp = np.ones_like(freq)
    for i in range(4):
        Fi = formants[i]
        Bi = BANDWIDTH[i]
        resp *= Fi**2 / np.sqrt((Fi**2 - freq**2) ** 2 + (Bi * freq) ** 2)
    return resp


def synth_meow(
    duration,
    f0_keys,
    vowel_keys,
    amp_keys,
    vibrato_hz=6.0,
    vibrato_depth=0.015,
    jitter=0.006,
    tilt=1.1,
    breath=0.04,
    trill=None,
):
    n = int(duration * SR)
    t = np.arange(n) / SR
    tn = t / duration  # 0~1 정규화 시간

    # ── 1) f0 궤적: 키포인트 + 비브라토 + 랜덤 지터(저역통과 노이즈) ──
    f0 = interp_curve(tn, f0_keys)
    f0 *= 1.0 + vibrato_depth * np.sin(2 * np.pi * vibrato_hz * t)
    jit = rng.standard_normal(n)
    jit = sosfilt(butter(2, 30, fs=SR, output="sos"), jit)  # 30Hz 이하로 완만하게
    jit /= np.max(np.abs(jit)) + 1e-9
    f0 *= 1.0 + jitter * jit

    phase = 2 * np.pi * np.cumsum(f0) / SR  # 위상 적분 → 피치 변화가 연속적

    # ── 2) 포먼트 궤적 ──
    F = formant_tracks(tn, vowel_keys)

    # ── 3) 하모닉 가산 합성 ──
    sig = np.zeros(n)
    k_max = int(0.45 * SR / f0.min())
    for k in range(1, k_max + 1):
        fk = k * f0
        valid = fk < 0.45 * SR  # 나이퀴스트 근처는 버려서 에일리어싱 방지
        amp = cascade_response(fk, F) / (k**tilt)
        amp *= valid
        # 하모닉마다 미세한 위상 오프셋 → 버지(buzz) 느낌 완화
        sig += amp * np.sin(k * phase + rng.uniform(0, 2 * np.pi))

    # ── 4) 숨소리: 고역 노이즈를 섞어 '바람 새는' 질감 추가 ──
    noise = rng.standard_normal(n)
    noise = sosfilt(butter(2, [2500, 7000], btype="band", fs=SR, output="sos"), noise)
    sig = sig / (np.max(np.abs(sig)) + 1e-9) + breath * noise

    # ── 5) 진폭 엔벨로프 (+ 옵션: 골골/트릴 AM) ──
    env = interp_curve(tn, amp_keys)
    env = sosfilt(butter(1, 40, fs=SR, output="sos"), env)  # 꺾임 부드럽게
    if trill is not None:
        rate, depth, end = trill  # (Hz, 깊이 0~1, 적용 구간 끝 비율)
        am = 1 - depth * (0.5 + 0.5 * np.sin(2 * np.pi * rate * t))
        mix = np.clip((end - tn) / 0.1, 0, 1)  # 구간 끝에서 서서히 해제
        env *= mix * am + (1 - mix)
    sig *= env

    # ── 6) 클릭 방지 페이드 + 정규화(-1 dBFS) ──
    fade = int(0.008 * SR)
    sig[:fade] *= np.linspace(0, 1, fade)
    sig[-fade:] *= np.linspace(1, 0, fade)
    sig /= np.max(np.abs(sig)) + 1e-9
    sig *= 10 ** (-1 / 20)
    return sig


# ── 프리셋 4종: f0/모음/진폭 키프레임만 다름 ──
PRESETS = {
    # 새끼고양이 "미-" : 짧고 높음
    "meow_1_kitten_mew": dict(
        duration=0.38,
        f0_keys=[(0, 780), (0.3, 1050), (0.7, 1000), (1, 820)],
        vowel_keys=[(0, "m"), (0.2, "i"), (0.6, "i"), (1, "u")],
        amp_keys=[(0, 0.0), (0.12, 0.6), (0.3, 1.0), (0.8, 0.8), (1, 0.0)],
        tilt=1.0,
        breath=0.05,
    ),
    # 기본 "야옹" : 중간 길이, 상승 후 하강
    "meow_2_normal": dict(
        duration=0.75,
        f0_keys=[(0, 520), (0.25, 720), (0.5, 760), (0.8, 600), (1, 470)],
        vowel_keys=[(0, "m"), (0.15, "i"), (0.45, "a"), (0.75, "a"), (1, "u")],
        amp_keys=[(0, 0.0), (0.1, 0.4), (0.3, 1.0), (0.75, 0.85), (1, 0.0)],
    ),
    # 조르는 "냐아아옹" : 길고 '아'를 오래 끎
    "meow_3_demanding": dict(
        duration=1.25,
        f0_keys=[(0, 480), (0.2, 640), (0.55, 700), (0.8, 560), (1, 420)],
        vowel_keys=[(0, "m"), (0.1, "i"), (0.3, "a"), (0.8, "a"), (1, "u")],
        amp_keys=[(0, 0.0), (0.08, 0.5), (0.25, 1.0), (0.85, 0.9), (1, 0.0)],
        vibrato_hz=5.0,
        vibrato_depth=0.025,
        tilt=1.2,
    ),
    # 질문형 "므르릉?" : 앞부분 트릴(AM) + 끝이 올라감
    "meow_4_trill_question": dict(
        duration=0.6,
        f0_keys=[(0, 420), (0.45, 470), (0.8, 680), (1, 760)],
        vowel_keys=[(0, "m"), (0.4, "u"), (0.75, "i"), (1, "i")],
        amp_keys=[(0, 0.0), (0.08, 0.8), (0.5, 0.9), (0.9, 0.8), (1, 0.0)],
        trill=(26.0, 0.8, 0.5),
        tilt=1.3,
        breath=0.03,
    ),
}


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "meows"
    os.makedirs(out_dir, exist_ok=True)
    for name, params in PRESETS.items():
        sig = synth_meow(**params)
        path = os.path.join(out_dir, f"{name}.wav")
        wavfile.write(path, SR, (sig * 32767).astype(np.int16))  # 16bit PCM 모노
        print(f"{path}  ({params['duration']:.2f}s)")


if __name__ == "__main__":
    main()