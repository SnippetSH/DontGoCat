"""강아지(흰/갈색) 애니메이션 시트 생성기.

사용자가 그린 흰 강아지 키 프레임(1칸 = 1px)을 머리·몸통·꼬리·다리로 나눠
고양이 애니메이션(src/module/CatAnim.cpp 키포즈)과 같은 움직임으로 프레임을 만든다.
갈색 강아지는 같은 키 프레임에서 머리를 갈색 원화 머리로, 꼬리를 방울 꼬리로 바꾸고 털을 갈색 곱슬로 칠한다.

입력 (흰 강아지 키 프레임, 40x32, 배경 (202,211,218)):
    original_assets/white_dog_small.png              기본 자세 (32x32)
    original_assets/dog_bodies/white/sit.png          앉기
    original_assets/dog_bodies/white/sleep*.png       잠자기 4프레임 (sleep_1 -> sleep_2 -> sleep -> sleep_3)
    original_assets/dog_bodies/white/climbing.png     벽타기
    original_assets/dog_bodies/white/roll.png         뒹굴기
    original_assets/dog_bodies/white/crouch*.png      웅크리기 2프레임
    original_assets/brown_dog_light.png               갈색 머리·꼬리 원화

출력:
    sheets/white_dog/<애니메이션>.png, sheets/brown_dog/<애니메이션>.png
        고양이 시트와 같은 형식: 프레임 하나 40x32 칸(hang 40x40)을 8배, 배경 #CAD3DA

사용법:
    .venv/Scripts/python.exe -I py_codes/make_dog_sheets.py
"""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
WDIR = ROOT / "original_assets" / "dog_bodies" / "white"
OUT = ROOT / "sheets"

W, H = 40, 32          # 강아지 캔버스
HANG = 40              # 매달림 캔버스 (정사각형)
SCALE = 8
BG = (202, 211, 218)

WHITE = {"o": (50, 20, 18), "f": (254, 250, 244), "s": (246, 224, 194), "b": (204, 168, 141),
         "c": (219, 188, 159), "i": (223, 151, 139), "n": (189, 107, 105), "x": (101, 78, 60)}
BROWN = {"o": (84, 50, 38), "f": (226, 176, 124), "s": (198, 142, 96), "c": (240, 206, 160),
         "d": (172, 116, 80), "w": (255, 246, 237),
         "e": (125, 91, 63)}   # e: 사용자가 hang 에 넣은 짙은 갈색

# ---------------------------------------------------------------- 격자 도구
# 격자 = dict {(x, y): 키}. '.' 은 저장하지 않는다.


def load(path, colors, dx=0, dy=0):
    inv = {v: k for k, v in colors.items()}
    img = Image.open(path).convert("RGBA")
    g = {}
    for y in range(img.height):
        for x in range(img.width):
            r, gg, b, a = img.getpixel((x, y))
            if a == 0 or (abs(r - BG[0]) + abs(gg - BG[1]) + abs(b - BG[2])) < 16:
                continue
            g[(x + dx, y + dy)] = inv[(r, gg, b)]   # 팔레트 밖 색이면 KeyError
    return g


def shift(g, dx, dy):
    return {(x + dx, y + dy): k for (x, y), k in g.items()}


def take(g, pred):
    return {p: k for p, k in g.items() if pred(*p)}


def drop(g, pred):
    return {p: k for p, k in g.items() if not pred(*p)}


def rect(x0, y0, x1, y1):
    return lambda x, y: x0 <= x <= x1 and y0 <= y <= y1


def fill_fur(g):
    """머리 밑에 깔 채움: 모양만 남기고 전부 바탕 털색."""
    return {p: "f" for p in g}


def outline(cells, owner=None, merged=()):
    """빈 칸 중 채워진 칸(외곽선 제외)과 맞닿은 곳에 외곽선. owner 가 있으면
    서로 합쳐지지 않는 레이어 경계에서 뒤 레이어 쪽 칸을 외곽선으로 (고양이 render() 와 같은 규칙)."""
    out = dict(cells)
    filled = lambda p: p in cells and cells[p] != "o"
    for (x, y), k in cells.items():
        if k == "o":
            continue
        for n in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if n not in cells:
                out[n] = "o"
    if owner:
        for p, k in cells.items():
            if k == "o":
                continue
            a = owner[p]
            x, y = p
            for n in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if filled(n):
                    b = owner[n]
                    if b > a and frozenset((a, b)) not in merged:
                        out[p] = "o"
                        break
    return out


