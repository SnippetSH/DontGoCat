#pragma once

#include "PathPlanner.hpp"
#include "Surface.hpp"

#include <QObject>
#include <QPoint>
#include <QRect>

#include <functional>
#include <optional>
#include <vector>

class Locomotion;
class MouseWatcher;

// 행동 상태 머신 (README 5.9). 위쪽 상태가 아래쪽을 선점한다.
//   Hidden > Grabbed > Falling > QuitBlock > TrayApproach > MousePlay > Autonomous
//
// Hidden / Grabbed / Falling 은 "덮어씌우는" 상태라 원래 의도(QuitBlock 등)를 지운 채 끼어들지 않고,
// 끝나면 의도에 맞게 다시 시작한다 (Hidden → Autonomous, Grabbed → 놓이면 Falling, Falling → 같은 의도로 재계획).
// 경로 실행: 현재 Route 를 들고 있다가 몸이 한가해질 때마다 다음 step 을 실행한다 (applyRouteStep).
// 같은 tick 안에서 다음 step 을 이어 줘서 이동이 끝난 직후 idle 이 한 tick 깜빡이지 않는다.
class QRandomGenerator;

class CatBrain : public QObject
{
    Q_OBJECT

public:
    enum class State { Hidden, Grabbed, Falling, QuitBlock, TrayApproach, MousePlay, Autonomous };

    // 통계용 현재 활동 (README 5.14). 덮어씌우는 상태(Hidden/Grabbed/Falling)와 QuitBlock/TrayApproach 는 Other
    //   Sleeping: Autonomous 이고 sleep 루프 중 (앉는 준비 동작은 Roaming)
    //   Roaming : 그 밖의 Autonomous
    //   Playing : MousePlay (접근 포함 전체)
    enum class Activity { Other, Roaming, Sleeping, Playing };

    CatBrain(Locomotion &body, PathPlanner &planner, MouseWatcher &mouse, QObject *parent = nullptr);

    void setScale(int scale);

    // 테스트용 이음새: 행동 선택에 쓰는 난수 발생기를 바꾼다 (소유권 없음, nullptr 이면 QRandomGenerator::global()).
    void setRandomGenerator(QRandomGenerator *rng) { m_rng = rng; }
    // 테스트용 이음새: 수면 기상 판정의 난수(0..99, 확률보다 작으면 기상)만 대신 공급한다. 비어 있으면 rng() 를 쓴다.
    void setWakeRoll(std::function<int()> roll) { m_wakeRoll = std::move(roll); }
    void onSnapshot(const DesktopSnapshot &snapshot);
    void tick(qint64 nowMs);

    // 종료 팝업 전체 사각형 (QuitBlock 의 커서 추적 영역 기준). null 이면 버튼 사각형을 쓴다
    void setPopupRect(const QRect &popup);

    // 현재 유효한 상태 (Hidden / Falling 이 의도보다 우선)
    State state() const;
    Activity activity() const;   // tick 직후 CatApp 이 읽어 통계 시간을 쌓는다

    // 잡기 (README 5.9, 5.13). grab 은 Hidden 이거나 의도가 QuitBlock 이면 거절(false)
    bool grab(QPoint gripDesktop);
    void moveGrab(QPoint gripDesktop);
    void releaseGrab();

public slots:
    void onTrayApproach(bool near, QPoint trayFloorPoint);
    void onBlockRequested(QRect quitButtonDesktop);
    void onBlockReact();
    void onBlockReleased();
    void onUserMoved(QPoint pos);
    void onMouseIdle(QPoint pos);

signals:
    void wantClickThrough(bool on);     // QuitBlock 으로 버튼을 덮는 동안 false
    void visibilityChanged(bool visible);
    void blockAbandoned();              // 도달 불가 등으로 방해를 포기 → TrayController 가 방해를 해제해야 한다
    void nudged();                      // 툭툭: nudge 로 커서가 실제로 움직였다 (MouseWatcher::nudge 성공 시에만, 통계)

private:
    enum class RouteStatus { None, Running, Arrived, Failed };
    enum class BlockPhase { Approach, Guard, Aside, AsideSit };
    enum class GuardAct { None, Dash, Walk, Crouch, Sit, Swipe };
    enum class TrayPhase { Walk, Wait };
    enum class PlayPhase { Approach, Crouch, Poke, Wander, Roll };
    enum class AutoAct { None, Idle, Wander, Explore, Sit, Sleep, PawSwipe, Sprawl };

    // 현재 경로. needPlan 이면 plan() 을 (재)시도한다
    struct RouteRun
    {
        bool active = false;
        bool needPlan = false;
        bool preempt = false;       // 재계획 시 진행 중인 명령을 끊어도 되는가 (스냅샷 무효화) — 낙하 후에는 착지 애니를 기다림
        bool run = false;
        double runFrameScale = 1.0; // Move(run) 의 Run 프레임 시간 배율 (QuitBlock 접근: kBlockRunFrameMsScale)
        bool dashFinal = false;     // 목표와 같은 바닥 위 마지막 Move 는 대시 (QuitBlock 접근)
        SurfacePoint goal;
        Route steps;
        size_t idx = 0;
        int failures = 0;           // 연속 plan() 실패
        int replans = 0;
        qint64 retryAtMs = 0;
    };

