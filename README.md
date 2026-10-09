# DontGoCat (CCat) — 데스크탑 상주 픽셀 고양이

> 배포 이름은 **DontGoCat** (`DontGoCat.exe`). 소스 내부 이름(CMake 타깃 `ccat`, `ccat_*` 라이브러리, `CCAT_` 매크로, 개발 도구 `ccat_preview`/`ccat_scan_dump`, 로그 카테고리 `ccat.brain`, 파일명 `ccat.iss`/`ccat.ico` 등)은 그대로 `ccat` 를 쓴다.

치즈 태비 픽셀 고양이가 작업표시줄과 열려 있는 창 위를 걸어 다니는 Windows 데스크탑 상주 프로그램.
Qt 6.11 (Widgets) + Win32 API, C++20.

> 이 문서는 구현 명세다. 각 모듈 구현자는 자기 모듈 절과 **공통 규칙**을 반드시 지킨다.
> 명세에 없는 결정이 필요하면 가장 보수적인 선택을 하고 코드에 `// TODO(decision): ...` 로 남긴다.

---

## 1. 요구사항 (원문 정리)

| # | 요구사항 | 구현 위치 |
|---|---|---|
| R1 | 고양이는 떠다니지 않는다. **작업표시줄 윗면, 창 윗면(지붕), 창 좌우 측면(벽)** 에만 붙어서 이동한다. 면 사이 이동은 점프(포물선), 발판이 사라지면 중력 낙하. 창 아랫면에 거꾸로 매달리기는 하지 않는다. | Surface, DesktopScanner, Locomotion, PathPlanner |
| R2 | 도달 가능한 화면의 모든 곳(다중 모니터 포함)을 자유롭게 돌아다닌다. 고양이가 있는 모니터에 전체화면 앱이 뜨면 숨었다가 끝나면 복귀한다. | DesktopScanner, CatBrain |
| R3 | 종료는 시스템 트레이에서만. 트레이 아이콘은 `cat_modi_big.png` 의 고양이 **얼굴만** 잘라서 사용한다. | TrayController, `py_codes/make_tray_icon.py` |
| R4 | 커서가 트레이 쪽으로 가면 고양이가 **천천히 걸어서** 트레이 쪽으로 온다. 종료 버튼을 누르려 하면 몸으로 가려 방해한다. 방해 확률 80%, 직전 시도 후 10초 내 재시도 시 직전 확률 −10%p. **Shift+클릭은 즉시 종료.** | TrayController, QuitPopup, QuitGuard, CatBrain |
| R5 | 평소 고양이는 클릭을 통과시킨다 (아래 버튼 클릭에 지장 없음). | CatOverlay |
| R6 | 고양이가 도달 가능한 곳에 커서가 있고, 커서가 5초 이상 움직이지 않으면(키보드 입력 무관) 다가가 장난친다: 툭툭(커서 ±1~2px 실제 이동), 근처 배회, 뒹굴기. | MouseWatcher, CatBrain |
| R7 | 부족한 동작은 현재 화풍(`cat_modi_big.png`, `sheets/idle.png`)을 유지해 추가한다: **climb, roll, sprawl, crouch**. | CatSprite, CatAnim |
| R8 | 표시 배율 기본 3x(96×72px). 1x/2x/3x/4x 를 트레이 팝업과 명령줄 `--scale N` 으로 바꿀 수 있고 QSettings에 저장한다. | Config, TrayController, CatApp |

---

## 2. 빌드 / 실행

```
scripts\build.cmd [빌드폴더=build] [타깃]
```

- 툴체인: VS 18 BuildTools (`vcvarsx86_amd64.bat`), Ninja, Qt `B:/Qt/6.11.2/msvc2022_64`.
- 실행 파일
  - `DontGoCat.exe` — 데스크탑 상주 앱 (`--scale N` 지원). CMake 타깃 이름은 `ccat`, `OUTPUT_NAME` 으로 `DontGoCat.exe` 출력
  - `ccat_preview.exe` — 애니메이션 미리보기 툴 (기존 main.cpp). 회전(Down/Left/Right) 미리보기, `--export <dir>` 로 시트 PNG 출력 + jitter/회전 무손실 검사. **`sheets/` 는 손질한 레퍼런스이므로 다른 폴더로 출력할 것.**
  - `ccat_scan_dump.exe` — 개발용. 데스크탑 스캔 결과(모니터, 바닥, 트레이, 면 세그먼트) 출력. `--watch`, `--mouse`, `--scale N`
- 행동 로그: `ccat.brain` 로깅 카테고리. `QT_FORCE_STDERR_LOGGING=1` 로 실행하고 stderr 를 파일로 리다이렉트하면 상태 전환/경로 step 이 기록된다.
- 배율 설정은 QSettings `DontGoCat/DontGoCat` 의 `scale` 키에 저장된다.
- Python 보조 스크립트는 `.venv` 로 실행 (pillow, numpy만 설치됨). 트레이 아이콘 재생성: `.venv\Scripts\python.exe py_codes\make_tray_icon.py`