# ---------------------------------------------------------------- 레이어 합성
L_FAR_FRONT, L_FAR_BACK, L_TAIL, L_BODY, L_NEAR_FRONT, L_NEAR_BACK, L_HEAD = range(7)
LEG_LAYER = {"NF": L_NEAR_FRONT, "FF": L_FAR_FRONT, "NB": L_NEAR_BACK, "FB": L_FAR_BACK}


def compose(layers, merged_legs=("NF", "FF", "NB", "FB")):
    """layers: [(레이어 번호, 격자)] 그리는 순서. 같은 칸은 나중 것이 덮는다."""
    cells, owner = {}, {}
    for layer, g in sorted(layers, key=lambda t: t[0]):
        for p, k in g.items():
            cells[p] = k
            owner[p] = layer
    merged = {frozenset((L_BODY, L_TAIL)), frozenset((L_BODY, L_HEAD))}
    for name in merged_legs:
        merged.add(frozenset((L_BODY, LEG_LAYER[name])))
    return outline(cells, owner, merged)


def leg(hx, hy, fx, fy, near=True, sock="f"):
    """다리 하나 (사용자 그림 규칙): 2칸 두께, 발끝 두 줄은 앞쪽으로 1칸 넓은 3칸 발.
    |dx| > |dy| 면 옆으로 뻗은 팔(2줄 띠)."""
    g = {}
    dx, dy = fx - hx, fy - hy
    a, b = "f", "s"   # 사용자 그림은 가까운/먼 다리 색이 같고 외곽선으로만 나뉜다
    if abs(dy) >= abs(dx):
        n = abs(dy)
        sy = 1 if dy >= 0 else -1
        pts = [(hx + (round(dx * i / n) if n else 0), hy + i * sy) for i in range(n + 1)]
        for x, y in pts:
            g[(x, y)] = a
            g[(x + 1, y)] = b
        x, y = pts[-1]
        paw_rows = (y - sy, y) if n >= 1 else (y,)
        for yy in paw_rows:
            g[(x - 1, yy)] = sock
            g[(x, yy)] = sock
            g[(x + 1, yy)] = "s" if yy == y else sock
    else:
        n = abs(dx)
        sx = 1 if dx > 0 else -1
        for i in range(n + 1):
            y = hy + round(dy * i / n)
            g[(hx + i * sx, y)] = sock if i == n else a
            g[(hx + i * sx, y + 1)] = sock if i == n else b
    return g


# ---------------------------------------------------------------- 키 프레임 분해
class Key:
    """사용자 키 프레임 하나. head/tail 영역은 손으로 잰 사각형."""

    def __init__(self, grid, head_rects, tail_rect=None, eye=None, legs_from=None):
        self.grid = grid
        hp = lambda x, y: any(r(x, y) for r in head_rects)
        self.head = take(grid, hp)
        rest = drop(grid, hp)
        self.tail = take(rest, tail_rect) if tail_rect else {}
        rest = drop(rest, tail_rect) if tail_rect else rest
        if legs_from is not None:
            rest = drop(rest, lambda x, y: y >= legs_from)
        self.body = rest
        self.eye = eye                       # 뜬 눈 맨 윗칸 (깜빡일 때 털색으로)
        xs = [x for x, _ in self.head]
        ys = [y for _, y in self.head]
        self.head_box = (min(xs), min(ys), max(xs), max(ys))
        # 머리가 1칸 움직여도 목이 비지 않게 몸통 밑에 까는 털 (턱 쪽 3줄만: 위까지 깔면 머리 위로 비어져 나온다)
        self.filler = fill_fur(take(self.head, lambda x, y: y >= self.head_box[3] - 3))
        if self.tail:
            tx = [x for x, _ in self.tail]
            ty = [y for _, y in self.tail]
            self.tail_box = (min(tx), min(ty), max(tx), max(ty))
        else:
            self.tail_box = None

    def head_closed(self):
        g = dict(self.head)
        if self.eye:
            g[self.eye] = "f"
        return g


def head_rects(top, bottom, x0, x1, chin_from, chin_x1):
    return [rect(x0, top, x1, chin_from - 1), rect(x0, chin_from, chin_x1, bottom)]


