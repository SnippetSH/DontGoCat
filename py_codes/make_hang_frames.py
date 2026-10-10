"""사용자 원화(hang) -> C++ 비트맵 소스 생성기.

original_assets/cat_hanging_resized.png (288x216, 4px 칸 = 72x54 격자) 의 고양이 영역(22x32 칸)을
1:1 로 잘라 32x32 캔버스에 놓고, 대롱대롱 4프레임을 만들어 src/module/CatHangFrames.inc 로 쓴다.
(README-settings.md 5.1 "hang 원화 규칙")

- 칸 안 색이 섞이면 최다 색을 쓴다. 모든 색은 CatPalette 색이어야 한다 (아니면 오류).
- 그립 열(앞발 맨 윗줄 가운데 칸)이 캔버스 x=16 에, 맨 윗줄이 y=0 에 오도록 가로로 배치.
- 프레임 i: PIVOT 줄 위(앞발~손목)는 고정, 그 줄부터 아래는 통째로 SWAY[i] 칸 가로로 민다.
- 외곽선은 원화에 이미 있으므로 따로 두르지 않는다. 키는 CatSprite.cpp 의 colorFor() 와 같다.

사용: .venv/Scripts/python.exe -I py_codes/make_hang_frames.py [원화.png] [출력.inc]
"""
import sys
from collections import Counter
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC_PNG = ROOT / "original_assets" / "cat_hanging_resized.png"
OUT_INC = ROOT / "src" / "module" / "CatHangFrames.inc"

CELL = 4            # 원화 한 칸 = 4px
CANVAS = 32         # CatSprite::Width x Width
GRIP_CROP_X = 11    # 크롭 안 그립 열 (앞발 맨 윗줄 3칸의 가운데)
GRIP_CANVAS_X = 16  # 캔버스에서 그립 열 위치
PIVOT = 3           # 이 줄(크롭 기준)부터 아래가 흔들림. 0~2 줄 = 앞발 + 손목은 고정
SWAY = (0, 1, 0, -1)

PALETTE = {          # CatPalette -> colorFor() 키
    (0x31, 0x14, 0x10): "o",   # outline
    (0xF9, 0xBD, 0x61): "f",   # fur
    (0xE4, 0x91, 0x45): "s",   # shade
    (0xFD, 0xF5, 0xE3): "c",   # cream
    (0xE9, 0xD6, 0xC4): "b",   # creamShadow
    (0xDC, 0x8A, 0x62): "i",   # innerEar
    (0xE6, 0x8A, 0x74): "n",   # nose
    (0x5A, 0x30, 0x20): "d",   # mouth
    (0xFF, 0xFF, 0xFF): "w",   # highlight
}


def load_grid(png: Path):
    """PNG -> 격자(칸마다 키 문자, 투명 '.'), 혼합 칸 목록."""
    img = Image.open(png).convert("RGBA")
    w, h = img.size
    assert w % CELL == 0 and h % CELL == 0, "크기가 칸 단위가 아님"
    px = img.load()
    grid, mixed = [], []
    for gy in range(h // CELL):
        row = []
        for gx in range(w // CELL):
            cnt = Counter(px[gx * CELL + i, gy * CELL + j] for i in range(CELL) for j in range(CELL))
            color, _ = cnt.most_common(1)[0]
            if len(cnt) > 1:
                mixed.append((gx, gy, dict(cnt)))
            if color[3] == 0:
                row.append(".")
            else:
                assert color[3] == 255, f"반투명 칸 ({gx},{gy})"
                row.append(PALETTE[color[:3]])   # 팔레트 밖이면 KeyError
        grid.append(row)
    return grid, mixed


def crop_cat(grid):
    ys = [y for y, r in enumerate(grid) if any(c != "." for c in r)]
    xs = [x for r in grid for x, c in enumerate(r) if c != "."]
    x0, x1, y0, y1 = min(xs), max(xs) + 1, min(ys), max(ys) + 1
    return [r[x0:x1] for r in grid[y0:y1]], (x0, y0, x1, y1)


def build_frames(crop):
    h, w = len(crop), len(crop[0])
    off = GRIP_CANVAS_X - GRIP_CROP_X
    assert crop[0][GRIP_CROP_X] == "o" and crop[0][GRIP_CROP_X - 1] == "o" and crop[0][GRIP_CROP_X + 1] == "o"
    assert h <= CANVAS and w + off + max(SWAY) <= CANVAS and off + min(SWAY) >= 0, "캔버스 밖으로 잘림"
    frames = []
    for dx in SWAY:
        cv = [["."] * CANVAS for _ in range(CANVAS)]
        for y in range(h):
            for x in range(w):
                if crop[y][x] != ".":
                    cv[y][x + off + (dx if y >= PIVOT else 0)] = crop[y][x]
        frames.append(["".join(r) for r in cv])
    return frames


def connected(frame):
    """불투명 픽셀이 8방향으로 한 덩어리인지."""
    pts = {(x, y) for y, r in enumerate(frame) for x, c in enumerate(r) if c != "."}
    seen, stack = set(), [next(iter(pts))]
    while stack:
        x, y = stack.pop()
        if (x, y) in seen:
            continue
        seen.add((x, y))
        stack += [(x + a, y + b) for a in (-1, 0, 1) for b in (-1, 0, 1) if (x + a, y + b) in pts]
    return len(seen) == len(pts)


def write_inc(path: Path, frames):
    lines = [
        "// 자동 생성 파일 - 직접 고치지 말 것. py_codes/make_hang_frames.py 로 재생성 (원화: original_assets/cat_hanging_resized.png)",
        "// hang 프레임: 32x32, 왼쪽 보기, 외곽선 포함. 키는 colorFor() 와 같고 '.' 은 투명.",
        f"// 그립 열 x={GRIP_CANVAS_X}, 맨 윗줄 y=0. 앞발~손목 {PIVOT} 줄은 고정, 그 아래는 가로로 {list(SWAY)} 칸 민 프레임.",
        f"constexpr int kHangFrameCount = {len(frames)};",
        f"const char *const kHangFrames[kHangFrameCount][{CANVAS}] = {{",
    ]
    for i, fr in enumerate(frames):
        lines.append(f"    {{   // 프레임 {i}: dx={SWAY[i]:+d}")
        lines += [f'        "{r}",' for r in fr]
        lines.append("    },")
    lines.append("};")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main():
    sys.stdout.reconfigure(encoding="utf-8")   # 윈도 콘솔 코드페이지와 무관하게 한글 출력
    png = Path(sys.argv[1]) if len(sys.argv) > 1 else SRC_PNG
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else OUT_INC
    grid, mixed = load_grid(png)
    for gx, gy, cnt in mixed:
        print(f"혼합 칸 ({gx},{gy}): {cnt}")
    crop, box = crop_cat(grid)
    print(f"크롭 격자 bbox={box} 크기={len(crop[0])}x{len(crop)}")
    frames = build_frames(crop)
    for i, fr in enumerate(frames):
        assert connected(fr), f"프레임 {i}: 이음매가 끊김 (PIVOT={PIVOT})"
    write_inc(out, frames)
    print(f"쓰기: {out} ({len(frames)}프레임, 이음매 연결 확인)")


if __name__ == "__main__":
    main()