### 배포

1. Inno Setup 6 설치 (인스톨러를 만들 때만 필요): `winget install --id JRSoftware.InnoSetup -e` 또는 https://jrsoftware.org
2. `scripts\package.cmd` — `build-release` 에 Release 빌드 → `dist\DontGoCat\` (DontGoCat.exe + 필요한 Qt DLL/플러그인 + MSVC 런타임, 약 28MB) → `dist\DontGoCat-<버전>-portable.zip` → `dist\DontGoCat-Setup-<버전>.exe`.
   Inno Setup 이 없으면 zip 까지만 만들고 안내 메시지를 출력한다. 버전은 `CMakeLists.txt` 의 `project(DontGoCat VERSION ...)` 한 곳에서 바꾼다. 자세한 내용은 6절.

### 테스트

```
scripts\build.cmd            # tests/ 의 실행 파일도 함께 빌드 (CMake 옵션 CCAT_BUILD_TESTS, 기본 ON)
scripts\test.cmd             # vcvars 환경에서 ctest --output-on-failure (추가 인자는 ctest 로 전달: -R test_brain 등)
```

- Qt DLL 경로(`PATH`)와 `QT_QPA_PLATFORM=offscreen`(위젯 테스트) 은 `tests/CMakeLists.txt` 가 test 환경에 넣는다. 자체 경량 `CHECK` 매크로(`tests/TestCheck.hpp`)만 쓰며 실패 시 종료 코드 1.
- 모든 테스트는 합성 `DesktopSnapshot`(`tests/TestSnapshot.hpp`) + 가상 시간만 사용한다. 실제 데스크탑 스캔, 커서 이동, 트레이 아이콘은 건드리지 않는다. 커서 툭툭(`MouseWatcher::nudge`)은 `setNudgeHook` 으로, 행동 난수는 `CatBrain::setRandomGenerator` 로 가로챈다 (`QRandomGenerator::global()` 은 시드 고정 금지).
- `test_motion` — Locomotion + PathPlanner (~570 검사): 스프라이트 앵커, 디딤발 걷기, 방향/중력, 창 따라 이동, 낙하(터널링 없음), 점프, 모서리, 가장자리 이탈, 대시, 배율 변경, 경로 계획(도달 불가 포함), 계획 시간(상한은 넉넉히, Debug 는 더 느슨). `--real-desktop` 을 주면 실제 데스크탑 스캔 점검도 수행(수동 전용, ctest 는 사용 안 함).
- `test_brain` — CatBrain (~75 검사): 30분 자율 행동 soak, 벽 위 제한, 트레이 접근, 종료 방해 추적/포기, 마우스 장난, 숨김/낙하, 경로 재계획. `--seed N` 으로 난수 시드 변경 (기본 1).
- `test_popup` — QuitPopup + TrayController 클릭 규칙 (~70 검사): 모든 배율에서 종료 버튼 64×16 고정(`Config::kQuitButtonW/H`)과 팝업 하단 = 바닥 y, 자동 시작 체크박스가 배율 줄 아래·종료 버튼 위에 위치, 버튼 클릭 = 종료(시도 아님) / 방해 중 고양이 클릭 = 재시도 / Shift+클릭 = 종료, 체크박스 토글 → 등록/삭제/실패 시 되돌림. `QuitGuard` 난수는 `TrayController` 생성자로 주입.
- `test_autostart` — AutoStart (~20 검사): 켜기(따옴표 친 절대 경로)/끄기/오래된 경로 갱신/실패. **실제 Run 키는 건드리지 않고** `HKCU\Software\CCatTest` 임시 키만 쓰며 끝에서 삭제한다 (`TrayController::setAutoStartKey` 로 팝업 테스트도 임시 키 사용).

---

## 3. 공통 규칙

### 3.1 좌표계
- **모든 데스크탑 좌표는 가상 데스크탑 물리 픽셀(int)** 이다. Win32 API 값과 1:1.
- `main()` 에서 `QApplication` 생성 전에 `qputenv("QT_ENABLE_HIGHDPI_SCALING", "0")` 를 설정해 Qt 좌표 = 물리 픽셀로 맞춘다.
- 창 사각형은 `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)` 로 얻는다 (보이지 않는 리사이즈 테두리 제외).

### 3.2 픽셀 아트 규칙 (기존 코드 규칙 유지)
- 스프라이트는 32×24 캔버스, 기본 방향 왼쪽, 키포즈만 표시(프레임 사이 보간 없음).
- 확대는 정수 배율 nearest-neighbor 만 사용. 회전은 90° 단위만 (무손실).
- 걷기/달리기/오르기 중 고양이는 프레임마다 정확히 `speedPxPerFrame × scale` 물리 px 만큼 이동한다 (디딤발 고정).
- 점프/낙하는 물리 px 단위 연속 이동 허용.
- 새 파츠 맵은 기존 팔레트 키(`. o f s c b i n d`)만 사용하고, 외곽선은 `render()` 의 자동 외곽선 규칙을 따른다.

### 3.3 고양이 방향(회전) 규칙
고양이가 붙은 면에 따라 "발이 향하는 방향"을 `CatGravity` 로 표현한다. 기본 스프라이트(왼쪽 보기, 발 아래)를 다음과 같이 변환한다.

| 붙은 면 | CatGravity | 변환 | 기본 Left 보기 → | 기본 Right 보기 → |
|---|---|---|---|---|
| 바닥(작업표시줄/창 지붕) | `Down` | 없음 | 왼쪽으로 진행 | 오른쪽으로 진행 |
| 창의 **오른쪽** 측면 (고양이는 창 오른쪽 바깥, 벽이 고양이 왼쪽) | `Left` | 시계 방향 90° | 위로 오름 | 아래로 내려감 |
| 창의 **왼쪽** 측면 (고양이는 창 왼쪽 바깥, 벽이 고양이 오른쪽) | `Right` | 반시계 방향 90° | 아래로 내려감 | 위로 오름 |

- 좌우 반전(facing)을 먼저 적용하고 그 다음 회전한다. 구현: `CatSprite::renderOriented()`.
- 앵커(고양이 위치 기준점) = 기본 프레임에서 캔버스 **하단 중앙** (발 닿는 선의 중앙). 회전 후에도 이 점이 면 위에 놓인다.

### 3.4 클릭 통과
- 고양이 오버레이 창은 기본적으로 `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW` (클릭 통과, 포커스 안 뺏음, 작업표시줄/Alt+Tab 미표시).
- **종료 버튼을 가리고 있는 동안만** `WS_EX_TRANSPARENT` 를 해제한다. 레이어드 창이므로 알파 0 픽셀은 여전히 통과되고, 고양이 몸(불투명 픽셀)만 클릭을 받는다.
- 작업표시줄을 클릭하면 작업표시줄이 최상위로 올라오므로 매 스캔 주기마다 `SetWindowPos(HWND_TOPMOST, ..., SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE)` 로 재확인한다.

### 3.5 튜닝 상수
모든 수치는 `src/include/Config.hpp` 한 곳에 둔다. 모듈 코드에 매직 넘버를 직접 쓰지 않는다.
거리 상수 중 "스프라이트 px" 로 정의된 값은 사용 시 `× scale` 한다.

---

## 4. 아키텍처

```
                   ┌──────────────┐  scan 250ms   ┌────────────────┐
                   │DesktopScanner│──────────────▶│ DesktopSnapshot │ (모니터, 작업표시줄, 창, 면 세그먼트, 전체화면)
                   └──────────────┘               └───────┬────────┘
