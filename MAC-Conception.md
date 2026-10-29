# 작업: 데스크톱 고양이 펫 macOS 이식

Qt(C++)로 만든 Windows용 데스크톱 고양이 펫을 macOS로 이식한다.
아래 설계는 방향 제시일 뿐이며, 기존 코드 구조를 기준으로 맞춰 적용한다.

## 0. 구현 전 필수: 현재 코드 구조 파악
코드를 수정하기 전에 반드시 아래를 먼저 조사하고 요약 보고한 뒤 승인을 받는다.
- 전체 디렉터리 구조, 빌드 시스템(CMake/qmake), 클래스 간 의존 관계
- 고양이 창 구현 방식: 고양이 크기만 한 작은 창을 이동하는지, 전체 화면 투명 창인지 확인
- 걷는 표면 계산 로직: 작업표시줄 + 창 벽면·윗면을 어떻게 수집하고 가림 처리하는지
- 종료 방해 로직: 트레이 메뉴 근처로 이동 → 종료 버튼 클릭 차단 → 다음 시도를 막을지/종료할지 랜덤 결정하는 부분이 어디 있는지
- 전체화면 감지 방식
- Windows 전용 API(Win32 등)가 어디에 흩어져 있는지 목록화
보고 후, 아래 설계를 현재 구조에 맞게 어떻게 바꿀지 매핑안을 먼저 제시한다.
설계와 기존 구조가 충돌하면 기존 구조를 우선하고 차이를 설명한다.

## 작업 규칙
- 파일·폴더 삭제, 기존 코드 블록 삭제, 부작용 있는 명령 실행 전에는 반드시 확인을 받는다.
- 각 Phase 완료 시 멈추고 변경 요약 + 내가 직접 확인할 항목을 보고한다.
- Windows 빌드가 깨지지 않게 한다. 플랫폼 분기는 CMake `if(APPLE)` / `#ifdef Q_OS_MACOS`로 한정한다.
- 플랫폼 무관 로직(상태머신, 물리, 종료 방해 랜덤 결정, 스프라이트)은 공통 코드로 유지한다.

## 목표 구조
공통 C++: CatBrain(상태머신) · Physics · QuitGuard(랜덤 결정) · Sprite
플랫폼 계층: EnvProvider 인터페이스
- surfaces(): 걸을 수 있는 선분 목록 (Floor / DockTop / WindowTop / WindowWall)
- obstacles(): 피해야 할 영역 (Stage Manager 띠 등)
- trayAnchor(): std::optional<QRect> — 트레이 아이콘 위치, 숨겨져 있으면 nullopt
- shouldHide(): 전체화면 등 숨김 여부
구현체: WinEnvProvider(기존 코드 이동), MacEnvProvider(.mm, Obj-C++)
- CMake에 enable_language(OBJCXX), .mm 파일은 APPLE에서만 컴파일

## Phase 1: 고양이 창 프로토타입 (macOS)
고양이 창을 띄우고 이동만 되는 최소 상태를 만든다.
- 창 생성 후 네이티브 NSWindow 설정 (QWindow::winId() → NSView → window)
  - level = NSPopUpMenuWindowLevel + 1 (Dock·자체 팝업보다 위)
  - collectionBehavior = CanJoinAllSpaces | Stationary | IgnoresCycle
  - FullScreenAuxiliary는 넣지 않는다 (전체화면 Space에 안 나타나게)
  - hidesOnDeactivate = NO (Qt::Tool 창은 기본 YES라 앱 비활성 시 사라짐)
  - hasShadow = NO
- Info.plist에 LSUIElement = YES
완료 후 멈추고 아래 실측 항목을 내게 안내한다:
- Space 전환 시 고양이 유지 여부
- 네이티브 전체화면 Space에서 고양이 미표시 여부
- Stage Manager 사용 시 고양이 창이 왼쪽 띠로 묶이지 않는지
- 투명 픽셀 영역 클릭이 아래 창으로 통과되는지 (안 되면 setMask 또는 입력 투명 토글 검토)

