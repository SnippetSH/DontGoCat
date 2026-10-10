import argparse
from pathlib import Path
from PIL import Image

def resize_pixel_art(input_path: Path, output_path: Path, align: str = "original"):
    # 1. 이미지 열기 (RGBA 모드 보장)
    img = Image.open(input_path).convert("RGBA")
    orig_w, orig_h = img.size

    # 2. 투명이 아닌 실제 그림 영역(바운딩 박스) 탐색
    bbox = img.getbbox()
    if not bbox:
        img.save(output_path)
        return

    left, upper, right, lower = bbox
    cropped = img.crop(bbox)

    # 3. 8px -> 4px 축소 (가로/세로 0.5배, Nearest Neighbor 보간)
    new_w = max(1, cropped.width // 2)
    new_h = max(1, cropped.height // 2)
    resized_art = cropped.resize((new_w, new_h), resample=Image.Resampling.NEAREST)

    # 4. 원본과 동일한 크기의 투명 캔버스 생성
    result_canvas = Image.new("RGBA", (orig_w, orig_h), (0, 0, 0, 0))

    # 5. 위치 결정 후 합성
    if align == "center":
        paste_x = left + (cropped.width - new_w) // 2
        paste_y = upper + (cropped.height - new_h) // 2
    elif align == "canvas_center":
        paste_x = (orig_w - new_w) // 2
        paste_y = (orig_h - new_h) // 2
    else:
        paste_x = left
        paste_y = upper

    result_canvas.paste(resized_art, (paste_x, paste_y), mask=resized_art)

    # 6. 저장
    result_canvas.save(output_path, "PNG")


def main():
    parser = argparse.ArgumentParser(description="Resize pixel art inside canvas.")
    parser.add_argument("-p", "--path", required=True, help="Input PNG file path")
    parser.add_argument(
        "--align",
        default="original",
        choices=["original", "center", "canvas_center"],
        help="Alignment mode for resized art (default: original)",
    )
    args = parser.parse_args()

    input_file = Path(args.path)
    if not input_file.is_file():
        raise FileNotFoundError(f"파일을 찾을 수 없습니다: {input_file}")

    # 동일한 디렉토리에 {기존이름}_resized.png 형태로 경로 생성
    output_file = input_file.with_stem(f"{input_file.stem}_resized")

    resize_pixel_art(input_file, output_file, align=args.align)
    print(f"변환 완료: {output_file}")


if __name__ == "__main__":
    main()