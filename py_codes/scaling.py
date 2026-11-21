import os
from PIL import Image

import argparse

def main():
    parser = argparse.ArgumentParser(description="Nearest-neighbor 픽셀아트 업/다운스케일러")
    parser.add_argument("--dir", required=True, help="이미지 파일 경로")

    # Exactly one of -u / -d must be given
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("-u", "--upscale", type=int, metavar="N", help="N배 업스케일")
    mode.add_argument("-d", "--downscale", type=int, metavar="N", help="한 칸 N px -> 1 px 다운스케일")

    args = parser.parse_args()

    # Reject non-positive factors early
    factor = args.upscale if args.upscale is not None else args.downscale
    if factor <= 0:
        parser.error("배수는 1 이상의 정수여야 해요.")

    input_file = args.dir.strip().strip("'\"")
    if not os.path.exists(input_file):
        parser.error(f"'{input_file}' 파일을 찾을 수 없어요.")

    if args.upscale is not None:
        upscale_8x_nearest(input_file, args.upscale)
    else:
        downscale_cell_to_pixel(input_file, args.downscale)

def upscale_8x_nearest(input_path: str, output_scailing: int):
    file_name, _ = os.path.splitext(input_path)
    output_path = f"{file_name}_big.png"

    with Image.open(input_path) as img:
        target_w = img.width * output_scailing
        target_h = img.height * output_scailing
        
        # 각 픽셀을 8x8 정사각형 블록으로 그대로 복제
        upscaled = img.resize((target_w, target_h), resample=Image.Resampling.NEAREST)
        
        upscaled.save(output_path, format="PNG")
        print(f"완료: {img.size} -> {upscaled.size}")
        print(f"저장 경로: {output_path}")

def downscale_cell_to_pixel(input_path: str, cell_size: int):
    file_name, _ = os.path.splitext(input_path)
    output_path = f"{file_name}_small.png"

    with Image.open(input_path) as img:
        # Warn if the image size is not an exact multiple of the cell size
        if img.width % cell_size or img.height % cell_size:
            print(f"경고: 이미지 크기 {img.size}가 {cell_size}의 배수가 아니에요. 남는 가장자리는 잘려요.")

        target_w = img.width // cell_size
        target_h = img.height // cell_size

        # NEAREST samples the center pixel of each n x n cell
        downscaled = img.resize((target_w, target_h), resample=Image.Resampling.NEAREST)

        downscaled.save(output_path, format="PNG")
        print(f"완료: {img.size} -> {downscaled.size}")
        print(f"저장 경로: {output_path}")

if __name__ == "__main__":
    main()