def load_keys():
    c = WHITE
    k = {}
    # 기본 자세: 32x32 를 40x32 에 놓는다 (지면선 27 줄, 다른 키와 같게)
    stand = load(ROOT / "original_assets" / "white_dog_small.png", c, 4, -1)
    k["stand"] = Key(stand, head_rects(2, 13, 6, 23, 12, 20), rect(27, 7, 33, 13), eye=(15, 10), legs_from=23)
    k["sit"] = Key(load(WDIR / "sit.png", c), head_rects(5, 15, 5, 22, 11, 20), rect(32, 23, 36, 24))
    k["climbing"] = Key(load(WDIR / "climbing.png", c), head_rects(11, 21, 2, 19, 20, 15),
                        rect(30, 12, 38, 16), legs_from=25)
    k["roll"] = Key(load(WDIR / "roll.png", c),   # 14~15 줄 오른쪽은 앞발이라 머리에서 뺌
                    [rect(2, 14, 15, 15), rect(2, 16, 18, 23), rect(2, 24, 15, 25)], rect(35, 21, 38, 25))
    k["crouch"] = Key(load(WDIR / "crouch.png", c), head_rects(14, 25, 3, 20, 23, 16), rect(35, 13, 39, 18),
                      eye=(12, 21))
    k["crouch_1"] = Key(load(WDIR / "crouch_1.png", c), head_rects(14, 25, 2, 19, 23, 15), rect(34, 15, 39, 21),
                        eye=(11, 21))
    for name, top in (("sleep_1", 12), ("sleep_2", 12), ("sleep", 11), ("sleep_3", 11)):
        k[name] = Key(load(WDIR / f"{name}.png", c), head_rects(top, top + 10, 4, 21, top + 9, 17),
                      rect(31, 22, 36, 24))
    return k


# ---------------------------------------------------------------- 꼬리
# 흰 강아지 꼬리 변형 (기본 자세 좌표, 외곽선 포함). 기본 = 사용자 그림
def white_tails(base):
    up = base
    sway = {}
    top = min(y for _, y in up)
    for (x, y), kk in up.items():
        sway[(x + (1 if y <= top + 2 else 0), y)] = kk
    x0 = min(x for x, _ in up)
    y1 = max(y for _, y in up)
    low_rows = [
        "....oooo",
        "..ooffffo",
        "ooffffso.",
        "ossso....",
    ]
    low = {}
    for r, row in enumerate(low_rows):
        for cc, kk in enumerate(row):
            if kk != ".":
                low[(x0 - 1 + cc, y1 - 3 + r)] = kk
    return {"Up": up, "Sway": sway, "Low": low}


# 갈색 방울 꼬리 (원화 brown_dog_light 5~12줄 24~30열)
BROWN_TAIL = [
    "..ooo..",
    ".offco.",
    "ofcffo.",
    "offffso",
    "osffsfo",
    "osssfso",
    "ossso..",
    "ooo....",
]


def brown_tail_at(x0, y1, low=False, sway=0):
    """방울 꼬리를 왼쪽 아래 (x0, y1) 에 붙인다. low: 뒤로 눕힘(공을 오른쪽 아래로)."""
    rows = BROWN_TAIL
    g = {}
    h = len(rows)
    for r, row in enumerate(rows):
        for cc, kk in enumerate(row):
            if kk == ".":
                continue
            if low:
                g[(x0 + 2 + cc, y1 - h + 3 + r)] = kk
            else:
                g[(x0 + cc + (sway if r < 4 else 0), y1 - h + 1 + r)] = kk
    return g


# ---------------------------------------------------------------- 갈색 변환
def brown_grid():
    """brown_dog_light.png -> 칸 격자 (4px 칸, 2px 오프셋)."""
    inv = {v: k for k, v in BROWN.items()}
    inv[(240, 206, 160)] = "c"
    img = Image.open(ROOT / "original_assets" / "brown_dog_light.png").convert("RGB")
    rows = []
    for cy in range(31):
        rows.append("".join(inv.get(img.getpixel((2 + cx * 4, 2 + cy * 4)), ".") for cx in range(31)))
    ys = [y for y, r in enumerate(rows) if r.strip(".")]
    xs = [x for r in rows for x, ch in enumerate(r) if ch != "."]
    return [r[min(xs):] for r in rows[min(ys):]]


BROWN_HEAD_CUT = {9: 22, 10: 22, 11: 20, 12: 20, 13: 18}   # 줄별 머리 오른쪽 끝 (뒤는 몸통)
BROWN_EYES = ((4, 8), (5, 8), (10, 8), (11, 8))            # 뜬 눈 윗줄 (하이라이트 + 눈동자)


def brown_head(closed=False):
    grid = brown_grid()
    g = {}
    for y in range(14):
        for x, ch in enumerate(grid[y][:BROWN_HEAD_CUT.get(y, 23)]):
            if ch != ".":
                g[(x, y)] = ch
    if closed:
        for p in BROWN_EYES:
            g[p] = "f"
    return g


