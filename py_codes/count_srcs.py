import argparse
from pathlib import Path

# 탐색 대상 확장자 정의
TARGET_EXTENSIONS = {".cpp", ".hpp", ".def"}


def main():
    parser = argparse.ArgumentParser(
        description="지정된 폴더를 재귀적으로 탐색하여 C++ 소스 파일(.cpp, .hpp, .def)의 글자 수 및 라인 수 통계를 계산합니다."
    )
    parser.add_argument("target_dir", type=str, help="탐색할 대상 폴더 경로")
    parser.add_argument(
        "--ignore-spaces",
        action="store_true",
        help="공백 및 개행 문자를 제외하고 글자 수를 계산할지 여부",
    )
    parser.add_argument(
        "--del",
        dest="exclude_dirs",
        nargs="*",
        default=[],
        help="탐색에서 제외할 폴더 경로 (여러 개 지정 가능)",
    )
    args = parser.parse_args()

    target_path = Path(args.target_dir).resolve()

    if not target_path.exists():
        print(f"오류: '{target_path}' 경로를 찾을 수 없습니다.")
        return

    if not target_path.is_dir():
        print(f"오류: '{target_path}' 경로가 폴더가 아닙니다.")
        return

    # 제외 대상 경로들을 절대 경로로 변환
    excluded_paths = {Path(p).resolve() for p in args.exclude_dirs}

    # 확장자별 통계 딕셔너리 초기화
    stats = {
        ext: {"files": 0, "lines": 0, "chars": 0}
        for ext in TARGET_EXTENSIONS
    }

    # 하위 폴더 재귀 탐색
    for file_path in target_path.rglob("*"):
        if not file_path.is_file():
            continue

        # .git 등 버전관리 디렉터리 기본 제외
        if ".git" in file_path.parts:
            continue

        # --del 옵션으로 지정한 폴더 하위 경로인지 확인 후 제외
        file_resolved = file_path.resolve()
        if any(file_resolved.is_relative_to(ex_path) for ex_path in excluded_paths):
            continue

        ext = file_path.suffix.lower()
        if ext not in TARGET_EXTENSIONS:
            continue

        # 텍스트 읽기 (UTF-8 우선, 실패 시 CP949 시도)
        try:
            text = file_path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            try:
                text = file_path.read_text(encoding="cp949")
            except Exception as e:
                print(f"[경고] {file_path} 인코딩 디코딩 실패: {e}")
                continue
        except (PermissionError, OSError) as e:
            print(f"[경고] {file_path} 파일 접근 실패: {e}")
            continue

        # 라인 수 계산 (마지막 줄 개행 여부와 관계없이 splitlines 기준으로 일관성 유지)
        lines = len(text.splitlines())

        # 글자 수 계산
        if args.ignore_spaces:
            char_count = sum(1 for ch in text if not ch.isspace())
        else:
            char_count = len(text)

        # 통계 누적
        stats[ext]["files"] += 1
        stats[ext]["lines"] += lines
        stats[ext]["chars"] += char_count

    # 총합 계산
    total_files = sum(s["files"] for s in stats.values())
    total_lines = sum(s["lines"] for s in stats.values())
    total_chars = sum(s["chars"] for s in stats.values())

    # 결과 출력
    space_label = "공백 제외" if args.ignore_spaces else "공백 포함"
    print(f"=== 탐색 경로: {target_path} ===")
    if excluded_paths:
        print(f"제외 경로: {', '.join(str(p) for p in excluded_paths)}")
    print("-" * 55)

    for ext in sorted(TARGET_EXTENSIONS):
        s = stats[ext]
        print(
            f"- {ext:<5}: {s['files']:,}개 파일 | {s['lines']:,}줄 | {s['chars']:,}자 ({space_label})"
        )

    print("-" * 55)
    print(f"총 파일 수     : {total_files:,}개")
    print(f"총 라인 수     : {total_lines:,}줄")
    print(f"총 글자 수     : {total_chars:,}자 ({space_label})")


if __name__ == "__main__":
    main()