## Phase 2: 표면 수집 (MacEnvProvider)
- WindowScanner: CGWindowListCopyWindowInfo(OnScreenOnly | ExcludeDesktopElements)
  - layer == 0만, 자기 PID 제외, alpha < 0.1 제외, 너무 작은 창 제외
  - 배열 순서 = 앞→뒤 Z순서. 이를 이용해 윗면은 앞쪽 창의 x구간, 벽면은 y구간을 빼서 보이는 선분만 남김
  - 고양이 폭보다 짧은 선분 제거
  - 창 제목은 쓰지 않는다 (화면 기록 권한 불필요 유지)
  - CG 전역 좌표와 Qt 좌표는 모두 좌상단 원점·포인트 단위이므로 변환 불필요 (멀티 모니터에서 검증)
- 100ms 폴링, kCGWindowNumber로 창 추적
  - 고양이가 서 있는 창이 이동하면 따라감, 사라지면(최소화·닫힘·Space 이탈) 낙하
- DockProbe: QScreen::geometry() vs availableGeometry() 차이로 Dock 방향·두께 산출, 하단 = 바닥
  - (선택) 손쉬운 사용 권한이 있을 때만 AX API로 Dock 아이콘 막대 실측 → DockTop 표면 추가. 권한 없어도 모든 기능 동작
- StageProbe: Stage Manager 켜짐 감지(com.apple.WindowManager GloballyEnabled),
  WindowManager 소유 창 bounds 합집합을 obstacles()로 등록
- 기존 Windows 표면 로직 중 공통화 가능한 부분(가림 계산 등)은 공통 코드로 뺀다

## Phase 3: 종료 방해 (macOS)
네이티브 NSMenu는 자체 추적 루프로 클릭을 판정해 고양이 창으로 클릭을 막지 못할 가능성이 높다.
- QSystemTrayIcon::setContextMenu() 대신 activated() 시그널에서 직접 만든 QWidget 팝업을 아이콘 아래에 표시
- 팝업 mousePressEvent에서 종료 버튼 영역과 고양이 영역이 겹치면 클릭 무효 + 차단 모션 + QuitGuard 랜덤 결정 (기존 공통 로직 재사용)
- 이동 경로 분기:
  - 고양이가 화면 상단 근처면 달려가 점프 → 팝업 가장자리에 매달림
  - 그 외엔 "천장 등장": 화면 밖 퇴장 후 메뉴 막대 바로 아래에서 낙하해 종료 버튼 위 착지
  - 천장 걷기 기능은 구현하지 않는다
- trayAnchor()가 nullopt(QSystemTrayIcon::geometry() 무효/화면 밖 = 메뉴 막대 아이콘 숨김 또는 자동 숨김)면:
  앱 재실행 이벤트(QEvent::ApplicationActivate 또는 단일 인스턴스 메시지) 수신 시 고양이 옆에 팝업 표시
- 기존 Windows 팝업과 공통화 가능하면 공통화 (1.2.0 통계 펼침 팝업과 같은 위젯 계층으로 가는 것 고려)

## Phase 4: 전체화면 숨김
- 1차: 네이티브 전체화면 Space는 Phase 1의 collectionBehavior로 처리 (실측 결과 반영)
- 2차: WindowScanner 결과에서 맨 앞 창 bounds == 화면 frame이면 shouldHide() = true (보더리스 전체화면 게임 대응)
- 기존 Windows 전체화면 감지 방식이 창 스캔 기반이면 공통 로직으로 재사용


## 최종 보고
- 변경 파일 목록과 역할
- macOS에서 내가 직접 확인해야 할 체크리스트
- 실측 결과에 따라 바뀔 수 있는 부분(네이티브 메뉴 클릭 차단 가능 여부, Stage Manager 동작) 명시