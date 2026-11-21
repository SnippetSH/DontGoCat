"""brown_dog.png 의 털색을 밝은 애프리콧 톤으로 바꾼다 (팔레트 1:1 치환).

픽셀 도트 그림이라 색 수가 적으므로, 기존 색을 지정한 새 색으로 그대로 바꾼다.
매핑에 없는 색(배경, 눈 하이라이트 등)은 건드리지 않는다.

사용법:
    .venv/Scripts/python.exe py_codes/lighten_dog.py [입력] [출력]
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "original_assets" / "brown_dog.png"
DST = ROOT / "original_assets" / "brown_dog_light.png"

# 기존 색 -> 새 색
PALETTE = {
    (212, 142, 82): (240, 206, 160),  # 털 하이라이트
    (197, 115, 62): (226, 176, 124),  # 털 기본색
    (165, 80, 50): (198, 142, 96),    # 털 그림자
    (143, 69, 46): (172, 116, 80),    # 다리 안쪽 진한 그림자
    (51, 21, 18): (84, 50, 38),       # 윤곽선/눈/코 (밝아진 털에 맞춰 살짝 완화)
}


def recolor(src: Path, dst: Path) -> None:
    im = Image.open(src)
    mode = im.mode
    arr = np.array(im.convert("RGBA"))
    rgb = arr[..., :3]
    out = arr.copy()
    for old, new in PALETTE.items():
        mask = np.all(rgb == old, axis=-1)
        out[mask, :3] = new
    result = Image.fromarray(out, "RGBA")
    if mode != "RGBA":
        result = result.convert(mode)
    result.save(dst)
    print(f"saved: {dst}")


if __name__ == "__main__":
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else SRC
    dst = Path(sys.argv[2]) if len(sys.argv) > 2 else DST
    recolor(src, dst)