┌────────────┐ cursor 30Hz                                │
│MouseWatcher│──────────┐                                 ▼
└────────────┘          │   ┌──────────┐ goal  ┌───────────┐ route ┌────────────┐
┌──────────────┐ signals├──▶│ CatBrain │──────▶│PathPlanner│──────▶│ Locomotion │ (면 부착, 걷기/오르기/점프/낙하, 포즈 선택)
│TrayController│────────┘   └────┬─────┘       └───────────┘       └─────┬──────┘
│ ├ QuitPopup  │◀── block/release │                                     │ frame(pose, gravity, facing, anchor)
│ └ QuitGuard  │                  ▼                                     ▼
└──────────────┘             ┌─────────┐                          ┌────────────┐
                             │ CatApp  │ 16ms tick, 배선, 설정      │ CatOverlay │ (투명 최상위 창, 클릭 통과 토글)
                             └─────────┘                          └────────────┘
```

### 4.1 CMake 타깃

| 타깃 | 종류 | 파일 | 담당 |
|---|---|---|---|
| `ccat_sprite` | STATIC | CatSprite, CatAnim | WP1 |
| `ccat_preview_ui` | STATIC | CatWidget | WP1 |
| `ccat_platform` | STATIC | Surface, DesktopScanner, MouseWatcher (+ user32, dwmapi, shell32) | WP2 |
| `ccat_ui` | STATIC | CatOverlay, QuitGuard, QuitPopup, TrayController, AutoStart (+ advapi32) | WP3 |
| `ccat_core` | STATIC | Locomotion, PathPlanner, CatBrain, CatApp | WP4 |
| `ccat` | EXE (WIN32), 출력 `DontGoCat.exe` | src/app/main.cpp, resources/ccat.qrc, build/ccat.rc (resources/ccat.rc.in 에서 생성: 버전 리소스 + 앱 아이콘) | WP4 |
| `ccat_preview` | EXE | src/tools/preview_main.cpp | WP1 |
| `test_motion` `test_brain` `test_popup` | EXE (ctest) | tests/*.cpp | 테스트 (2절 참고) |

### 4.2 파일 구조
```
src/
  app/main.cpp              데스크탑 앱 진입점
  tools/preview_main.cpp    애니메이션 미리보기 (기존 main.cpp 이동)
  include/  *.hpp
  module/   *.cpp