def brownify(g):
    """흰 털 -> 갈색 곱슬 (칸 좌표가 레이어 기준이라 레이어가 움직여도 무늬가 같음)."""
    out = {}
    for (x, y), k in g.items():
        if k == "o":
            out[(x, y)] = "o"
            continue
        above = g.get((x, y - 1), ".")
        if k in "sbcin":
            base = "s"
        else:
            base = "f"
        if base == "f" and (above in ".o" or (x * 7 + y * 3) % 17 == 0):
            base = "c"
        elif base == "f" and (x * 3 + y * 5) % 13 == 0:
            base = "s"
        out[(x, y)] = "o" if k == "x" else base
    return out


def place_brown_head(key, closed, dx=0, dy=0):
    """흰 머리 영역의 왼쪽 아래에 갈색 머리를 맞춘다 (갈색은 귀가 턱 아래로 2줄 더 처짐)."""
    hx0, _, _, hy1 = key.head_box
    bh = brown_head(closed)
    bh_h = max(y for _, y in bh) + 1
    return shift(bh, hx0 - 1 + dx, hy1 + 2 - bh_h + dy)


# ---------------------------------------------------------------- 프레임 만들기
class Dog:
    def __init__(self, brown):
        self.brown = brown
        self.keys = load_keys()
        st = self.keys["stand"]
        self.tails = white_tails(st.tail)

    # 한 키에서 머리/몸통/꼬리 레이어 꺼내기 (갈색이면 바꿔 끼움)
    def head(self, key, closed=False, dx=0, dy=0):
        if self.brown:
            return place_brown_head(key, closed, dx, dy)
        return shift(key.head_closed() if closed else key.head, dx, dy)

    def body(self, key, dx=0, dy=0, head_dy=0):
        """head_dy: 몸 기준 머리 세로 오프셋. 머리가 내려가면(>0) 목 채움 털도 같이 내려
        머리 왼쪽으로 삐져나오지 않게 한다 (머리가 올라갈 때만 원래 자리에서 목을 메움)."""
        g = shift(key.filler, 0, max(0, head_dy))
        g.update(key.body)
        if self.brown:
            g = brownify(g)
        return shift(g, dx, dy)

    def tail(self, key, variant=None, dx=0, dy=0):
        if self.brown:
            if not key.tail_box:
                return {}
            x0, _, _, y1 = key.tail_box
            if key is self.keys["stand"] or variant:
                return shift(brown_tail_at(x0 - 1, y1, low=variant == "Low", sway=1 if variant == "Sway" else 0),
                             dx, dy)
            return shift(brown_tail_at(x0 - 1, y1 + 1), dx, dy)
        if variant:
            return shift(self.tails[variant], dx, dy)
        return shift(key.tail, dx, dy)

    def legs(self, hips, feet, hip_y, foot_y, body_dy=0, unmerged=(), clamp=True):
        """hips: {이름: hip x}, feet: {이름: (dx, dy) 또는 None}"""
        out = []
        for name in ("FF", "FB", "NF", "NB"):
            f = feet.get(name)
            if f is None:
                continue
            hy = hip_y + body_dy
            fy = foot_y + f[1]
            if clamp and name not in unmerged:
                fy = max(fy, hy)
            g = leg(hips[name] + (0 if name not in unmerged else 0), hy, hips[name] + f[0], fy,
                    near=name in ("NF", "NB"))
            if self.brown:
                g = brownify(g)
            out.append((LEG_LAYER[name], g))
        return out

    # -------- 기본 자세 계열
    STAND_HIPS = {"FF": 12, "NF": 17, "FB": 24, "NB": 29}

    def stand_frame(self, body=(0, 0), head=(0, 0), tail="Up", feet=None, closed=False, unmerged=()):
        st = self.keys["stand"]
        feet = feet or {n: (0, 0) for n in ("NF", "FF", "NB", "FB")}
        bx, by = body
        layers = [(L_BODY, self.body(st, bx, by, head[1] - by)),
                  (L_TAIL, self.tail(st, tail, bx, by)),
                  (L_HEAD, self.head(st, closed, head[0], head[1]))]
        hips = {k: v + bx for k, v in self.STAND_HIPS.items()}
        layers += self.legs(hips, feet, 23, 26, by, unmerged)
        merged = [n for n in ("NF", "FF", "NB", "FB") if n not in unmerged]
        return compose(layers, merged)

    def key_frame(self, key, head=(0, 0), closed=False, body=(0, 0), extra=()):
        k = self.keys[key]
        layers = [(L_BODY, self.body(k, body[0], body[1], head[1])), (L_TAIL, self.tail(k, None, *body)),
                  (L_HEAD, self.head(k, closed, head[0] + body[0], head[1] + body[1]))]
        layers += list(extra)
        return compose(layers)

    # -------- 애니메이션 (CatAnim.cpp 와 같은 값)
    def idle(self):
        eye = [0, 0, 0, 0, 1, 0]
        tail = ["Up", "Up", "Sway", "Sway", "Sway", "Up"]
        return [self.stand_frame(tail=tail[f], closed=eye[f]) for f in range(6)]

    def walk(self):
        dx = [-2, -1, 0, 1, 1, 0, -1, -2]
        dy = [0, 0, 0, 0, -1, -1, -1, -1]
        bob = [0, 0, 1, 1, 0, 0, 1, 1]
        fr = []
        for f in range(8):
            a, b = f, (f + 4) % 8
            feet = {"NF": (dx[a], dy[a]), "FB": (dx[a], dy[a]), "FF": (dx[b], dy[b]), "NB": (dx[b], dy[b])}
            fr.append(self.stand_frame((0, bob[f]), (0, bob[(f - 1) % 8]), "Up", feet))
        return fr

    def run(self):
        dx = [-1, 1, 3, 1, -2, -3]
        dy = [0, 0, -1, -2, -2, -1]
        bob = [0, 1, 0, 0, 0, -1]
        fr = []
        for f in range(6):
            k = lambda i: (dx[i % 6], dy[i % 6])
            feet = {"NF": k(f), "FF": k(f + 1), "NB": k(f + 3), "FB": k(f + 4)}
            fr.append(self.stand_frame((0, bob[f]), (0, bob[(f - 1) % 6]), "Low", feet))
        return fr

    def sit(self):
        return [self.stand_frame((0, 1), (0, 1)),
                self.key_frame("sit", head=(0, 1)),
                self.key_frame("sit"),
                self.key_frame("sit")]

    def sleep(self):
        return [self.key_frame(n, closed=True) for n in ("sleep_1", "sleep_2", "sleep", "sleep_3")]

    def jump_up(self):
        return [self.stand_frame((0, 1), (0, 1)),
                self.stand_frame((0, -1), (0, 0), "Low",
                                 {"NF": (-2, -2), "FF": (-1, -2), "NB": (2, 0), "FB": (2, 0)}),
                self.stand_frame((0, -2), (0, -2), "Low",
                                 {"NF": (-3, -3), "FF": (-2, -3), "NB": (3, -1), "FB": (3, -1)})]

    def fall(self):
        feet = {"NF": (-1, 0), "FF": (0, 0), "NB": (1, -1), "FB": (1, -1)}
        return [self.stand_frame((0, -1), (0, -1), t, feet) for t in ("Up", "Sway")]

    def land(self):
        return [self.stand_frame((0, 1), (0, 2), "Low", {"NF": (-1, 0), "FF": (-1, 0), "NB": (1, 0), "FB": (1, 0)}),
                self.stand_frame((0, 0), (0, 1))]

    def paw_swipe(self):
        paw = [(-1, -4), (-1, -6), (-5, -5), (-5, -2), (-2, 0)]
        head_y = [0, 0, 1, 1, 0]
        fr = []
        for f in range(5):
            feet = {"NF": paw[f], "FF": (0, 0), "NB": (0, 0), "FB": (0, 0)}
            fr.append(self.stand_frame((0, 0), (0, head_y[f]), "Up", feet, unmerged=("NF",)))
        return fr

    # -------- 벽타기 계열
    CLIMB_HIPS = {"NF": 16, "FF": 14, "NB": 24, "FB": 22}   # 팔(2줄 띠)이 시작하는 x, 줄은 25~26

    def climb_frame(self, body=(0, 0), head=(0, 0), feet=None, lifted=()):
        k = self.keys["climbing"]
        bx, by = body
        layers = [(L_BODY, self.body(k, bx, by, head[1])), (L_TAIL, self.tail(k, None, bx, by)),
                  (L_HEAD, self.head(k, False, bx + head[0], by + head[1]))]
        hips = {n: v + bx for n, v in self.CLIMB_HIPS.items()}
        for name in ("FF", "FB", "NF", "NB"):
            fx, fy = feet[name]
            g = leg(hips[name], 25 + by, self.CLIMB_HIPS[name] + fx, 25 + fy, near=name in ("NF", "NB"))
            if self.brown:
                g = brownify(g)
            layers.append((LEG_LAYER[name], g))
        g = compose(layers, [n for n in ("NF", "FF", "NB", "FB") if n not in lifted])
        return shift(g, self.CLIMB_SHIFT_X, 0)

    CLIMB_SHIFT_X = 2   # climb/mantle 은 머리가 왼쪽 끝에 붙어 잘리므로 프레임 전체를 오른쪽으로

    def climb(self):
        sx = [0, 1, 2, 3, 3, 1, -1, -1]
        sy = [0, 0, 0, 0, -1, -2, -2, -1]
        bob = [0, 0, -1, -1, 0, 0, -1, -1]
        x0 = {"NF": -10, "FF": -9, "NB": 4, "FB": 5}   # 뒷발은 뒤로 뻗은 띠
        phase = {"NF": 0, "FF": 4, "NB": 2, "FB": 6}
        fr = []
        for f in range(8):
            feet, lifted = {}, []
            for n in x0:
                kk = (f + phase[n]) % 8
                feet[n] = (x0[n] + sx[kk], sy[kk])
                if kk >= 4:
                    lifted.append(n)
            fr.append(self.climb_frame((0, 0), (0, bob[f]), feet, lifted))
        return fr

    def mantle(self):
        front = {"NF": (-11, 0), "FF": (-9, 0)}
        steps = [((0, 0), (-1, 1), {"NB": (6, 1), "FB": (5, 1)}),
                 ((-1, 0), (-1, 0), {"NB": (7, 0), "FB": (6, 0)}),
                 ((-2, -1), (-1, -1), {"NB": (6, 0), "FB": (5, 0)})]
        fr = []
        for body, head, back in steps:
            feet = dict(front)
            feet.update(back)
            fr.append(self.climb_frame(body, head, feet))
        return fr

    # -------- 뒹굴기
    def roll(self):
        base = self.key_frame("roll")
        return [base, lift_columns(shift(base, -1, 0), lambda x: x <= 19, 23),
                self.key_frame("roll", body=(0, 0)),
                lift_columns(shift(base, 1, 0), lambda x: x >= 29, 23)]

    # -------- 엎드림: sleep_1 몸통 + 뜬 눈 머리(웅크림 머리) + 앞으로 뻗은 앞발
    def sprawl(self):
        sl = self.keys["sleep_1"]
        cr = self.keys["crouch"]
        hx0, hy0, _, _ = sl.head_box
        cx0, cy0, _, _ = cr.head_box
        fr = []
        tail_lift = [0, 1, 2, 1, 0, 1, 2, 1]
        for f in range(8):
            if self.brown:
                head = place_brown_head(sl, f == 5)
            else:
                head = sl.head_closed() if f == 5 else shift(cr.head, hx0 - cx0, hy0 - cy0)
            paws = {}
            for x in range(1, 10):
                paws[(x, 25)] = "f"
                paws[(x, 26)] = "s" if x > 1 else "f"
            if self.brown:
                paws = brownify(paws)
            body = self.body(sl)
            tail = lift_tail(self.tail(sl), tail_lift[f])
            layers = [(L_FAR_FRONT, paws), (L_BODY, body), (L_TAIL, tail), (L_HEAD, head)]
            fr.append(compose(layers, ("NF", "FF", "NB", "FB")))
        return fr

    def crouch(self):
        return [self.key_frame(n) for n in ("crouch", "crouch_1", "crouch", "crouch_1")]

    # -------- 매달림 (40x40): 한 앞발을 위로 뻗어 커서에 매달림
    def hang(self):
        cr = self.keys["crouch"]
        hx0, hy0, _, _ = cr.head_box
        fr = []
        for sway in (0, 1, 0, -1):
            arm = {}
            for y in range(1, 9):
                arm[(20, y)] = "f"
                arm[(21, y)] = "s"
            for x in (19, 20, 21):
                arm[(x, 1)] = "f"
                arm[(x, 2)] = "f"
            body = {}
            for y in range(13, 31):
                for x in range(10, 27):
                    ex = (x - 18) / 8.0
                    ey = (y - 22) / 9.0
                    if ex * ex + ey * ey <= 1.0:
                        body[(x, y)] = "s" if (x >= 24 or y >= 29) else "f"
            legs = {}
            for lx in (12, 21):
                for y in range(29, 35):
                    legs[(lx, y)] = "f"
                    legs[(lx + 1, y)] = "s"
                for x in (lx - 1, lx, lx + 1):
                    legs[(x, 34)] = "f"
                    legs[(x, 35)] = "f" if x < lx + 1 else "s"
            paw = leg(9, 18, 8, 23, near=True)
            tail_g = {}
            for y in range(24, 32):
                tail_g[(26 + (1 if y > 28 else 0), y)] = "f"
                tail_g[(27 + (1 if y > 28 else 0), y)] = "s"
            if self.brown:
                arm, body, legs, paw = brownify(arm), brownify(body), brownify(legs), brownify(paw)
                tail_g = brown_tail_at(25, 33)
                head = place_brown_head(cr, False, 6 - hx0, 4 - hy0)
            else:
                head = shift(cr.head, 6 - hx0, 4 - hy0)
            moving = [(L_FAR_BACK, legs), (L_TAIL, tail_g), (L_BODY, body), (L_NEAR_FRONT, paw), (L_HEAD, head)]
            moving = [(l, {(x + (sway if y >= 3 else 0), y): k for (x, y), k in g.items()}) for l, g in moving]
            arm = {(x + (sway if y >= 3 else 0), y): k for (x, y), k in arm.items()}
            g = compose([(L_FAR_FRONT, arm)] + moving, ("NF", "FF", "NB", "FB"))
            fr.append(g)
        return fr


