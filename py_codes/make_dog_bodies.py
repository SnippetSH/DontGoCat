"""고양이 자세별 몸통 파츠를 강아지 크기로 키운 시안(편집용 PNG)을 만든다.

src/module/CatSprite.cpp 의 몸통·꼬리 파츠 맵을 읽어 가로 SX, 세로 SY 배로 키우고
(nearest, 겹친 칸의 내부 외곽선은 한 줄로 유지), 강아지별 털 무늬로 칠한 뒤 외곽선을 두른다.
결과는 사용자가 다시 다듬을 초안이다.

출력 (original_assets/dog_bodies/<white|brown>/):
    body_<자세>.png    몸통만 (외곽선 포함, 1칸 = 4px, 투명 배경)
    tail_<꼬리>.png    꼬리만 (같은 형식)
    preview.png        머리(원화)·꼬리·다리를 붙여 본 자세별 합성 미리보기 (40x32 캔버스, 8배)

사용법:
    .venv/Scripts/python.exe -I py_codes/make_dog_bodies.py
"""
import math
import re
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
CAT_CPP = ROOT / "src" / "module" / "CatSprite.cpp"
OUT = ROOT / "original_assets" / "dog_bodies"

SX, SY = 1.2, 1.25          # 고양이 -> 강아지 배율 (원화 몸 크기 비 27/23, 25/19 근처)
CELL = 4                    # 편집용 PNG 한 칸 = 4px (원화와 같음)
CANVAS_W, CANVAS_H = 40, 32 # 강아지 캔버스
ORIGIN_X, ORIGIN_Y = 6, 4   # 고양이 기본 좌표 (0,0) 이 놓일 강아지 캔버스 위치

DOGS = {
    "white": {
        "src": "white_dog.png",
        "colors": {"o": (50, 20, 18), "f": (254, 250, 244), "s": (246, 224, 194),
                   "b": (204, 168, 141), "c": (219, 188, 159), "i": (223, 151, 139), "n": (189, 107, 105)},
        "head_rows": (0, 10), "head_cut": {8: 15, 9: 15}, "head_w": 18,   # 원화 격자에서 머리 영역 (줄 범위, 줄별 자를 열)
    },
    "brown": {
        "src": "brown_dog_light.png",
        "colors": {"o": (84, 50, 38), "f": (226, 176, 124), "s": (198, 142, 96),
                   "c": (240, 206, 160), "d": (172, 116, 80), "w": (255, 246, 237)},
        "head_rows": (0, 14), "head_cut": {9: 22, 10: 22, 11: 20, 12: 20, 13: 18}, "head_w": 23,
    },
}

BODIES = ["Stand", "Sit", "Loaf", "LoafIn", "Stretch", "Roll", "RollLeft", "RollRight", "Sprawl", "Crouch"]
TAILS = ["Up", "Sway", "Low", "Ground", "SprawlA", "SprawlB", "SprawlC", "RollA", "RollB", "CrouchA", "CrouchB"]

# 고양이 BodyGeom (CatSprite.cpp 와 같음): hipX(NearFront, FarFront, NearBack, FarBack), hipY, footY, tailDx, tailDy
GEOM = {
    "Stand": ((8, 5, 16, 13), 15, 17, 0, 0), "Sit": ((8, 5, 16, 13), 15, 17, 0, 0),
    "Loaf": ((8, 5, 16, 13), 15, 17, 0, 0), "LoafIn": ((8, 5, 16, 13), 15, 17, 0, 0),
    "Stretch": ((8, 5, 18, 15), 15, 17, 2, 2),
    "Roll": ((14, 10, 22, 18), 11, 7, 0, 0), "RollLeft": ((14, 10, 22, 18), 11, 7, 0, 0),
    "RollRight": ((14, 10, 22, 18), 11, 7, 0, 0),
    "Sprawl": ((8, 5, 16, 13), 15, 17, 0, 0), "Crouch": ((8, 5, 16, 13), 15, 17, 0, 0),
}

# 미리보기 자세 (CatAnim.cpp 키포즈에서 가져옴): 몸통, body, head, 꼬리, 다리 foot (NF, FF, NB, FB, None=숨김)
PREVIEW = [
    ("sit",     "Sit",       (0, 0),  (0, -1), "Ground", [(0, 0), (0, 0), None, None]),
    ("sleep",   "Loaf",      (0, 0),  (0, 5),  "Ground", [None] * 4),
    ("sleep_in", "LoafIn",   (0, 0),  (0, 5),  "Ground", [None] * 4),
    ("climb",   "Stretch",   (0, 1),  (-1, 6), "Low",    [(-8, 0), (-4, -1), (2, 0), (0, -2)]),
    ("roll",    "Roll",      (0, 0),  (-2, 8), "RollA",  [(-1, 0), (-1, -1), (1, -1), (1, 0)]),
    ("roll_l",  "RollLeft",  (-1, 0), (-3, 7), "RollB",  [(-2, -1), (-1, 0), (2, 0), (0, -1)]),
    ("roll_r",  "RollRight", (1, 0),  (-1, 8), "RollB",  [(0, 0), (-1, -1), (0, -1), (2, 0)]),
    ("sprawl",  "Sprawl",    (0, 0),  (5, 8),  "SprawlA", [None] * 4),
    ("crouch",  "Crouch",    (-1, 0), (-2, 7), "CrouchA", [None] * 4),
]