resources/ccat.qrc          트레이 아이콘 등
assets/                     tray_face_{16,24,32,48,64}.png, ccat.ico (생성물)
py_codes/make_tray_icon.py  얼굴 자르기 스크립트 (PNG + ico)
scripts/build.cmd           빌드 스크립트
scripts/test.cmd            ctest 실행 스크립트
scripts/package.cmd         Release 빌드 + dist\DontGoCat + zip + 인스톨러 (package_no_iscc.txt = ISCC 없음 안내문)
installer/ccat.iss          Inno Setup 6 스크립트
tests/                      테스트 (TestCheck.hpp, TestSnapshot.hpp, test_*.cpp)
LICENSE                     MIT (Copyright (c) 2026 Seung)
THIRD_PARTY_NOTICES.txt     배포물에 포함된 Qt(LGPLv3) / MSVC 런타임 / Inno Setup 고지
licenses/                   제3자 라이선스 전문 (Qt-LGPL-3.0-GPL-3.0.txt = B:/Qt/Licenses/LICENSE 사본)
.gitattributes              *.cmd / *.iss 는 CRLF 고정
```

---

## 5. 모듈 명세

### 5.1 CatSprite / CatAnim — 새 동작 (WP1)

기존 애니메이션: idle, walk, run, sit, sleep, jump_up, fall, land, paw_swipe.
`CatAnim` enum 의 `Count` 앞에 다음을 추가한다 (기존 값 순서 유지).

| 이름 | loop | 설명 | 사용처 |
|---|---|---|---|
| `climb` | O | 벽타기 전용. 기본 프레임(발 아래)에서 그리고 회전해 쓴다. 몸통을 길게 뻗고 앞발이 번갈아 앞으로 크게 뻗어 걸치는 동작, 꼬리 Low. `computeSpeed` 규칙(디딤발 y=0 연속 2프레임)으로 속도가 계산되도록 디딤발을 설계한다. | 벽 오르내리기 |
| `mantle` | X | 벽 꼭대기 → 창 지붕으로 넘어가는 2~3프레임 (앞발 걸치고 몸 끌어올리기). 바닥 프레임 기준으로 그리며 Locomotion 이 회전 전환 시점을 잡는다. | 모서리 전환 |
| `roll` | O | 등을 대고 누워 배(cream)를 보이며 좌우로 뒹굴. 새 몸통 맵 필요. | 마우스 장난 |
| `sprawl` | O | 옆으로 길게 엎드린 자세(몸 낮고 길게, 앞발 앞으로 뻗음), 꼬리만 살랑, 가끔 깜빡임. | 자율 휴식, 방해 해제 후 |
| `crouch` | O | 몸을 낮추고 엉덩이를 좌우로 실룩(사냥 준비). 눈은 크게 뜸. | 마우스 장난 |

- 필요하면 `CatBody` 에 새 몸통 형태(`Sprawl`, `Roll` 등), `CatTail` 에 새 꼬리 형태를 추가한다.
- `CatSprite::renderOriented(pose, gravity, facing)` 구현 (3.3 표).
- `CatSprite::opaqueBounds(anim)` : 해당 애니메이션 모든 프레임의 불투명 영역 합집합/교집합 사각형(스프라이트 px). QuitPopup 이 종료 버튼 크기를 sprawl **교집합** 보다 작게 잡는 데 쓴다.
- 미리보기 툴(`ccat_preview`)에 회전(`CatGravity`) 선택을 추가하고, `--export sheets` 로 새 시트를 출력해 시각 확인한다. 지글거림(jitter) 0 유지.

### 5.2 Surface / DesktopScanner (WP2)

```cpp
enum class SurfaceKind { Floor, WallLeftSide, WallRightSide };
// Floor        : 수평선 y, 구간 [a, b) x. 고양이는 선 위(y 감소 방향)에 선다.
// WallLeftSide : 창의 왼쪽 측면. 수직선 x, 구간 [a, b) y. 고양이는 선 왼쪽에 붙는다 (gravity Right).
// WallRightSide: 창의 오른쪽 측면. 고양이는 선 오른쪽에 붙는다 (gravity Left).
```

- **스캔 주기** 250ms (`Config::kScanIntervalMs`). `EnumWindows` 는 z-order 순(앞→뒤)으로 순회한다.
- **창 필터**: `IsWindowVisible`, `!IsIconic`, `DWMWA_CLOAKED == 0`, `WS_EX_TOOLWINDOW` 제외, 자기 프로세스 창 제외, `Progman`/`WorkerW`/`Shell_TrayWnd`/`Shell_SecondaryTrayWnd` 제외, 최소 크기(`kMinWindowW/H`) 미만 제외.
- **면 추출**: 창마다 지붕(Floor), 왼쪽 측면, 오른쪽 측면 3개 엣지.
  - **가림 처리**: 앞쪽 창이 덮고 있는 엣지 구간을 빼서 보이는 세그먼트만 남긴다(엣지 선 자체 기준).
  - **화면 밖 처리**: 고양이가 엣지에 붙었을 때 몸이 가상 데스크탑(모니터 합집합) 밖으로 나가는 구간은 제외. 최대화 창 지붕/측면은 자연히 제외된다.
  - 구간 길이 < 고양이 몸길이(`kMinSegmentSpritePx × scale`)이면 제외.
- **바닥(작업표시줄)**: 모니터마다 `Shell_TrayWnd`/`Shell_SecondaryTrayWnd` 의 윗면. 작업표시줄이 하단이 아니거나 자동 숨김이면 그 모니터의 **작업 영역 하단**을 바닥으로 쓴다. 따라서 모든 모니터에 항상 바닥이 존재한다.
- **식별자**: `SurfaceId { HWND owner (작업표시줄 바닥은 nullptr + monitorIndex), SurfaceKind kind }`. 창이 움직이면 같은 id 로 새 좌표가 나오므로 그 위의 고양이는 창을 따라 이동한다.
- **전체화면 감지**: 전경 창이 모니터 전체 사각형을 덮고 셸 창이 아니면 그 모니터는 fullscreen. 추가로 `SHQueryUserNotificationState` 가 `QUNS_BUSY`/`QUNS_RUNNING_D3D_FULL_SCREEN`/`QUNS_PRESENTATION_MODE` 이면 전경 창의 모니터를 fullscreen 으로 본다.
- **트레이 위치**: `Shell_TrayWnd` → `TrayNotifyWnd` 사각형을 스냅샷에 포함 (QSystemTrayIcon::geometry() 가 비었을 때 대체).
- 스냅샷 쿼리 함수: 점 아래의 첫 바닥 찾기(낙하용), id 로 세그먼트 찾기, 점이 세그먼트 위에 있는지, 점이 속한 모니터.

### 5.3 MouseWatcher (WP2)

- 30Hz 로 `GetCursorPos` 폴링. 위치가 바뀌면 `lastUserMoveTime` 갱신.
- **자기 이동 무시**: `nudge(dx, dy)` 로 커서를 옮길 때 기대 위치를 기록하고, 다음 폴링에서 그 위치면 사용자 이동으로 치지 않는다.
- `idleMs()` : 마지막 사용자 이동 후 경과 ms. 키보드 입력과 무관.
- `anyButtonDown()` : `GetAsyncKeyState(VK_LBUTTON|VK_RBUTTON|VK_MBUTTON)`. 눌린 상태면 `nudge` 는 아무것도 하지 않는다.
- 시그널: `userMoved(QPoint)`, `becameIdle(QPoint)` (`kMouseIdleMs` = 5000 경과 시 1회).

### 5.4 CatOverlay (WP3)

- 고정 크기 정사각형 창: 한 변 `32 × scale` (모든 회전을 담음). `Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus`, `WA_TranslucentBackground`, `WA_ShowWithoutActivating`.
- 생성 후 네이티브 스타일에 3.4 의 확장 스타일을 설정.
- `setFrame(QImage orientedSprite, QPoint anchorDesktop, CatGravity)` : 회전된 스프라이트의 앵커가 `anchorDesktop` 에 오도록 창을 배치하고 다시 그린다. 이동은 `SetWindowPos(..., SWP_NOACTIVATE)`.
- `setClickThrough(bool)` : `WS_EX_TRANSPARENT` 토글. 해제 상태에서 고양이 클릭 시 `clicked(Qt::KeyboardModifiers)` 시그널.
- `setScale(int)`, `ensureTopmost()`.

### 5.5 QuitGuard (WP3) — 순수 로직

```
attempt(now):
    if lastAttempt 존재 && now - lastAttempt <= 10s:  p = max(0, p - 10)
    else:                                             p = 80
    lastAttempt = now
    return random(0..99) < p    // true = 방해
