#pragma once

// 튜닝 상수 모음. 모듈 코드에 매직 넘버를 직접 쓰지 않고 여기서 가져다 쓴다.
// "SpritePx" 로 끝나는 거리는 스프라이트 px 단위이므로 사용 시 × scale 한다.
// 그 외 "Px" 는 데스크탑 물리 px.

namespace Config {

// ── 표시 ────────────────────────────────────────────
inline constexpr int kDefaultScale = 3;
inline constexpr int kMinScale = 1;
inline constexpr int kMaxScale = 4;
inline constexpr const char *kSettingsOrg = "DontGoCat";
inline constexpr const char *kSettingsApp = "DontGoCat";
inline constexpr const char *kSettingsScaleKey = "scale";

// ── 타이머 ──────────────────────────────────────────
inline constexpr int kTickMs = 16;            // 메인 tick (Locomotion, CatBrain)
inline constexpr int kScanIntervalMs = 250;   // DesktopScanner
inline constexpr int kFollowMs = 4;           // 붙어 있는 창 위치 추종 (≈250Hz, 144~240Hz 모니터 대응)
inline constexpr int kMousePollMs = 33;       // MouseWatcher (≈30Hz)

// ── 데스크탑 스캔 ───────────────────────────────────
inline constexpr int kMinWindowW = 120;
inline constexpr int kMinWindowH = 80;
inline constexpr int kMinSegmentSpritePx = 24;   // 이보다 짧은 면 세그먼트는 버림
inline constexpr int kCornerTolerancePx = 8;     // 벽 아래끝 ↔ 바닥 연결 허용 오차

// ── 이동 ────────────────────────────────────────────
inline constexpr int kJumpMaxUpSpritePx = 60;    // 위로 점프 가능한 최대 높이
inline constexpr int kJumpMaxDxSpritePx = 90;    // 점프 가능한 최대 수평 거리
inline constexpr double kGravityPxPerS2 = 2400.0; // scale 3 기준, 사용 시 × scale / 3
inline constexpr int kPlannerSampleSpritePx = 32; // 경로 탐색 샘플 간격
inline constexpr int kNearestFloorPreferMaxSpritePx = 60; // nearestReachable: 점 바로 아래 바닥을 우선하는 최대 수직 거리

// 모서리 전환 기하 (Locomotion::cornerLink, PathPlanner 가 공유). "SpritePx" 는 × scale
inline constexpr int kCornerStandOffsetSpritePx = 12; // 벽 아래끝 ↔ 바닥: 바닥에 선 몸 중심이 벽에서 떨어지는 거리 (서 있는 몸 반폭)
inline constexpr int kCornerWallClearSpritePx = 16;   // 벽 아래끝 ↔ 바닥: 벽 위 몸 중심이 바닥선 위로 떠 있는 거리 (벽타기 몸 반길이)
inline constexpr int kMantleInsetSpritePx = 2;        // 넘어가기: 지붕 앵커가 모서리(창 가장자리)에서 지붕 안쪽으로 들어간 거리
inline constexpr double kCornerSitSec = 0.12;         // 플래너가 추정하는 벽↔바닥 회전 전환 시간
inline constexpr int kCornerSnapMaxSpritePx = 8;      // corner() 시작 시 접점과 현재 위치가 이보다 멀면 전환을 거부

// 점프 포물선: 꼭짓점은 더 높은 끝점보다 max(최소값, 수평거리 × 비율) 만큼 위
inline constexpr int kJumpApexExtraMinSpritePx = 8;
inline constexpr double kJumpApexExtraFrac = 0.25;
inline constexpr double kJumpPenaltySec = 0.5;        // 플래너: 점프 1회 고정 비용 (이륙 준비 + 착지)
inline constexpr double kFallPenaltySec = 0.3;        // 플래너: 낙하 착지 고정 비용

// Climb / Mantle 은 팔·발이 기본 프레임 마지막 줄(Height-1)까지 내려와 지면선 아래로 1 스프라이트 px 파고든다.
// 그릴 때 이만큼 면에서 띄워(Climb: 벽에서 멀어지는 쪽, Mantle: 위쪽) 팔이 면에 정확히 닿게 한다.
inline constexpr int kStretchLiftSpritePx = 1;

// 대시 (종료 방해 커서 추적, README 5.9): run 을 빠르게 재생하며 디딤발과 무관하게 미끄러져 이동
inline constexpr int kDashSpeedSpritePxPerS = 300;    // × scale px/s
inline constexpr double kDashFrameMsScale = 0.4;      // run 프레임 시간 배율 (작을수록 빠름)
inline constexpr double kBlockRunFrameMsScale = 0.35;  // 방해하러 달려올 때 Run 프레임 시간 배율 (작을수록 빠름). 디딤발 보폭은 그대로라 속도 ∝ 1/배율
inline constexpr int kDashFacingDeadbandSpritePx = 2; // 목표까지 이보다 가까우면 바라보는 방향을 바꾸지 않음 (떨림 방지)

// 종료 방해 (CatBrain QuitBlock, QuitPopup)
inline constexpr int kQuitButtonW = 64;               // 종료 버튼 고정 크기 (물리 px, 모든 배율 공통 = 32×8 스프라이트 px 의 2x)
inline constexpr int kQuitButtonH = 16;
inline constexpr int kGuardTrackMarginPx = 48;        // 팝업 사각형 확장폭 = 커서 추적 영역
inline constexpr int kGuardSettleMs = 400;            // 커서가 이 시간 멈추면 crouch
inline constexpr int kBodyHalfWidthSpritePx = 12;     // 서 있는 몸 반폭 (추적 목표 x 를 버튼 좌우 끝 ± 이만큼으로 제한)
inline constexpr int kGuardTrackDeadbandSpritePx = 1; // 추적 목표와 이보다 가까우면 새로 대시하지 않음 (crouch 유지)
inline constexpr int kAsideOffsetSpritePx = 24;       // 방해 해제 후 버튼에서 비켜 앉는 거리 (몸 길이 하나)
inline constexpr int kAsideSitMs = 2500;              // 비켜 앉은 뒤 자율 행동으로 돌아가기까지

// ── 경로 실행 (CatBrain) ────────────────────────────
inline constexpr int kMaxTickDtMs = 100;              // CatApp tick: 절전 복귀 등으로 dt 가 이보다 크면 잘라낸다
inline constexpr int kReplanRetryMs = 500;            // plan() 실패 시 재시도 간격
inline constexpr int kMaxPlanFailures = 3;            // 연속 plan() 실패 허용 횟수 (넘으면 목표 포기)
inline constexpr int kMaxReplans = 6;                 // 한 경로 안에서 다시 계획할 수 있는 횟수
inline constexpr int kArriveToleranceSpritePx = 1;    // 경로 종료 후 목표와의 허용 오차 (넘으면 재계획)
inline constexpr int kTrayApproachOffsetSpritePx = 20; // 트레이 접근: 트레이 아이콘 중심에서 왼쪽으로 떨어진 거리

// ── 트레이 컨트롤러 ─────────────────────────────────
inline constexpr int kReopenGuardMs = 300;            // 팝업이 비활성화로 먼저 닫힌 직후 온 트레이 클릭은 "닫기"로 본다
inline constexpr int kTrayRectCacheMs = 500;          // QSystemTrayIcon::geometry() 는 셸 호출이라 캐시
inline constexpr int kFallbackTrayPx = 32;            // 트레이 위치를 전혀 모를 때 주 모니터 우하단에 두는 가상 아이콘 크기

// ── 종료 방해 (R4) ──────────────────────────────────
inline constexpr int kQuitBlockBaseChance = 80;   // %
inline constexpr int kQuitBlockStep = 10;         // %p, 재시도마다 감소
inline constexpr int kQuitRetryWindowMs = 10000;  // 직전 시도 후 이 시간 안이면 재시도

// ── 트레이 접근 (R4) ────────────────────────────────
inline constexpr int kTrayApproachRadiusPx = 300;
inline constexpr int kTrayLeaveDelayMs = 3000;

// ── 마우스 장난 (R6) ────────────────────────────────
inline constexpr int kMouseIdleMs = 5000;
inline constexpr int kNudgeMinPx = 1;
inline constexpr int kNudgeMaxPx = 2;
inline constexpr int kPlayWanderSpritePx = 24;
inline constexpr int kRollMinMs = 2000;
inline constexpr int kRollMaxMs = 4000;
inline constexpr int kPlayStandOffsetSpritePx = 10;   // 앞발이 커서 근처에 오도록 앵커를 커서에서 떼는 거리
inline constexpr int kPawHitFrame = 2;                // paw_swipe 에서 앞발이 가장 앞으로 나가는 프레임 (CatAnim.cpp pawSwipe)
inline constexpr int kPlayCrouchMs = 1000;            // 도착 후 처음 crouch 시간
inline constexpr int kPlayPauseMinMs = 400;           // 장난 사이 crouch 휴지
inline constexpr int kPlayPauseMaxMs = 1200;
inline constexpr int kPlayMaxMs = 60000;              // 장난 한 판 최대 시간 (커서가 계속 멈춰 있어도 이후 자율 행동으로)
inline constexpr int kPlayWeightPoke = 50;            // 장난 종류 가중치
inline constexpr int kPlayWeightWander = 30;
inline constexpr int kPlayWeightRoll = 20;            // 벽 위에서는 제외
inline constexpr int kPlayWanderLegsMin = 2;          // 배회 한 번에 왕복하는 구간 수
inline constexpr int kPlayWanderLegsMax = 3;
inline constexpr int kNudgeDriftMaxPx = 4;            // 누적 이동이 이보다 커지면 반대 방향으로 밀어 커서가 멀리 흘러가지 않게 함

// ── 자율 행동 (가중치, 시간) ────────────────────────
inline constexpr int kWeightIdle = 30;
inline constexpr int kWeightWander = 25;
inline constexpr int kWeightExplore = 20;
inline constexpr int kWeightSit = 12;
inline constexpr int kWeightSleep = 8;
inline constexpr int kWeightPawSwipe = 5;
inline constexpr int kWeightSprawl = 8;
inline constexpr int kIdleMinMs = 2000;
inline constexpr int kIdleMaxMs = 6000;
inline constexpr int kSitMinMs = 4000;
inline constexpr int kSitMaxMs = 12000;
inline constexpr int kSleepMinMs = 20000;
inline constexpr int kSleepMaxMs = 60000;
inline constexpr int kSprawlMinMs = 6000;
inline constexpr int kSprawlMaxMs = 15000;
inline constexpr int kWanderMinSpritePx = 16;         // 현재 면 위 배회 거리 범위
inline constexpr int kWanderMaxSpritePx = 80;
inline constexpr int kExploreRunMinSpritePx = 150;    // 탐험 목표가 이보다 멀면 달리기를 우선

} // namespace Config