def lift_columns(g, colpred, pivot):
    """조건 열에서 pivot 줄 위쪽을 1칸 올림 (pivot 줄을 한 번 더 복제해 바닥은 그대로)."""
    out = {}
    for (x, y), k in g.items():
        if colpred(x) and y <= pivot:
            out[(x, y - 1)] = k
            if y == pivot:
                out[(x, y)] = k
        else:
            out.setdefault((x, y), k)
    return outline(out)


def lift_tail(g, n):
    """엎드린 꼬리 끝을 n칸 들어올림 (꼬리 왼쪽 열은 고정)."""
    if not g or n == 0:
        return g
    x0 = min(x for x, _ in g)
    out = {}
    for (x, y), k in g.items():
        lift = n if x >= x0 + 2 else (n // 2 if x >= x0 + 1 else 0)
        out[(x, y - lift)] = k
        for yy in range(y - lift + 1, y + 1):
            out.setdefault((x, yy), k if k != "o" else "f")
    return out


ANIMS = ["idle", "walk", "run", "sit", "sleep", "jump_up", "fall", "land", "paw_swipe",
         "climb", "mantle", "roll", "sprawl", "crouch", "hang"]


def save_sheet(frames, colors, path, rows):
    img = Image.new("RGB", (W * SCALE * len(frames), rows * SCALE), (0xCA, 0xD3, 0xDA))
    px = img.load()
    for i, g in enumerate(frames):
        for (x, y), k in g.items():
            if 0 <= x < W and 0 <= y < rows and k in colors:
                for a in range(SCALE):
                    for b in range(SCALE):
                        px[i * W * SCALE + x * SCALE + a, y * SCALE + b] = colors[k]
    img.save(path)


NL = chr(10)
# 사용자가 칸을 세느라 찍은 표시 색 (파랑/빨강/연두): 수정본에 있어도 무시하고 생성 결과를 그대로 둔다
MARKERS = {(0, 169, 255), (255, 27, 0), (4, 255, 79)}
PATCH_FILE = Path(__file__).resolve().parent / "dog_sheet_patches.py"
DOGS = {"white": (False, "white_dog", WHITE), "brown": (True, "brown_dog", BROWN)}


def load_patches():
    """사용자가 시트에서 손으로 고친 칸 (dog_sheet_patches.py, --capture 로 생성)."""
    if not PATCH_FILE.exists():
        return {}
    ns = {}
    exec(PATCH_FILE.read_text(encoding="utf-8"), ns)
    return ns.get("PATCHES", {})


def frames_for(dog, dog_key, anim, patches):
    frames = getattr(dog, anim)()
    for f, text in patches.get(dog_key, {}).get(anim, {}).items():
        g = frames[f]
        for item in text.split(";"):
            x, y, k = item.split(",")
            if k == ".":
                g.pop((int(x), int(y)), None)
            else:
                g[(int(x), int(y))] = k
    return frames


def frame_pixels(g, colors, rows):
    return [[colors.get(g.get((x, y)), BG) if g.get((x, y)) in colors else BG for x in range(W)] for y in range(rows)]


def read_small(path, n, rows):
    img = Image.open(path).convert("RGB")
    assert img.size == (W * n, rows), f"{path}: 크기 {img.size} != {(W * n, rows)}"
    return [[[img.getpixel((f * W + x, y)) for x in range(W)] for y in range(rows)] for f in range(n)]


def capture(dog_key, src_dir):
    """src_dir 의 *_small.png (사용자 수정본) 과 코드 결과의 차이를 패치로 저장."""
    brown, _, colors = DOGS[dog_key]
    inv = {v: k for k, v in colors.items()}
    inv[BG] = "."
    dog = Dog(brown)
    all_patches = load_patches()
    mine = {}
    for anim in ANIMS:
        rows = HANG if anim == "hang" else H
        frames = getattr(dog, anim)()
        target = read_small(Path(src_dir) / f"{anim}_small.png", len(frames), rows)
        per = {}
        for f, g in enumerate(frames):
            gen = frame_pixels(g, colors, rows)
            want = [row[:] for row in target[f]]
            for y in range(rows):
                for x in range(W):
                    if want[y][x] in MARKERS:   # 표시 칸: 주변(표시 제외 8칸)이 대부분 배경이면 배경, 아니면 생성 결과
                        nb = [target[f][yy][xx] for yy in range(y - 1, y + 2) for xx in range(x - 1, x + 2)
                              if (xx, yy) != (x, y) and 0 <= xx < W and 0 <= yy < rows and target[f][yy][xx] not in MARKERS]
                        want[y][x] = BG if nb and sum(c == BG for c in nb) * 2 > len(nb) else gen[y][x]
            items = [f"{x},{y},{inv[want[y][x]]}"   # 팔레트 밖 색이면 KeyError
                     for y in range(rows) for x in range(W) if gen[y][x] != want[y][x]]
            if items:
                per[f] = ";".join(items)
        if per:
            mine[anim] = per
    all_patches[dog_key] = mine
    lines = ["# 자동 생성 파일 - 직접 고치지 말 것. py_codes/make_dog_sheets.py --capture 로 재생성.",
             "# 사용자가 시트(small, 1칸=1px)에서 손으로 고친 칸: {강아지: {애니메이션: {프레임: \"x,y,키;...\"}}}",
             "# 키 '.' = 배경(지움). 생성 규칙 결과 위에 덮어쓴다.",
             "PATCHES = {"]
    for dk, anims in all_patches.items():
        lines.append(f"    {dk!r}: {{")
        for anim, per in anims.items():
            lines.append(f"        {anim!r}: {{")
            for f, text in per.items():
                lines.append(f"            {f}: {text!r},")
            lines.append("        },")
        lines.append("    },")
    lines.append("}")
    PATCH_FILE.write_text(NL.join(lines) + NL, encoding="utf-8", newline=NL)
    n = sum(t.count(";") + 1 for per in mine.values() for t in per.values())
    print(f"패치 저장: {PATCH_FILE} ({dog_key}: {n}칸)")


def main():
    import argparse
    import sys
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dog", choices=["white", "brown", "all"], default="all")
    ap.add_argument("--small-only", action="store_true", help="8배 시트는 쓰지 않고 small(1칸=1px)만 sheets/<dog>/small 에 쓴다")
    ap.add_argument("--capture", metavar="DIR", help="DIR 의 *_small.png 를 정답으로 패치를 만든다 (--dog white|brown)")
    ap.add_argument("--verify", metavar="DIR", help="결과가 DIR 의 *_small.png 와 픽셀 단위로 같은지 검사 (파일은 쓰지 않음)")
    args = ap.parse_args()
    keys = ["white", "brown"] if args.dog == "all" else [args.dog]

    if args.capture:
        assert args.dog != "all", "--capture 는 --dog white 또는 brown 과 함께"
        capture(args.dog, args.capture)
        return

    patches = load_patches()
    bad = 0
    for dk in keys:
        brown, name, colors = DOGS[dk]
        dog = Dog(brown)
        out = OUT / name
        for anim in ANIMS:
            rows = HANG if anim == "hang" else H
            frames = frames_for(dog, dk, anim, patches)
            if args.verify:
                target = read_small(Path(args.verify) / f"{anim}_small.png", len(frames), rows)
                diff = [sum(1 for y in range(rows) for x in range(W)
                            if frame_pixels(g, colors, rows)[y][x] != target[f][y][x]
                            and target[f][y][x] not in MARKERS) for f, g in enumerate(frames)]
                bad += sum(diff)
                print(f"{name:10s} {anim:10s} {'같음' if not any(diff) else '다름 ' + str(diff)}")
                continue
            clipped = sum(1 for g in frames for (x, y) in g if not (0 <= x < W and 0 <= y < rows))
            if not args.small_only:
                out.mkdir(parents=True, exist_ok=True)
                save_sheet(frames, colors, out / f"{anim}.png", rows)
            small = out / "small"
            small.mkdir(parents=True, exist_ok=True)
            img = Image.new("RGB", (W * len(frames), rows), BG)
            for f, g in enumerate(frames):
                for y, row in enumerate(frame_pixels(g, colors, rows)):
                    for x, c in enumerate(row):
                        img.putpixel((f * W + x, y), c)
            img.save(small / f"{anim}_small.png")
            print(f"{name:10s} {anim:10s} frames={len(frames)}" + (f"  캔버스 밖 칸 {clipped}" if clipped else ""))
    if args.verify:
        print("검증:", "전부 같음" if bad == 0 else f"다른 칸 {bad}")
        sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