```
- `시도` 는 (a) 종료 버튼 hover 진입 판정, (b) 방해가 진행 중일 때 고양이 몸 클릭 — 둘만 1회로 센다. 종료 버튼 자체의 클릭은 시도가 아니다 (5.6: 보이는 부분을 눌렀으면 곧바로 종료).
- 상수는 `Config::kQuitBlockBaseChance(80)`, `kQuitBlockStep(10)`, `kQuitRetryWindowMs(10000)`. 난수는 주입 가능하게 (테스트용).

### 5.6 QuitPopup / TrayController (WP3)

- 트레이 아이콘: `assets/tray_face_*.png` 를 담은 QIcon. 툴팁 "DontGoCat".
- 아이콘 좌클릭/우클릭 → **QuitPopup 토글**. (표준 QMenu 는 쓰지 않는다)
- QuitPopup: `Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint` (**`Qt::Popup` 금지** — 앱 전체 마우스를 잡아 고양이 클릭이 팝업 바깥 클릭으로 처리되기 때문). 앱 비활성화 / Esc / 아이콘 재클릭 시 닫힘.
  - 구성: 상단 얼굴 아이콘 + "DontGoCat", 배율 선택 1x/2x/3x/4x, **하단에 종료 버튼**.
  - 위치: 팝업 **하단 = 작업표시줄 윗면(바닥 y)**, 가로는 트레이 아이콘 근처(화면 밖으로 나가지 않게). → 바닥을 걷는 고양이가 그대로 종료 버튼 앞을 덮을 수 있다.
  - 종료 버튼 크기 = `Config::kQuitButtonW × kQuitButtonH` = **64×16 물리 px 고정** (배율과 무관, 32×8 스프라이트 px 의 2x. 배율 변경 시 팝업은 재배치되지만 버튼 크기는 그대로). 버튼의 하단도 바닥 y 에 맞닿게 배치. 높이는 고양이 crouch/idle 몸통 높이보다 낮게 유지해 커서 밑에 들어간 고양이 몸이 버튼 높이를 덮게 한다.
- 흐름:
  1. 종료 버튼 hover 진입(방해 중이 아닐 때) → `QuitGuard::attempt()`. 방해면 `blockRequested(QRect buttonRectDesktop)` 시그널.
  2. 방해 중: **고양이 몸 클릭**(`CatOverlay::clicked` → `onCatClicked`)만 재시도로 `attempt()` 재판정. 방해 유지면 `blockReact()` (고양이 paw_swipe), 실패면 `blockReleased()` (고양이가 옆으로 비켜 앉음). 반면 **고양이가 덮지 않은 버튼의 보이는 부분을 클릭하면 재판정 없이 곧바로 종료**한다 (고양이 오버레이는 클릭 통과 해제 시 불투명 픽셀만 클릭을 받으므로, 버튼에 도달한 클릭은 곧 덮이지 않은 부분의 클릭).
  3. 방해 아님: 버튼 클릭 → 종료 (고양이 클릭은 무시).
  4. **Shift+클릭** (버튼이든, 덮고 있는 고양이든) → 즉시 `QApplication::quit()`.
  5. 팝업 닫힘 → `blockReleased()`.
- 트레이 접근 감지: 커서가 트레이 아이콘 사각형(없으면 `TrayNotifyWnd`) 중심에서 `kTrayApproachRadiusPx` 이내 → `trayApproach(true)`, 벗어나 `kTrayLeaveDelayMs` 경과 && 팝업 닫힘 → `trayApproach(false)`.
- 배율 선택 → `scaleChanged(int)`.

### 5.7 Locomotion (WP4)

고양이 몸의 물리/운동 상태. 매 tick(16ms) 갱신, 애니메이션 프레임 타이밍은 `animInfo().frameMs` 를 따른다.

- 상태: `Attached{SurfaceId, offset(창 기준 상대 좌표), facing}` / `Airborne{pos, vel}` .
- 동작 명령: `walkTo(offset)`, `runTo(offset)`, `climbTo(offset)`, `jumpTo(SurfaceId, offset)`, `playInPlace(CatAnim)`, `stop()`.
- **걷기/오르기**: 프레임이 넘어갈 때만 `speedPxPerFrame × scale` 이동 (디딤발 고정). 바닥은 walk/run, 벽은 climb.
- **모서리 전환**
  - 바닥 끝 → 그 바닥 아래로 이어지는 벽(같은 창이 아님에도 붙은 경우는 무시): 하지 않음. 바닥 끝에서 벗어나면 낙하.
  - 벽 아래끝이 바닥 위 `kCornerTolerancePx` 이내 → 바닥↔벽 직접 전환 (sit 1프레임 후 회전).
  - 벽 위끝 → 같은 창 지붕: `mantle` 재생 후 Floor 로 전환.
- **점프**: 이륙점→착지점 포물선. 상승 중 `jump_up`, 하강 중 `fall`, 착지 시 `land`. 한계 `kJumpMaxUpSpritePx`, `kJumpMaxDxSpritePx` (× scale). 아래로는 거리 제한 없음.
- **낙하**: 매 스캔 후 부착 면이 사라졌거나 offset 이 세그먼트 밖이면 Airborne 으로 전환, 중력 `kGravityPxPerS2` (× scale/3) 로 떨어져 아래 첫 바닥에 `land`. 낙하 중 지나치는 바닥과의 교차는 프레임 사이 선분 교차로 판정(관통 방지).
- **창 이동 추종**: Attached 상태의 위치는 항상 `창 좌표 + offset` 으로 계산 → 창을 드래그하면 고양이가 같이 움직인다.
- 출력: 현재 `CatPose`, `CatGravity`, `CatFacing`, 앵커 데스크탑 좌표 → CatOverlay.

### 5.8 PathPlanner (WP4)

- 입력: 스냅샷, 시작(부착 상태), 목표(SurfaceId + offset, 또는 데스크탑 점 → 가장 가까운 도달 가능 면 위 점).
- 그래프 노드: 세그먼트 양 끝점 + 세그먼트를 `kPlannerSampleSpritePx × scale` 간격으로 샘플링한 점.
- 간선: 같은 세그먼트 내 걷기, 모서리 전환(5.7), 점프(한계 이내), 바닥 끝에서 걸어 나가 낙하.
- 비용 = 예상 소요 시간. Dijkstra. 결과는 `Locomotion` 명령 시퀀스.
- 경로가 없으면 `std::nullopt` (해당 목표 포기).

### 5.9 CatBrain (WP4) — 행동 우선순위

높은 순서로 선점한다.

1. **Hidden**: 고양이 모니터가 fullscreen → 오버레이 숨김, 상태 정지. 해제 시 복귀(부착 면 재검증, 필요시 낙하).
2. **Falling**: 공중 상태는 착지까지 다른 행동 불가.
3. **QuitBlock (커서 추적 방해)**: `blockRequested` → 버튼 앞 바닥(트레이 모니터 바닥)으로 이동 후 `CatOverlay::setClickThrough(false)`.
   - **접근** = 경로(PathPlanner)를 따라 **빠른 run** — Run 프레임 시간을 `kBlockRunFrameMsScale` 배로 줄여 재생한다 (`Locomotion::moveAlong(…, run, frameScale)`). 프레임마다 정확히 `speedPxPerFrame × scale` 만 움직이므로 디딤발 고정 규칙은 유지되고, 프레임이 빨리 넘어갈 뿐이라 속도 ∝ 1/배율. 이 배율은 접근 중 run 이동에만 적용되며 평소 달리기는 그대로다.
   - 경로의 **마지막 같은 바닥 구간**(목표와 같은 바닥 세그먼트 위의 마지막 Move)은 run 대신 아래 **대시**(`Locomotion::dashAlong`)로 이동한다. 지붕/벽/점프 구간은 빠른 run·기존 동작 그대로.
   - **추적 영역** = 팝업 사각형을 `kGuardTrackMarginPx` 만큼 넓힌 영역. 커서가 이 안에 있으면 고양이는 바닥 위에서 **커서 x 바로 밑으로 몸 중심을 옮긴다** (목표 x 는 버튼 좌우 끝 ± 몸 절반으로 clamp).
   - 이동은 `Locomotion::dashAlong()` — run 애니메이션을 빠르게 재생하며 `kDashSpeedSpritePxPerS × scale` 로 미끄러지듯 이동 (이 모드에서만 디딤발 고정 규칙 예외).
   - 커서가 `kGuardSettleMs` 이상 멈추면 그 자리에서 `crouch` (몸으로 버튼을 막고 실룩), 커서가 다시 움직이면 즉시 대시.
   - 커서가 추적 영역 밖이면 버튼 중앙 앞에서 `sit` 하고 커서 쪽을 바라본다.
   - 고양이 클릭: Shift → 종료, 아니면 `QuitGuard` 재판정 (유지 시 `blockReact` → paw_swipe 1회 후 추적 계속, 실패 시 release). 버튼 클릭(고양이가 미처 못 덮은 보이는 부분)은 재판정 없이 곧바로 종료.
   - `blockReleased` → 클릭 통과 복구, 버튼 옆으로 비켜 `sit` 후 자율 행동.
   - 도착 전에도 방해는 유효하다 (아직 고양이가 못 덮은 버튼 부분을 누르면 종료).
   - 경로가 없어 도달 불가하면 즉시 `blockReleased` 처리(방해 포기).
4. **TrayApproach**: `trayApproach(true)` → 팝업 예정 위치(트레이 왼쪽 바닥)로 **walk(천천히)** 이동 후 `sit` 하고 커서 쪽을 바라봄.
5. **MousePlay**: 커서가 도달 가능(바닥 세그먼트 바로 위 `catHeight` 이내, 또는 벽 세그먼트 옆 `catWidth` 이내) && `idleMs ≥ 5000` && 버튼 안 눌림 → 접근(walk) → `crouch` → 다음을 무작위 반복:
   - **툭툭**: `paw_swipe`, 앞발이 가장 앞으로 나가는 프레임에 `MouseWatcher::nudge(±1~2px)`.
   - **배회**: 커서 근처 ±`kPlayWanderSpritePx` 를 walk 로 왕복.
   - **뒹굴기**: `roll` 2~4초.
   - 사용자가 마우스를 직접 움직이면 즉시 중단 → `idle`.
6. **Autonomous** (가중치 랜덤, `Config` 에 가중치): idle(2~6s), 현재 면 위 배회(walk), 다른 면으로 탐험(PathPlanner로 무작위 도달 가능 지점), sit, sleep(sit→sleep, 20~60s), paw_swipe.

### 5.10 CatApp (WP4)

- 모든 모듈 생성/배선, 16ms 메인 tick (`Qt::PreciseTimer`), 스캔 타이머.
- 시작 위치: 주 모니터 바닥 중앙에 `land` 로 등장.
- 배율: 명령줄 `--scale N`(1~4) > QSettings(`CCat/scale`) > 기본 3. 변경 시 오버레이·팝업 재배치.
- `QApplication::setQuitOnLastWindowClosed(false)`.

---

### 5.11 AutoStart — 윈도우 시작 시 자동 실행

- 트레이 팝업의 배율 줄 아래에 체크박스 **"윈도우 시작 시 실행"**. 종료 버튼은 계속 팝업 맨 아래(바닥 y)에 붙는다.
- 켜기 = `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 에 값 이름 `DontGoCat`, 데이터 `"<DontGoCat.exe 전체 경로>"` 기록. 끄기 = 값 삭제. 관리자 권한 불필요.
- 체크 상태는 레지스트리 값 존재 여부가 기준 (별도 설정 저장 없음). 기본값 꺼짐.
- 앱 시작 시 값이 있는데 경로가 현재 exe 와 다르면 (설치 위치 이동, 포터블 폴더 이동) 현재 경로로 갱신한다.
- 제거 프로그램은 이 값을 삭제한다 (§6 배포).

