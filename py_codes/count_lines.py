import sys
from pathlib import Path


def count_code_stats(target_dir: Path):
  cpp_count = 0
  hpp_count = 0
  cpp_lines = 0
  hpp_lines = 0

  # 대상 디렉터리 내부를 재귀적으로 순회
  for file_path in target_dir.rglob('*'):
    if not file_path.is_file():
      continue

    ext = file_path.suffix.lower()
    if ext not in ('.cpp', '.hpp'):
      continue

    try:
      # UTF-8 및 기타 인코딩 오류 방지
      with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
        line_count = sum(1 for _ in f)
    except Exception as e:
      print(f'[경고] {file_path} 읽기 실패: {e}')
      continue

    if ext == '.cpp':
      cpp_count += 1
      cpp_lines += line_count
    elif ext == '.hpp':
      hpp_count += 1
      hpp_lines += line_count

  return {
      'cpp_count': cpp_count,
      'hpp_count': hpp_count,
      'cpp_lines': cpp_lines,
      'hpp_lines': hpp_lines,
      'total_files': cpp_count + hpp_count,
      'total_lines': cpp_lines + hpp_lines,
  }


def main():
  if len(sys.argv) < 2:
    print('사용법: py count_lines.py <디렉터리 경로>')
    sys.exit(1)

  target_path = Path(sys.argv[1])

  if not target_path.exists() or not target_path.is_dir():
    print(f"오류: '{target_path}' 경로가 존재하지 않거나 디렉터리가 아니에요.")
    sys.exit(1)

  stats = count_code_stats(target_path)

  print(f'=== 탐색 경로: {target_path.resolve()} ===')
  print(f'- .cpp 파일 수: {stats["cpp_count"]:,}개 ({stats["cpp_lines"]:,}줄)')
  print(f'- .hpp 파일 수: {stats["hpp_count"]:,}개 ({stats["hpp_lines"]:,}줄)')
  print(f'- 전체 파일 수: {stats["total_files"]:,}개')
  print(f'- 전체 코드 라인 수: {stats["total_lines"]:,}줄')


if __name__ == '__main__':
  main()