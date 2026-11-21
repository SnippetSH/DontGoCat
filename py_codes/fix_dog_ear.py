"""brown_dog_light.png 의 오른쪽(화면 기준) 귀 모양을 다듬어 새 파일로 저장한다.

그림은 4px 크기 도트가 (2,2) 오프셋부터 시작하는 31x31 격자다.
귀가 아래로 갈수록 좁고 바닥이 평평해 막대처럼 보이는 점을 고치려고,
귀 바깥 윤곽선 아랫부분을 한 칸 바깥으로 옮기고 바닥을 한 칸 넓혀 둥근 처진 귀로 만든다.

사용법:
    .venv/Scripts/python.exe py_codes/fix_dog_ear.py [입력] [출력]
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "original_assets" / "brown_dog_light.png"
DST = ROOT / "original_assets" / "brown_dog_light_ear.png"

CELL = 4
OFFSET = 2

OUTLINE = (84, 50, 38)
SHADOW = (198, 142, 96)
BASE = (226, 176, 124)

# (열, 행) 격자 좌표 -> 새 색
EDITS = {
    # 기존 바깥 윤곽선 아랫부분 -> 귀 안쪽 털(가장자리 그림자)
    (17, 15): SHADOW,
    (17, 16): SHADOW,
    (16, 16): BASE,
    # 윤곽선을 한 칸 바깥으로
    (18, 15): OUTLINE,
    (18, 16): OUTLINE,
    # 바닥 한 칸 넓히기 (모서리는 대각선으로 둥글게)
    (17, 17): OUTLINE,
}


def fix(src: Path, dst: Path) -> None:
    im = Image.open(src)
    mode = im.mode
    im = im.convert("RGB")
    draw = ImageDraw.Draw(im)
    for (cx, cy), color in EDITS.items():
        x0 = OFFSET + cx * CELL
        y0 = OFFSET + cy * CELL
        draw.rectangle([x0, y0, x0 + CELL - 1, y0 + CELL - 1], fill=color)
    if mode != "RGB":
        im = im.convert(mode)
    im.save(dst)
    print(f"saved: {dst}")


if __name__ == "__main__":
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else SRC
    dst = Path(sys.argv[2]) if len(sys.argv) > 2 else DST
    fix(src, dst)
