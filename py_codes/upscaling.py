import os
from PIL import Image

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

if __name__ == "__main__":
    input_file = input("업스케일할 이미지 파일명을 입력하세요: ").strip().strip("'\"")
    output_scailing = int(input("업스케일 배수를 입력하세요: "))

    if os.path.exists(input_file):
        upscale_8x_nearest(input_file, output_scailing)
    else:
        print(f"오류: '{input_file}' 파일을 찾을 수 없습니다.")