## 6. 배포

- **버전**: CMake `project(DontGoCat VERSION 1.0.0)` 한 곳에서 관리 → exe 버전 리소스, 인스톨러에 자동 반영. 제품명/게시자 `DontGoCat`.
- **아이콘**: `assets/ccat.ico` (tray_face 를 16~256 멀티 사이즈로, nearest-neighbor). `py_codes/make_tray_icon.py` 가 함께 생성. exe(.rc) 와 인스톨러에 사용.
- **Release 배포 폴더**: `scripts\package.cmd` 가 `build-release` 에 Release 빌드 → `dist\DontGoCat\` 에 `DontGoCat.exe` + `windeployqt --release` (불필요한 플러그인/DLL 제외: translations, network, svg, opengl sw, system d3d compiler 등) + MSVC 런타임 DLL app-local 동봉 + 라이선스 파일 (`LICENSE.txt` = 저장소 `LICENSE`, `THIRD_PARTY_NOTICES.txt`, `licenses\Qt-LGPL-3.0-GPL-3.0.txt`).
- **라이선스**: 앱 코드는 MIT. 바이너리 배포물은 Qt 를 LGPLv3 로 동적 링크하므로 위 라이선스 파일 3개를 반드시 함께 배포하고, Qt DLL 은 수정하지 않은 채 별도 파일로 둔다 (정적 링크·단일 exe 금지). Qt 버전을 올리면 `licenses/` 사본과 `THIRD_PARTY_NOTICES.txt` 의 버전/소스 링크도 갱신한다.
- **산출물**: `dist\DontGoCat-<버전>-portable.zip`, `dist\DontGoCat-Setup-<버전>.exe` (Inno Setup 6, `installer\ccat.iss`).
- **인스톨러**: 현재 사용자 설치(`PrivilegesRequired=lowest`, `%LOCALAPPDATA%\Programs\DontGoCat`), 시작 메뉴 바로가기, 설치 후 실행 옵션, 실행 중인 DontGoCat.exe 자동 종료(업데이트/제거 시), 제거 시 `HKCU\...\Run\DontGoCat` 값 삭제. `AppId` GUID 는 절대 바꾸지 않는다 (바꾸면 업데이트가 덮어쓰기 대신 따로 설치됨). 자동 시작 등록은 인스톨러가 하지 않는다 (트레이 팝업에서).
- ISCC 를 찾지 못하면 zip 까지만 만들고 안내 메시지를 출력한다.

## 7. 작업 패키지

| WP | 내용 | 선행 |
|---|---|---|
| WP1 | 새 애니메이션 5종 + `renderOriented` + `opaqueBounds` + 미리보기 툴 | - |
| WP2 | Surface, DesktopScanner, MouseWatcher | - |
| WP3 | 트레이 아이콘 생성 스크립트, CatOverlay, QuitGuard, QuitPopup, TrayController | - |
| WP4 | Locomotion, PathPlanner, CatBrain, CatApp, main | WP1~3 |
| WP5 | 통합 빌드, 실행 점검, 튜닝 | WP4 |

각 WP 는 자기 CMake 타깃만 빌드해 검증한다: `scripts\build.cmd build-<wp> <타깃>`.
