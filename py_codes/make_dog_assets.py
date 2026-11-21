"""강아지 시트(small) -> 앱 에셋 생성기.

1. src/module/DogFrames.inc
   sheets/<white_dog|brown_dog>/small/<동작>_small.png (1칸 = 1px, 프레임 폭 40, hang 만 40x40) 를
   C++ 문자열 비트맵으로 옮긴다. 키 문자 -> 색 표는 강아지마다 따로 둔다 ('.' = 투명).
   동작 순서는 CatAnim 열거 순서와 같다.
2. assets/<white|brown>_dog_face_{16,24,32,48,64}.png
   idle 0번 프레임에서 머리만 잘라 (make_dog_sheets.py 의 머리 영역) 고양이 tray_face 처럼
   투명 정사각 캔버스 가운데에 정수 배율로 놓는다.

사용법:
    .venv/Scripts/python.exe -I py_codes/make_dog_assets.py
"""
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_dog_sheets as m  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT_INC = ROOT / "src" / "module" / "DogFrames.inc"
ICON_SIZES = (16, 24, 32, 48, 64)
DOGS = (("white", "white_dog", "kWhiteDog", m.WHITE, False), ("brown", "brown_dog", "kBrownDog", m.BROWN, True))


def read_sheet(dog_dir, anim, colors):
    rows_n = m.HANG if anim == "hang" else m.H
    img = Image.open(ROOT / "sheets" / dog_dir / "small" / f"{anim}_small.png").convert("RGB")
    assert img.height == rows_n and img.width % m.W == 0, f"{dog_dir}/{anim}: 크기 {img.size}"
    inv = {v: k for k, v in colors.items()}
    inv[m.BG] = "."
    rows = []
    for y in range(img.height):
        rows.append("".join(inv[img.getpixel((x, y))] for x in range(img.width)))   # 팔레트 밖이면 KeyError
    return rows, img.width // m.W


def write_inc():
    lines = [
        "// 자동 생성 파일 - 직접 고치지 말 것. py_codes/make_dog_assets.py 로 재생성",
        "// (원본: sheets/<white_dog|brown_dog>/small/<동작>_small.png, make_dog_sheets.py 결과).",
        "// 강아지 프레임: 왼쪽 보기, 외곽선 포함, 1칸 = 1px. 프레임 폭 40, 높이 32 (hang 만 40).",
        "// 시트 한 줄 = 프레임들을 가로로 이어 붙인 것. 키는 강아지별 색 표(k*Keys/k*Colors), '.' 은 투명.",
        "",
    ]
    for _, dog_dir, prefix, colors, _ in DOGS:
        keys = "".join(colors.keys())
        lines.append(f'constexpr char {prefix}Keys[] = "{keys}";')
        lines.append(f"constexpr QRgb {prefix}Colors[] = {{"
                     + ", ".join(f"0xFF{r:02X}{g:02X}{b:02X}" for r, g, b in colors.values()) + "};")
        table = []
        for anim in m.ANIMS:
            rows, n = read_sheet(dog_dir, anim, colors)
            name = f"{prefix}_{anim}"
            lines.append(f"const char *const {name}[{len(rows)}] = {{")
            lines += [f'    "{r}",' for r in rows]
            lines.append("};")
            table.append(f"    {{{n}, {len(rows)}, {name}}},")
        lines.append(f"const DogSheet {prefix}Sheets[] = {{   // CatAnim 순서")
        lines += table
        lines.append("};")
        lines.append("")
    OUT_INC.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"쓰기: {OUT_INC}")


def head_cells(brown):
    """idle 0번 프레임에서 머리가 차지하는 칸 (make_dog_sheets 의 머리 레이어 위치)."""
    dog = m.Dog(brown)
    st = dog.keys["stand"]
    return set(dog.head(st, False).keys())


def write_icons():
    for short, dog_dir, _, colors, brown in DOGS:
        img = Image.open(ROOT / "sheets" / dog_dir / "small" / "idle_small.png").convert("RGB")
        cells = {p for p in head_cells(brown) if 0 <= p[0] < m.W and 0 <= p[1] < m.H}
        cells = {p for p in cells if img.getpixel(p) != m.BG}
        # 몸통 쪽으로 잘린 변에는 외곽선을 둘러 닫는다
        ring = set()
        for (x, y) in cells:
            for n in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if n not in cells:
                    ring.add(n)
        xs = [x for x, _ in cells | ring]
        ys = [y for _, y in cells | ring]
        x0, y0 = min(xs), min(ys)
        w, h = max(xs) - x0 + 1, max(ys) - y0 + 1
        head = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        outline = colors["o"] + (255,)
        for (x, y) in ring:
            head.putpixel((x - x0, y - y0), outline)
        for (x, y) in cells:
            head.putpixel((x - x0, y - y0), img.getpixel((x, y)) + (255,))
        for size in ICON_SIZES:
            factor = size // max(w, h)
            if factor >= 1:   # 정수 배율로 들어가면 픽셀 그대로
                big = head.resize((w * factor, h * factor), Image.Resampling.NEAREST)
            else:             # 머리가 아이콘보다 크면 (16px) 비율 유지해 줄임
                k = size / max(w, h)
                big = head.resize((max(1, round(w * k)), max(1, round(h * k))), Image.Resampling.BOX)
            canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
            canvas.paste(big, ((size - big.width) // 2, (size - big.height) // 2))
            canvas.save(ROOT / "assets" / f"{short}_dog_face_{size}.png")
        print(f"{short}: 머리 {w}x{h} -> assets/{short}_dog_face_*.png")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    write_inc()
    write_icons()