CAT_NAMES = {"Stand": "kBodyStand", "Sit": "kBodySit", "Loaf": "kBodyLoaf", "LoafIn": "kBodyLoafIn",
             "Stretch": "kBodyStretch", "Roll": "kBodyRoll", "RollLeft": "kBodyRollLeft",
             "RollRight": "kBodyRollRight", "Sprawl": "kBodySprawl", "Crouch": "kBodyCrouch"}


def parse_cat_maps():
    text = CAT_CPP.read_text(encoding="utf-8")
    maps = {}
    for m in re.finditer(r"const PartMap (\w+) = \{(-?\d+), (-?\d+), \{(.*?)\}\};", text, re.S):
        rows = re.findall(r'"([^"]*)"', m.group(4))
        maps[m.group(1)] = (int(m.group(2)), int(m.group(3)), rows)
    return maps


def scale_map(x0, y0, rows):
    """고양이 파츠 -> 강아지 칸 좌표 dict {(x, y): key}. 겹친 칸의 내부 'o' 는 한 줄만 남긴다."""
    w = max(len(r) for r in rows)
    h = len(rows)
    get = lambda x, y: rows[y][x] if 0 <= y < h and x < len(rows[y]) else "."
    X0, Y0 = math.floor(x0 * SX), math.floor(y0 * SY)
    out = {}
    for ty in range(math.ceil(h * SY)):
        sy = min(int(ty / SY), h - 1)
        dup_y = ty > 0 and int((ty - 1) / SY) == sy
        for tx in range(math.ceil(w * SX)):
            sx = min(int(tx / SX), w - 1)
            dup_x = tx > 0 and int((tx - 1) / SX) == sx
            k = get(sx, sy)
            if k == "o" and (dup_x or dup_y):   # 늘어난 칸은 옆 칸 색으로
                k = get(sx + 1, sy) if dup_x else get(sx, sy + 1)
                if k in ".o":
                    k = "f"
            if k != ".":
                out[(X0 + tx, Y0 + ty)] = k
    return out


def texture(cells, dog):
    """고양이 키(f/s/c/b/…) -> 강아지 털 무늬. 내부 'o' 는 유지."""
    out = {}
    for (x, y), k in cells.items():
        if k == "o":
            out[(x, y)] = "o"
            continue
        below = (x, y + 1) in cells
        above = (x, y - 1) in cells
        if dog == "white":
            if not below:
                out[(x, y)] = "s"
            else:
                out[(x, y)] = "s" if k == "s" and (x + y) % 3 == 0 else "f"
        else:   # brown: 곱슬 털 = f 바탕에 s/c 점
            if not below:
                out[(x, y)] = "s"
            elif not above or (x * 5 + y * 2) % 9 == 0:
                out[(x, y)] = "c"
            elif (x * 3 + y * 5) % 7 == 0 or (k == "s" and (x + y) % 2 == 0):
                out[(x, y)] = "s"
            else:
                out[(x, y)] = "c" if k in "cb" else "f"
    return out


def outline(cells):
    out = dict(cells)
    for (x, y), k in cells.items():
        if k == "o":
            continue
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if (nx, ny) not in cells:
                out[(nx, ny)] = "o"
    return out


def leg(cells, hx, hy, fx, fy, col, sock):
    """CatSprite drawLeg 과 같은 규칙, 강아지 칸 좌표."""
    dx, dy = fx - hx, fy - hy
    if abs(dy) >= abs(dx):
        n, s = abs(dy), (1 if dy >= 0 else -1)
        for i in range(n + 1):
            x = hx + (round(dx * i / n) if n else 0)
            k = sock if i == n else col
            cells[(x, hy + i * s)] = k
            cells[(x + 1, hy + i * s)] = k
    else:
        n, s = abs(dx), (1 if dx > 0 else -1)
        for i in range(n + 1):
            y = hy + round(dy * i / n)
            k = sock if i == n else col
            cells[(hx + i * s, y)] = k
            cells[(hx + i * s, y + 1)] = k


