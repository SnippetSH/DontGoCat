#pragma once

#include "CatAnim.hpp"
#include "Surface.hpp"

#include <QPointF>

#include <functional>
#include <optional>

// 오버레이에 넘길 한 프레임
struct CatFrame
{
    CatPose pose;
    CatAnim anim = CatAnim::Idle;
    int animFrame = 0;
    CatGravity gravity = CatGravity::Down;
    CatFacing facing = CatFacing::Left;
    QPoint anchor;          // 데스크탑 물리 px
};

// 포물선 점프 계획: from → to 를 flightSec 동안 날고 v0 는 이륙 속도 (px/s, y 아래가 +)
struct JumpArc
{
    double flightSec = 0.0;
    QPointF v0;
};

// 벽 세그먼트와 바닥 세그먼트가 모서리 전환으로 이어지는 접점 쌍 (README 5.7).
// 기하 (edge 규약은 DesktopScanner 주석 참고, s = scale):
//  WallBottom: 벽 아래끝이 바닥선에서 kCornerTolerancePx 이내.
//    - 바닥 접점: 벽 선에서 바깥쪽으로 kCornerStandOffsetSpritePx*s 떨어진 바닥 위 (서 있는 몸이 벽에 닿되 파고들지 않음)
//    - 벽 접점  : 바닥선 위로 kCornerWallClearSpritePx*s 올라간 벽 위 (벽타기 몸이 바닥 밑으로 안 내려감)
//  WallTop: 같은 창의 지붕. 벽 위끝이 지붕선에서 허용 오차 이내이고 지붕 구간이 모서리까지 이어짐.
//    - 지붕 접점: 모서리에서 지붕 안쪽으로 kMantleInsetSpritePx*s (넘어가기 프레임의 뒷발이 모서리에 걸림)
//    - 벽 접점  : 벽 위끝 (a)
struct CornerLink
{
    enum class Kind { WallBottom, WallTop };
    Kind kind = Kind::WallBottom;
    SurfacePoint wall;     // 벽 쪽 접점
    SurfacePoint floor;    // 바닥(지붕) 쪽 접점
};

// 고양이 몸의 운동 상태 (README 5.7).
// - Attached: 면 위 (SurfacePoint). 위치는 항상 "소유 사각형 + offset" 으로 계산 → 창 이동 추종
// - Airborne: 점프/낙하 중 (연속 좌표, 중력)
// - 걷기/오르기는 애니메이션 프레임이 넘어갈 때만 speedPxPerFrame × scale 만큼 이동 (디딤발 고정)
// 명령은 한 번에 하나. 스스로 끝나는 명령이 진행 중이면 isBusy() == true (Airborne 은 항상 busy).
// 반복(loop) 애니메이션의 play() 는 stop() 까지 계속되지만 busy 가 아니다 (idle 과 같음).
// 공중 상태에서는 점프·낙하 중 어떤 이동 명령도 무시된다 (stop() 은 점프를 자유낙하로 바꿈).
class Locomotion
{
public:
    enum class Mode { Attached, Airborne };

    Locomotion();

    void setScale(int scale);

    // 시작 위치: desktopPoint 에서 떨어져 아래 첫 바닥 위로 착지 (land)
    void spawn(const DesktopSnapshot &snapshot, QPoint desktopPoint);

    // 새 스냅샷 반영: 창 이동 추종, 발판 소실 / 세그먼트 이탈 시 낙하 시작
    void onSnapshot(const DesktopSnapshot &snapshot);

    // 스캔 사이에 붙어 있는 창의 실시간 사각형 반영: 크기가 같으면 그 창의 세그먼트를 평행 이동한다.
    // 스캔 주기(Config::kScanIntervalMs)보다 촘촘하게 창 이동을 따라가기 위함. 크기 변경/가림은 다음 스캔이 처리
    void followOwner(const QRect &liveOwnerRect);

    void tick(qint64 dtMs);

    // ── 명령 ───────────────────────────────────────
    // 현재 면 위 이동 (바닥 walk/run, 벽 climb). runFrameScale: 바닥 run 일 때만 적용되는 프레임 시간 배율
    // (작을수록 빠름). 프레임마다 정확히 speedPxPerFrame × scale 만 움직이므로 디딤발 고정 규칙은 유지된다.
    void moveAlong(int targetOffset, bool run, double runFrameScale = 1.0);
    // 바닥 전용: run 을 빠르게 재생하며 디딤발과 무관하게 연속으로 미끄러져 이동 (kDashSpeedSpritePxPerS).
    // 이동 중 다시 부르면 애니메이션 재시작 없이 목표만 바꾼다. 도착하면 완료(idle). 벽에서는 moveAlong(…, true).
    void dashAlong(int targetOffset);
    void corner(const SurfacePoint &target);           // 벽↔바닥 직접 전환, 벽 꼭대기 ↔ 지붕 mantle
    void jumpTo(const SurfacePoint &target);           // 포물선 점프 (jump_up → fall → land)
    void walkOffEdge(bool towardPositive);             // 세그먼트 끝까지 걸어 나가 낙하
    void play(CatAnim anim);                           // 제자리 애니메이션 (loop 면 stop() 까지, one-shot 은 끝 프레임 유지)
    void face(CatFacing facing);
    void faceToward(QPoint desktopPoint);              // 바닥: x 비교, 벽: y 비교
    void stop();                                       // 현재 명령 취소 → idle