    // ── 공통 ───────────────────────────────────────
    int px(int spritePx) const { return spritePx * m_scale; }
    bool spawned() const;
    const SurfaceSegment *bodySegment() const;
    const SurfaceSegment *floorSegmentAt(int x, int y) const;
    int randBetween(int lo, int hi) const;
    QRandomGenerator &rng() const;   // 주입된 발생기 또는 global()
    QRandomGenerator *m_rng = nullptr;
    void beginIntent(State s, const char *why);
    void enterAutonomous(const char *why);
    void restartIntent();
    void restoreClickThrough();

    // ── 경로 ───────────────────────────────────────
    void startRoute(const SurfacePoint &goal, bool run, bool preempt);
    void abortRoute();
    void requestReplan(bool preempt);
    const SurfaceSegment *segmentOf(const SurfacePoint &p) const;
    RouteStatus stepRoute();
    bool stepApplicable(const RouteStep &step) const;

    // ── 덮어씌우는 상태 ────────────────────────────
    bool updateHidden();
    bool updateGrabbed();
    bool updateFalling();

    // ── 상태별 ─────────────────────────────────────
    void runQuitBlock();
    void startBlockRoute();
    void guardTick(const SurfaceSegment &seg);
    void beginAside();
    void abandonBlock(const char *why);
    QRect trackRegion() const;

    void runTrayApproach();
    void startTrayRoute();

    bool tryStartMousePlay();
    std::optional<SurfacePoint> findPlayGoal() const;
    void runMousePlay();
    void startPlayAction();
    void finishPlayAction();
    void doNudge();
    void onBodyFrame(CatAnim anim, int frame);

    void runAutonomous();
    bool updateAutoAct();
    void startNextAutoAct();
    void startIdleAct();
    bool startWander();
    bool startExplore();

    Locomotion &m_body;
    PathPlanner &m_planner;
    MouseWatcher &m_mouse;
    DesktopSnapshot m_snapshot;
    int m_scale = 3;
    qint64 m_now = 0;

    State m_intent = State::Autonomous;
    bool m_hidden = false;
    bool m_falling = false;
    RouteRun m_route;
    bool m_routeAirOk = false;      // 마지막으로 실행한 step 이 Jump/WalkOff → 공중 상태가 예상된 것

    // 커서
    QPoint m_cursor;
    bool m_cursorKnown = false;
    qint64 m_cursorChangedMs = 0;
    bool m_idlePending = false;     // becameIdle 을 받았지만 아직 장난을 평가하지 않음

    // QuitBlock
    QRect m_buttonRect;
    QRect m_popupRect;
    BlockPhase m_blockPhase = BlockPhase::Approach;
    bool m_clickThroughOff = false;
    bool m_swipePending = false;
    GuardAct m_guardAct = GuardAct::None;
    int m_guardTargetX = 0;
    qint64 m_phaseEndMs = 0;

    // TrayApproach
    TrayPhase m_trayPhase = TrayPhase::Walk;
    QPoint m_trayFloorPoint;

    // MousePlay
    PlayPhase m_playPhase = PlayPhase::Approach;
    qint64 m_playStartMs = 0;
    qint64 m_playEndMs = 0;          // Crouch(휴지) / Roll 종료 시각
    bool m_pokeArmed = false;
    QPoint m_drift;                  // 누적 nudge
    std::vector<int> m_wanderLegs;   // 남은 배회 목표 (along, 데스크탑 좌표)
    int m_playStandAlong = 0;

    // Autonomous
    AutoAct m_act = AutoAct::None;
    qint64 m_actEndMs = 0;
    bool m_sleeping = false;         // Sleep: sit 이 끝나고 sleep 루프에 들어갔는가

    // 수면 (README 5.9.1)
    bool updateSleep();              // sleep 루프 중 기상 판정. true = 깼다 (act 종료)
    void noteSleepEnded();           // 수면이 어떤 이유로든 끝났다 → 재취침 금지 시작 (sleep 루프 중이었을 때만)
    bool sleepAllowed() const;       // 재취침 금지 시간이 지났는가
    qint64 m_sleepStartMs = 0;       // sleep 루프 시작 시각
    int m_wakeChecks = 0;            // 지금까지 한 기상 판정 횟수
    qint64 m_sleepCooldownUntilMs = -1;   // 이 시각 전에는 Sleep 을 고르지 않는다 (-1: 제한 없음)
    std::function<int()> m_wakeRoll;   // 테스트 주입 (비어 있으면 rng())
};