def load_dog_grid(name):
    """원화 PNG -> 칸 격자 (흰 강아지는 줄 높이가 고르지 않아 2px 격자에서 같은 줄을 접는다)."""
    cfg = DOGS[name]
    img = Image.open(ROOT / "original_assets" / cfg["src"]).convert("RGB")
    inv = {v: k for k, v in cfg["colors"].items()}
    px = img.load()
    g2 = [[inv.get(px[x * 2, y * 2], ".") for x in range(0, 64, 2)] for y in range(64)]
    rows, i = [], 0
    while i < len(g2):
        j = i
        while j < len(g2) and g2[j] == g2[i]:
            j += 1
        rows += [g2[i]] * max(1, (j - i) // 2)
        i = j
    rows = [r for r in rows if any(c != "." for c in r)]
    xs = [x for r in rows for x, c in enumerate(r) if c != "."]
    return [r[min(xs):max(xs) + 1] for r in rows]


def dog_head(name):
    cfg = DOGS[name]
    grid = load_dog_grid(name)
    y0, y1 = cfg["head_rows"]
    head = {}
    for y in range(y0, y1):
        cut = cfg["head_cut"].get(y, cfg["head_w"])
        for x, c in enumerate(grid[y][:cut]):
            if c != ".":
                head[(x, y - y0)] = c
    return head, grid


def save_cells(cells, colors, path):
    xs = [x for x, _ in cells]
    ys = [y for _, y in cells]
    x0, y0 = min(xs), min(ys)
    img = Image.new("RGBA", ((max(xs) - x0 + 1) * CELL, (max(ys) - y0 + 1) * CELL), (0, 0, 0, 0))
    for (x, y), k in cells.items():
        if k in colors:
            img.paste(colors[k] + (255,), ((x - x0) * CELL, (y - y0) * CELL, (x - x0 + 1) * CELL, (y - y0 + 1) * CELL))
    img.save(path)


def main():
    cat = parse_cat_maps()
    for dog, cfg in DOGS.items():
        colors = cfg["colors"]
        out_dir = OUT / dog
        out_dir.mkdir(parents=True, exist_ok=True)
        bodies = {b: texture(scale_map(*cat[CAT_NAMES[b]]), dog) for b in BODIES}
        tails = {t: texture(scale_map(*cat["kTail" + t]), dog) for t in TAILS}
        for b, cells in bodies.items():
            save_cells(outline(cells), colors, out_dir / f"body_{b}.png")
        for t, cells in tails.items():
            save_cells(outline(cells), colors, out_dir / f"tail_{t}.png")

        head, _ = dog_head(dog)
        head_w = max(x for x, _ in head) + 1
        head_h = max(y for _, y in head) + 1

        z = 8
        sheet = Image.new("RGB", (CANVAS_W * z * len(PREVIEW), CANVAS_H * z), (0xCA, 0xD3, 0xDA))
        for i, (_, shape, body, headoff, tail, feet) in enumerate(PREVIEW):
            hips, hip_y, foot_y, tdx, tdy = GEOM[shape]
            bx, by = round(body[0] * SX), round(body[1] * SY)
            cv = {}
            near = {0: "f", 2: "f"}
            for li in (1, 3, 0, 2):     # 먼 다리 -> 꼬리/몸통 -> 가까운 다리
                if li == 0:
                    for (x, y), k in tails[tail].items():
                        cv[(x + bx + round(tdx * SX), y + by + round(tdy * SY))] = k
                    for (x, y), k in bodies[shape].items():
                        cv[(x + bx, y + by)] = k
                f = feet[li]
                if f is None:
                    continue
                hx = round(hips[li] * SX) + bx
                hy = round(hip_y * SY) + by
                fy = round((foot_y + f[1]) * SY)
                if shape not in ("Roll", "RollLeft", "RollRight"):
                    fy = max(fy, hy)
                leg(cv, hx, hy, round((hips[li] + f[0]) * SX), fy, near.get(li, "s"), "f")
            cv = outline(cv)
            # 머리: 고양이 머리(11x9, 기본 좌표 (0,1)) 의 턱 왼쪽 아래에 강아지 머리 턱 왼쪽 아래를 맞춘다
            ax = round(headoff[0] * SX) - 1
            ay = round((10 + headoff[1]) * SY) - head_h
            for (x, y), k in head.items():
                cv[(x + ax, y + ay)] = k
            for (x, y), k in cv.items():
                cx, cy = x + ORIGIN_X, y + ORIGIN_Y
                if 0 <= cx < CANVAS_W and 0 <= cy < CANVAS_H and k in colors:
                    sheet.paste(colors[k], (i * CANVAS_W * z + cx * z, cy * z,
                                            i * CANVAS_W * z + (cx + 1) * z, (cy + 1) * z))
        sheet.save(out_dir / "preview.png")
        print(f"{dog}: {out_dir} (head {head_w}x{head_h})")


if __name__ == "__main__":
    main()
