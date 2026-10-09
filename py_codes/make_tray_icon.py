import io
import os
import struct
import numpy as np
from PIL import Image

# cat_modi_big.png(원본 cat_modi.png 의 8배 nearest 업스케일)에서 고양이 "얼굴"만 잘라
# 트레이 아이콘 assets/tray_face_{16,24,32,48,64}.png 와 exe/인스톨러용 assets/ccat.ico(16~256 멀티 사이즈)를 만든다.
#  - 8x8 블록을 1픽셀로 되돌려(네이티브 격자) 머리 영역만 자른다 (귀 + 머리 외곽선, 몸통/꼬리 제외)
#  - 배경(균일한 연한 청회색)은 투명 처리. 오른쪽 아래의 작은 잡티는 자르는 영역 밖이라 무시된다
#  - 정사각 투명 캔버스 중앙에 정수 배율(nearest)로 최대한 크게 배치 → 픽셀이 뭉개지지 않는다

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "cat_modi_big.png")
OUT_DIR = os.path.join(ROOT, "assets")
SIZES = (16, 24, 32, 48, 64)
ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)

BLOCK = 8                      # cat_modi_big.png 의 업스케일 배율
BG_TOLERANCE = 30              # 배경색과의 RGB 절대차 합이 이 이하면 배경

# 네이티브 격자 기준 머리 영역 (양끝 포함): 귀 끝 y=15 ~ 턱 y=24, 왼쪽 외곽선 x=5 ~ 오른쪽 외곽선 x=15
HEAD_BOX = (5, 15, 15, 24)     # (x0, y0, x1, y1)
# 머리 상자 안이지만 몸통(털)에 속하는 픽셀 → 제외
BODY_PIXELS = ((15, 23), (14, 24), (15, 24))


def load_native(path: str) -> np.ndarray:
    """8배 업스케일 이미지를 네이티브 해상도 RGBA 배열로 되돌린다."""
    img = Image.open(path).convert("RGBA")
    arr = np.array(img)
    h, w = arr.shape[0] // BLOCK, arr.shape[1] // BLOCK
    native = arr[BLOCK // 2::BLOCK, BLOCK // 2::BLOCK][:h, :w].copy()
    bg = native[0, 0, :3].astype(int)   # 모서리 색 = 배경
    diff = np.abs(native[:, :, :3].astype(int) - bg).sum(axis=2)
    native[diff <= BG_TOLERANCE, 3] = 0
    return native


def crop_head(native: np.ndarray) -> np.ndarray:
    x0, y0, x1, y1 = HEAD_BOX
    head = native[y0:y1 + 1, x0:x1 + 1].copy()
    for (x, y) in BODY_PIXELS:
        head[y - y0, x - x0, 3] = 0
    return head


def make_icon(head: np.ndarray, size: int) -> Image.Image:
    h, w = head.shape[:2]
    factor = max(1, size // max(w, h))   # 들어가는 가장 큰 정수 배율
    img = Image.fromarray(head, "RGBA").resize((w * factor, h * factor), Image.Resampling.NEAREST)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.paste(img, ((size - img.width) // 2, (size - img.height) // 2))
    return canvas


def write_ico(head: np.ndarray, path: str) -> None:
    """크기별 nearest 정수 배율 이미지를 PNG 로 담은 멀티 사이즈 ICO 를 직접 쓴다 (Pillow 의 ICO 저장은 재샘플링해 픽셀이 뭉개진다)."""
    pngs = []
    for size in ICO_SIZES:
        buf = io.BytesIO()
        make_icon(head, size).save(buf, format="PNG")
        pngs.append(buf.getvalue())
    out = bytearray(struct.pack("<HHH", 0, 1, len(pngs)))   # reserved, type=icon, count
    offset = 6 + 16 * len(pngs)
    for size, data in zip(ICO_SIZES, pngs):
        dim = 0 if size >= 256 else size                   # 0 = 256
        out += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for data in pngs:
        out += data
    with open(path, "wb") as f:
        f.write(out)


def main():
    native = load_native(SRC)
    head = crop_head(native)
    print(f"머리 영역(네이티브 px): x={HEAD_BOX[0]}..{HEAD_BOX[2]}, y={HEAD_BOX[1]}..{HEAD_BOX[3]} "
          f"-> {head.shape[1]}x{head.shape[0]}")
    os.makedirs(OUT_DIR, exist_ok=True)
    for size in SIZES:
        out = os.path.join(OUT_DIR, f"tray_face_{size}.png")
        make_icon(head, size).save(out, format="PNG")
        print(f"저장: {out}")
    ico = os.path.join(OUT_DIR, "ccat.ico")
    write_ico(head, ico)
    print(f"저장: {ico} ({', '.join(str(s) for s in ICO_SIZES)})")


if __name__ == "__main__":
    main()