    // ── 상태 ───────────────────────────────────────
    Mode mode() const { return m_mode; }
    bool isBusy() const;
    std::optional<SurfacePoint> attachment() const;
    QPoint anchor() const;
    CatFrame frame() const;

    // 애니메이션 프레임이 바뀔 때마다 호출 (예: paw_swipe 타격 프레임에 커서 nudge)
    std::function<void(CatAnim anim, int frame)> onFrame;

    // ── PathPlanner 와 공유하는 기하/물리 ─────────────
    // 점프 포물선 (중력 kGravityPxPerS2 × scale / 3). 꼭짓점이 더 높은 끝점보다 조금 위가 되도록 비행 시간을 정한다
    static JumpArc planJump(QPointF from, QPointF to, int scale);
    // 바닥 → 면 아래로 떨어지는 데 걸리는 시간 (초, 초속도 0)
    static double fallSeconds(double heightPx, int scale);
    // 벽(wall) 세그먼트와 바닥(floor) 세그먼트가 모서리 전환으로 이어지는지. 안 이어지면 nullopt
    static std::optional<CornerLink> cornerLink(const SurfaceSegment &wall, const SurfaceSegment &floor, int scale);
    // 디딤발 고정 이동의 평균 속도 (px/s). anim = Walk/Run/Climb
    static double pxPerSecond(CatAnim anim, int scale);
    // 세그먼트 위 along(데스크탑 좌표) ↔ SurfacePoint
    static SurfacePoint pointOn(const SurfaceSegment &seg, int along);
    static int originOf(const SurfaceSegment &seg);   // offset 기준 원점 (Floor: ownerRect.left, Wall: top)

private:
    enum class Cmd { None, Move, Dash, Corner, Jump, Fall, WalkOff, Play };

    // ── 내부 ───────────────────────────────────────
    void startAnim(CatAnim anim, int frame = 0, double frameScale = 1.0, bool reverse = false);
    void ensureAnim(CatAnim anim, double frameScale);
    void finishToIdle();
    void advanceFrames(qint64 dtMs);
    void onFrameAdvanced();
    void onAnimEnded();

    std::optional<int> alongOf(const SurfacePoint &p) const;
    const SurfaceSegment *currentSegment() const;
    int currentAlong() const;
    void setAlong(const SurfaceSegment &seg, int along);
    void attachTo(const SurfacePoint &p, CatFacing facing);
    CatGravity currentGravity() const;
    CatFacing facingForDirection(SurfaceKind kind, int dir) const;
    bool wantsMoveStep() const;

    void stepMove();
    void tickDash(double dtSec);
    void finishCornerSit();
    void finishCornerMantle();
    void tickAir(double dtSec);
    void beginFall(QPointF pos, QPointF vel);
    void landOn(const SurfaceSegment &seg, int along);
    void snapToMonitorFloor(QPointF pos);
    void updateAirAnim();
    void retargetJump();
    double gravity() const;

    Mode m_mode = Mode::Attached;
    int m_scale = 3;
    DesktopSnapshot m_snapshot;

    // 부착
    SurfacePoint m_point;
    CatFacing m_facing = CatFacing::Left;
    QPoint m_lastAnchor;
    bool m_hasPosition = false;

    // 공중
    QPointF m_pos;
    QPointF m_vel;
    QPointF m_jumpFrom;
    QPointF m_jumpV0;
    SurfacePoint m_jumpTarget;
    double m_jumpT = 0.0;
    double m_jumpTotal = 0.0;

    // 애니메이션 시계
    CatAnim m_anim = CatAnim::Idle;
    int m_frame = 0;
    double m_animMs = 0.0;
    double m_frameScale = 1.0;
    bool m_reverse = false;
    bool m_frozen = false;        // 끝 프레임 유지 (one-shot 종료 / 벽 위 대기)

    // 명령
    Cmd m_cmd = Cmd::None;
    int m_targetOffset = 0;
    bool m_run = false;
    bool m_walkOffPositive = false;
    bool m_playThenIdle = false;
    double m_dashPos = 0.0;       // Dash: 소유 사각형 기준 연속 offset
    CornerLink m_corner;
    bool m_cornerToFloor = false; // 벽 → 바닥 방향이면 true
    int m_cornerPhase = 0;
};
