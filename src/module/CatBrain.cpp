#include "CatBrain.hpp"

#include "Config.hpp"
#include "Locomotion.hpp"
#include "MouseWatcher.hpp"

#include <QLoggingCategory>
#include <QRandomGenerator>

#include <algorithm>
#include <cmath>
#include <cstdlib>

Q_LOGGING_CATEGORY(lcBrain, "ccat.brain")

namespace {

constexpr int kMaxStepsPerTick = 8;   // 한 tick 에 이어서 실행하는 route step 상한 (무한 루프 방지)

int signOf(int v)
{
    return (v > 0) - (v < 0);
}

const char *stateName(CatBrain::State s)
{
    switch (s) {
    case CatBrain::State::Hidden:       return "Hidden";
    case CatBrain::State::Grabbed:      return "Grabbed";
    case CatBrain::State::Falling:      return "Falling";
    case CatBrain::State::QuitBlock:    return "QuitBlock";
    case CatBrain::State::TrayApproach: return "TrayApproach";
    case CatBrain::State::MousePlay:    return "MousePlay";
    case CatBrain::State::Autonomous:   return "Autonomous";
    }
    return "?";
}

const char *kindName(RouteStep::Kind k)
{
    switch (k) {
    case RouteStep::Kind::Move:    return "Move";
    case RouteStep::Kind::Corner:  return "Corner";
    case RouteStep::Kind::Jump:    return "Jump";
    case RouteStep::Kind::WalkOff: return "WalkOff";
    }
    return "?";
}

} // namespace

CatBrain::CatBrain(Locomotion &body, PathPlanner &planner, MouseWatcher &mouse, QObject *parent)
    : QObject(parent)
    , m_body(body)
    , m_planner(planner)
    , m_mouse(mouse)
{
    // paw_swipe 타격 프레임에 커서를 툭 친다 (MousePlay)
    m_body.onFrame = [this](CatAnim anim, int frame) { onBodyFrame(anim, frame); };
}

CatBrain::State CatBrain::state() const
{
    if (m_hidden)
        return State::Hidden;
    if (m_body.mode() == Locomotion::Mode::Held)
        return State::Grabbed;
    if (m_falling)
        return State::Falling;
    return m_intent;
}

void CatBrain::setScale(int scale)
{
    if (scale == m_scale)
        return;
    m_scale = scale;
    // 거리 상수가 모두 바뀌므로 진행 중인 경로/행동을 버린다 (방해 중이면 팝업도 다시 배치되므로 포기)
    if (m_intent == State::QuitBlock)
        abandonBlock("scale changed");
    else if (spawned())
        enterAutonomous("scale changed");
}

void CatBrain::setPopupRect(const QRect &popup)
{
    m_popupRect = popup;
}

// ── 공통 보조 ────────────────────────────────────────────

bool CatBrain::spawned() const
{
    return m_body.mode() != Locomotion::Mode::Attached || m_body.attachment().has_value();
}

const SurfaceSegment *CatBrain::segmentOf(const SurfacePoint &p) const
{
    const std::optional<QRect> rect = m_snapshot.ownerRect(p.surface);
    if (!rect)
        return nullptr;
    const int origin = p.surface.kind == SurfaceKind::Floor ? rect->left() : rect->top();
    return m_snapshot.findSegment(p.surface, origin + p.offset);
}

const SurfaceSegment *CatBrain::bodySegment() const
{
    const std::optional<SurfacePoint> att = m_body.attachment();
    return att ? segmentOf(*att) : nullptr;
}

const SurfaceSegment *CatBrain::floorSegmentAt(int x, int y) const
{
    // y 와 허용 오차 이내인 바닥 중 x 를 포함하는 것. 모니터 바닥(owner 0)을 우선하고 그다음 가까운 선
    const SurfaceSegment *best = nullptr;
    for (const SurfaceSegment &seg : m_snapshot.segments) {
        if (seg.id.kind != SurfaceKind::Floor || !seg.contains(x))
            continue;
        const int dy = std::abs(seg.line - y);
        if (dy > Config::kCornerTolerancePx)
            continue;
        if (!best) {
            best = &seg;
            continue;
        }
        const bool segMonitor = seg.id.owner == 0;
        const bool bestMonitor = best->id.owner == 0;
        if (segMonitor != bestMonitor ? segMonitor : dy < std::abs(best->line - y))
            best = &seg;
    }
    return best;
}

QRandomGenerator &CatBrain::rng() const
{
    return m_rng ? *m_rng : *QRandomGenerator::global();
}

int CatBrain::randBetween(int lo, int hi) const
{
    if (hi <= lo)
        return lo;
    return rng().bounded(lo, hi + 1);
}

void CatBrain::restoreClickThrough()
{
    if (!m_clickThroughOff)
        return;
    m_clickThroughOff = false;
    emit wantClickThrough(true);
}

void CatBrain::beginIntent(State s, const char *why)
{
    if (m_intent != s)
        qCInfo(lcBrain) << "state" << stateName(m_intent) << "->" << stateName(s) << "-" << why;
    restoreClickThrough();
    abortRoute();
    m_intent = s;
    m_guardAct = GuardAct::None;
    m_swipePending = false;
    m_pokeArmed = false;
    m_wanderLegs.clear();
    m_act = AutoAct::None;
    m_sleeping = false;
}

void CatBrain::enterAutonomous(const char *why)
{
    beginIntent(State::Autonomous, why);
}

void CatBrain::restartIntent()
{
    // 낙하가 끝났다 → 같은 의도로 처음부터 다시 (경로는 착지 위치에서 새로 계획)
    switch (m_intent) {
    case State::QuitBlock:
        if (m_blockPhase == BlockPhase::Approach || m_blockPhase == BlockPhase::Guard) {
            m_blockPhase = BlockPhase::Approach;
            m_guardAct = GuardAct::None;
            startBlockRoute();
        } else {
            enterAutonomous("landed after release");
        }
        break;
    case State::TrayApproach:
        m_trayPhase = TrayPhase::Walk;
        startTrayRoute();
        break;
    case State::MousePlay:
        enterAutonomous("landed during play");
        break;
    default:
        m_act = AutoAct::None;
        break;
    }
}

// ── 경로 실행 ────────────────────────────────────────────

void CatBrain::startRoute(const SurfacePoint &goal, bool run, bool preempt)
{
    m_route = RouteRun{};
    m_route.active = true;
    m_route.needPlan = true;
    m_route.preempt = preempt;
    m_route.run = run;
    m_route.goal = goal;
    m_routeAirOk = false;
}

void CatBrain::abortRoute()
{
    m_route.active = false;
    m_route.steps.clear();
    m_routeAirOk = false;
}

void CatBrain::requestReplan(bool preempt)
{
    if (!m_route.active)
        return;
    if (m_route.needPlan) {
        m_route.preempt = m_route.preempt || preempt;
        return;
    }
    ++m_route.replans;
    m_route.needPlan = true;
    m_route.preempt = preempt;
    m_route.failures = 0;
    m_route.retryAtMs = 0;
}

bool CatBrain::stepApplicable(const RouteStep &step) const
{
    const std::optional<SurfacePoint> att = m_body.attachment();
    if (!att)
        return false;
    if (step.kind == RouteStep::Kind::WalkOff)
        return true;   // target 은 예상 착지점일 뿐
    if (!m_snapshot.resolve(step.target))
        return false;

    // 플래너가 가정한 위치(세그먼트)에 지금 있어야 한다 (낙하/가림으로 어긋났다면 재계획)
    const SurfaceSegment *here = segmentOf(*att);
    const SurfaceSegment *there = segmentOf(step.target);
    switch (step.kind) {
    case RouteStep::Kind::Move:   return here && here == there;
    case RouteStep::Kind::Corner: return here && there && here != there;
    default:                      return true;
    }
}

CatBrain::RouteStatus CatBrain::stepRoute()
{
    RouteRun &r = m_route;
    if (!r.active)
        return RouteStatus::None;
    if (m_body.mode() == Locomotion::Mode::Airborne)
        return RouteStatus::Running;

    if (r.needPlan) {
        if (r.replans > Config::kMaxReplans) {
            qCInfo(lcBrain) << "route gave up: too many replans";
            r.active = false;
            return RouteStatus::Failed;
        }
        if (m_now < r.retryAtMs)
            return RouteStatus::Running;
        if (!r.preempt && m_body.isBusy())
            return RouteStatus::Running;   // 착지 애니 등이 끝나길 기다린다
        const std::optional<SurfacePoint> att = m_body.attachment();
        if (!att)
            return RouteStatus::Running;

        std::optional<Route> plan;
        if (m_snapshot.resolve(r.goal))
            plan = m_planner.plan(m_snapshot, *att, r.goal, r.run);
        if (!plan) {
            if (++r.failures >= Config::kMaxPlanFailures) {
                qCInfo(lcBrain) << "route gave up: plan failed" << r.failures << "times";
                r.active = false;
                return RouteStatus::Failed;
            }
            r.retryAtMs = m_now + Config::kReplanRetryMs;
            qCDebug(lcBrain) << "plan failed, retry in" << Config::kReplanRetryMs << "ms";
            return RouteStatus::Running;
        }
        if (m_body.isBusy())
            m_body.stop();   // 진행 중이던 명령을 끊고 현재 위치에서 새 경로로 출발
        r.steps = std::move(*plan);
        r.idx = 0;
        r.needPlan = false;
        r.failures = 0;
        m_routeAirOk = false;
        qCDebug(lcBrain) << "plan ok:" << r.steps.size() << "steps, run" << r.run << "replans" << r.replans;
    }

    for (int i = 0; i < kMaxStepsPerTick; ++i) {
        if (m_body.isBusy())
            return RouteStatus::Running;
        m_routeAirOk = false;

        if (r.idx >= r.steps.size()) {
            // 끝났다 → 실제로 목표 위에 있는지 확인 (모서리 거부 등으로 어긋났다면 재계획)
            const std::optional<QPoint> dest = m_snapshot.resolve(r.goal);
            const bool there = dest && (m_body.anchor() - *dest).manhattanLength() <= px(Config::kArriveToleranceSpritePx);
            if (there) {
                r.active = false;
                return RouteStatus::Arrived;
            }
            qCDebug(lcBrain) << "route ended away from goal, replan";
            requestReplan(false);
            return RouteStatus::Running;
        }

        const RouteStep &step = r.steps[r.idx];
        if (!stepApplicable(step)) {
            qCDebug(lcBrain) << "route step" << r.idx << kindName(step.kind) << "not applicable, replan";
            requestReplan(false);
            return RouteStatus::Running;
        }
        qCDebug(lcBrain) << "route step" << (r.idx + 1) << "/" << r.steps.size() << kindName(step.kind)
                         << "offset" << step.target.offset << "run" << step.run;
        const SurfaceSegment *stepSeg = segmentOf(step.target);
        const bool finalLeg = r.idx + 1 == r.steps.size() && step.kind == RouteStep::Kind::Move
            && stepSeg && stepSeg->id.kind == SurfaceKind::Floor && stepSeg == segmentOf(r.goal);
        if (r.dashFinal && finalLeg)
            m_body.dashAlong(step.target.offset);   // 버튼 앞 바닥에 올라온 뒤 마지막 구간은 대시
        else
            applyRouteStep(m_body, step, r.runFrameScale);
        ++r.idx;
        m_routeAirOk = step.kind == RouteStep::Kind::Jump || step.kind == RouteStep::Kind::WalkOff;
    }
    return RouteStatus::Running;
}

void CatBrain::onSnapshot(const DesktopSnapshot &snapshot)
{
    m_snapshot = snapshot;
    if (!m_route.active || m_route.needPlan)
        return;

    // 목표나 (실행 중인 / 다음) step 이 더 이상 풀리지 않으면 재계획
    bool bad = !m_snapshot.resolve(m_route.goal);
    const size_t first = m_route.idx > 0 ? m_route.idx - 1 : 0;
    const size_t last = std::min(m_route.idx + 1, m_route.steps.size());
    for (size_t i = first; i < last; ++i) {
        const RouteStep &step = m_route.steps[i];
        if (step.kind != RouteStep::Kind::WalkOff && !m_snapshot.resolve(step.target))
            bad = true;
    }
    if (bad) {
        qCInfo(lcBrain) << "route invalidated by snapshot";
        requestReplan(true);
    }
}

// ── 덮어씌우는 상태 ──────────────────────────────────────

bool CatBrain::updateHidden()
{
    const bool fullscreen = m_snapshot.isFullscreenAt(m_body.anchor());
    if (fullscreen == m_hidden)
        return m_hidden;

    m_hidden = fullscreen;
    if (fullscreen) {
        qCInfo(lcBrain) << "state" << stateName(m_intent) << "-> Hidden (fullscreen)";
        const bool wasBlocking = m_intent == State::QuitBlock;
        enterAutonomous("hidden");
        m_body.stop();
        emit visibilityChanged(false);
        if (wasBlocking)
            emit blockAbandoned();
    } else {
        qCInfo(lcBrain) << "state Hidden -> Autonomous (fullscreen cleared)";
        enterAutonomous("fullscreen cleared");
        emit visibilityChanged(true);
    }
    return m_hidden;
}

bool CatBrain::updateGrabbed()
{
    // 잡혀 있는 동안: 경로 중단, 클릭 통과 복구 (CatApp 이 grabbing 으로 따로 끈다), 의도는 그대로.
    // 놓이면 몸이 Airborne 이 되어 updateFalling → 착지 → restartIntent 로 이어진다
    if (m_body.mode() != Locomotion::Mode::Held)
        return false;
    restoreClickThrough();
    abortRoute();
    return true;
}

bool CatBrain::grab(QPoint gripDesktop)
{
    if (!spawned() || m_hidden || m_intent == State::QuitBlock)
        return false;
    qCInfo(lcBrain) << "state" << stateName(state()) << "-> Grabbed";
    restoreClickThrough();
    abortRoute();
    m_swipePending = false;
    m_pokeArmed = false;
    m_body.hold(gripDesktop);
    return true;
}

void CatBrain::moveGrab(QPoint gripDesktop)
{
    m_body.moveHeld(gripDesktop);
}

void CatBrain::releaseGrab()
{
    if (m_body.mode() != Locomotion::Mode::Held)
        return;
    qCInfo(lcBrain) << "released, resume" << stateName(m_intent);
    m_body.release();
}

bool CatBrain::updateFalling()
{
    // 경로의 Jump / WalkOff 로 공중에 있는 것은 정상. 그 밖의 공중 상태(발판 소실 등)가 Falling
    const bool air = m_body.mode() == Locomotion::Mode::Airborne;
    const bool falling = air && !(m_route.active && m_routeAirOk);
    if (falling) {
        if (!m_falling) {
            m_falling = true;
            qCInfo(lcBrain) << "state" << stateName(m_intent) << "-> Falling";
            restoreClickThrough();
            abortRoute();
        }
        return true;
    }
    if (m_falling) {
        m_falling = false;
        qCInfo(lcBrain) << "landed, resume" << stateName(m_intent);
        restartIntent();
    }
    return false;
}

// ── tick ─────────────────────────────────────────────────

void CatBrain::tick(qint64 nowMs)
{
    m_now = nowMs;
    if (!m_cursorKnown) {
        m_cursor = m_mouse.pos();
        m_cursorKnown = true;
        m_cursorChangedMs = nowMs;
    }
    if (!spawned())
        return;
    if (updateHidden())
        return;
    if (updateGrabbed())
        return;
    if (updateFalling())
        return;
    if (!m_body.attachment())
        return;

    switch (m_intent) {
    case State::QuitBlock:    runQuitBlock(); break;
    case State::TrayApproach: runTrayApproach(); break;
    case State::MousePlay:    runMousePlay(); break;
    default:                  runAutonomous(); break;
    }
}

// ── 입력 슬롯 ────────────────────────────────────────────

void CatBrain::onUserMoved(QPoint pos)
{
    m_cursor = pos;
    m_cursorKnown = true;
    m_cursorChangedMs = m_now;
    m_idlePending = false;
    // 잡혀 있을 때는 커서가 움직이는 게 당연하므로 건드리지 않는다 (stop() 이 잡기를 풀어 버림). 놓이면 restartIntent 가 Autonomous 로
    if (m_intent == State::MousePlay && m_body.mode() != Locomotion::Mode::Held) {
        m_body.stop();   // 즉시 중단 → idle
        enterAutonomous("user moved");
    }
}

void CatBrain::onMouseIdle(QPoint pos)
{
    m_cursor = pos;
    m_cursorKnown = true;
    m_idlePending = true;
    if (m_intent == State::Autonomous && !m_hidden && !m_falling && spawned())
        tryStartMousePlay();
    // 다른 의도가 진행 중이면 Autonomous 로 돌아왔을 때 평가한다 (runAutonomous)
}

void CatBrain::onTrayApproach(bool near, QPoint trayFloorPoint)
{
    if (!near) {
        if (m_intent == State::TrayApproach)
            enterAutonomous("tray left");
        return;
    }
    if (m_hidden || m_intent == State::QuitBlock)
        return;
    m_trayFloorPoint = trayFloorPoint;
    beginIntent(State::TrayApproach, "tray near");
    m_trayPhase = TrayPhase::Walk;
    startTrayRoute();
}

void CatBrain::onBlockRequested(QRect quitButtonDesktop)
{
    m_buttonRect = quitButtonDesktop;
    if (m_hidden) {
        qCInfo(lcBrain) << "block requested while hidden, abandon";
        emit blockAbandoned();
        return;
    }
    beginIntent(State::QuitBlock, "block requested");
    m_blockPhase = BlockPhase::Approach;
    startBlockRoute();
}

void CatBrain::onBlockReact()
{
    if (m_intent == State::QuitBlock && m_blockPhase <= BlockPhase::Guard)
        m_swipePending = true;
}

void CatBrain::onBlockReleased()
{
    restoreClickThrough();
    if (m_intent != State::QuitBlock || m_blockPhase >= BlockPhase::Aside)
        return;
    qCInfo(lcBrain) << "block released";
    abortRoute();
    m_swipePending = false;
    m_guardAct = GuardAct::None;
    beginAside();
}

// ── QuitBlock ────────────────────────────────────────────

void CatBrain::startBlockRoute()
{
    const SurfaceSegment *seg = floorSegmentAt(m_buttonRect.center().x(), m_buttonRect.bottom() + 1);
    if (!seg) {
        abandonBlock("no floor in front of the button");
        return;
    }
    const int x = std::clamp(m_buttonRect.center().x(), seg->a, seg->b - 1);
    startRoute(Locomotion::pointOn(*seg, x), true, true);
    m_route.runFrameScale = Config::kBlockRunFrameMsScale;   // 방해하러 달려올 때만 빠르게
    m_route.dashFinal = true;
}

void CatBrain::abandonBlock(const char *why)
{
    qCInfo(lcBrain) << "block abandoned:" << why;
    enterAutonomous("block abandoned");   // 클릭 통과 복구 포함
    emit blockAbandoned();
}

QRect CatBrain::trackRegion() const
{
    const QRect base = m_popupRect.isNull() ? m_buttonRect : m_buttonRect.united(m_popupRect);
    return base.adjusted(-Config::kGuardTrackMarginPx, -Config::kGuardTrackMarginPx,
                         Config::kGuardTrackMarginPx, Config::kGuardTrackMarginPx);
}

void CatBrain::beginAside()
{
    const SurfaceSegment *seg = bodySegment();
    if (!seg || seg != floorSegmentAt(m_buttonRect.center().x(), m_buttonRect.bottom() + 1)) {
        enterAutonomous("released away from the button floor");
        return;
    }
    // 버튼에서 몸 길이 하나쯤 떨어진 곳. 지금 있는 쪽을 우선하고, 자리가 없으면 반대쪽
    const int len = px(Config::kAsideOffsetSpritePx);
    const int left = m_buttonRect.left() - len;
    const int right = m_buttonRect.right() + 1 + len;
    const auto fits = [seg](int x) { return x >= seg->a && x < seg->b; };
    const bool preferLeft = m_body.anchor().x() < m_buttonRect.center().x();
    int x = preferLeft ? left : right;
    if (!fits(x))
        x = preferLeft ? right : left;
    if (!fits(x)) {
        enterAutonomous("no room beside the button");
        return;
    }
    qCInfo(lcBrain) << "step aside to x" << x;
    m_blockPhase = BlockPhase::Aside;
    m_body.moveAlong(x - Locomotion::originOf(*seg), false);
}

void CatBrain::runQuitBlock()
{
    // 해제 후 비켜 앉는 단계는 팝업/바닥 상태와 무관하게 진행
    if (m_blockPhase == BlockPhase::Aside) {
        if (!m_body.isBusy()) {
            m_body.faceToward(m_cursor);
            m_body.play(CatAnim::Sit);
            m_blockPhase = BlockPhase::AsideSit;
            m_phaseEndMs = m_now + Config::kAsideSitMs;
        }
        return;
    }
    if (m_blockPhase == BlockPhase::AsideSit) {
        if (m_now >= m_phaseEndMs)
            enterAutonomous("aside done");
        return;
    }

    const SurfaceSegment *seg = floorSegmentAt(m_buttonRect.center().x(), m_buttonRect.bottom() + 1);
    if (!seg) {
        abandonBlock("button floor gone");
        return;
    }

    // 버튼이 놓인 바닥 세그먼트 위에 올라오면 클릭 통과를 끈다 (고양이 몸이 클릭을 받는다)
    const bool onFloor = bodySegment() == seg;
    if (onFloor != m_clickThroughOff) {
        m_clickThroughOff = onFloor;
        emit wantClickThrough(!onFloor);
    }

    if (m_blockPhase == BlockPhase::Approach) {
        const RouteStatus st = stepRoute();
        if (st == RouteStatus::Running)
            return;
        if (st != RouteStatus::Arrived) {
            abandonBlock("button unreachable");
            return;
        }
        qCInfo(lcBrain) << "block: arrived, guarding";
        m_blockPhase = BlockPhase::Guard;
        m_guardAct = GuardAct::None;
    }

    if (!onFloor) {
        m_blockPhase = BlockPhase::Approach;
        startBlockRoute();
        return;
    }
    guardTick(*seg);
}

void CatBrain::guardTick(const SurfaceSegment &seg)
{
    // 반응 paw_swipe: 끝날 때까지 추적을 멈춘다
    if (m_guardAct == GuardAct::Swipe) {
        if (m_body.isBusy())
            return;
        m_body.stop();
        m_guardAct = GuardAct::None;
    }
    if (m_swipePending) {
        m_swipePending = false;
        m_body.faceToward(m_cursor);
        m_body.play(CatAnim::PawSwipe);
        m_guardAct = GuardAct::Swipe;
        return;
    }

    const int origin = Locomotion::originOf(seg);
    const int cur = m_body.anchor().x();
    const int half = px(Config::kBodyHalfWidthSpritePx);
    const int deadband = std::max(1, px(Config::kGuardTrackDeadbandSpritePx));
    const bool busy = m_body.isBusy();

    if (trackRegion().contains(m_cursor)) {
        // 커서 x 바로 밑으로 (버튼 좌우 끝 ± 몸 절반 안에서) 대시
        int target = std::clamp(m_cursor.x(), m_buttonRect.left() - half, m_buttonRect.right() + 1 + half);
        target = std::clamp(target, seg.a, seg.b - 1);
        if (std::abs(target - cur) > deadband) {
            if (m_guardAct != GuardAct::Dash || target != m_guardTargetX || !busy) {
                m_body.dashAlong(target - origin);
                m_guardAct = GuardAct::Dash;
                m_guardTargetX = target;
            }
        } else if (!busy) {
            const bool settled = m_now - m_cursorChangedMs >= Config::kGuardSettleMs;
            if (settled && m_guardAct != GuardAct::Crouch) {
                m_body.faceToward(m_cursor);
                m_body.play(CatAnim::Crouch);   // 몸으로 버튼을 막고 실룩
                m_guardAct = GuardAct::Crouch;
            } else if (m_guardAct == GuardAct::Dash || m_guardAct == GuardAct::Walk) {
                m_guardAct = GuardAct::None;
            }
        }
        return;
    }

    // 추적 영역 밖: 버튼 중앙 앞으로 돌아가 커서 쪽을 보고 앉는다
    const int target = std::clamp(m_buttonRect.center().x(), seg.a, seg.b - 1);
    if (std::abs(target - cur) > deadband) {
        if (m_guardAct != GuardAct::Walk || target != m_guardTargetX || !busy) {
            m_body.moveAlong(target - origin, false);
            m_guardAct = GuardAct::Walk;
            m_guardTargetX = target;
        }
    } else if (!busy) {
        m_body.faceToward(m_cursor);
        if (m_guardAct != GuardAct::Sit) {
            m_body.play(CatAnim::Sit);
            m_guardAct = GuardAct::Sit;
        }
    }
}

// ── TrayApproach ─────────────────────────────────────────

void CatBrain::startTrayRoute()
{
    const SurfaceSegment *seg = floorSegmentAt(m_trayFloorPoint.x(), m_trayFloorPoint.y());
    if (!seg) {
        enterAutonomous("no floor at the tray");
        return;
    }
    const int x = std::clamp(m_trayFloorPoint.x(), seg->a, seg->b - 1);
    startRoute(Locomotion::pointOn(*seg, x), false, true);   // 천천히 걷는다 (run 없음)
}

void CatBrain::runTrayApproach()
{
    if (m_trayPhase == TrayPhase::Walk) {
        const RouteStatus st = stepRoute();
        if (st == RouteStatus::Running)
            return;
        if (st != RouteStatus::Arrived) {
            enterAutonomous("tray unreachable");   // 포기하고 자율 행동
            return;
        }
        qCInfo(lcBrain) << "tray approach: arrived, sit";
        m_body.faceToward(m_cursor);
        m_body.play(CatAnim::Sit);
        m_trayPhase = TrayPhase::Wait;
        return;
    }
    if (!m_body.isBusy())
        m_body.faceToward(m_cursor);   // 앉은 채 커서를 따라 바라본다
}

// ── MousePlay ────────────────────────────────────────────

std::optional<SurfacePoint> CatBrain::findPlayGoal() const
{
    const std::optional<SurfacePoint> att = m_body.attachment();
    if (!att)
        return std::nullopt;

    // 도달 가능 판정 (README 5.9): 바닥 세그먼트 바로 위 catHeight 이내 / 벽 세그먼트의 고양이 쪽 catHeight 이내
    const int height = CatSprite::Height * m_scale;
    struct Cand
    {
        const SurfaceSegment *seg;
        int dist;
    };
    std::vector<Cand> cands;
    for (const SurfaceSegment &seg : m_snapshot.segments) {
        int d;
        if (seg.id.kind == SurfaceKind::Floor) {
            if (!seg.contains(m_cursor.x()))
                continue;
            d = seg.line - m_cursor.y();
        } else {
            if (m_cursor.y() < seg.a || m_cursor.y() >= seg.b)
                continue;
            d = seg.id.kind == SurfaceKind::WallRightSide ? m_cursor.x() - seg.line : seg.line - m_cursor.x();
        }
        if (d < 0 || d > height)
            continue;
        cands.push_back({&seg, d});
    }
    std::sort(cands.begin(), cands.end(), [](const Cand &a, const Cand &b) { return a.dist < b.dist; });

    const QPoint me = m_body.anchor();
    const int offset = px(Config::kPlayStandOffsetSpritePx);
    for (size_t i = 0; i < cands.size() && i < 3; ++i) {
        const SurfaceSegment &seg = *cands[i].seg;
        const bool floor = seg.id.kind == SurfaceKind::Floor;
        const int cursorAlong = floor ? m_cursor.x() : m_cursor.y();
        const int meAlong = floor ? me.x() : me.y();
        // 앞발이 커서 근처에 오도록, 지금 있는 쪽에서 커서를 바라보며 선다
        int stand = cursorAlong + (meAlong <= cursorAlong ? -offset : offset);
        stand = std::clamp(stand, seg.a, seg.b - 1);
        const SurfacePoint goal = Locomotion::pointOn(seg, stand);
        if (m_planner.plan(m_snapshot, *att, goal, false))
            return goal;
    }
    return std::nullopt;
}

bool CatBrain::tryStartMousePlay()
{
    if (!m_body.attachment())
        return false;   // 아직 평가하지 않은 것으로 둔다 (m_idlePending 유지)
    m_idlePending = false;
    if (m_mouse.anyButtonDown())
        return false;

    const std::optional<SurfacePoint> goal = findPlayGoal();
    if (!goal) {
        qCInfo(lcBrain) << "mouse idle at" << m_cursor << "but unreachable, no play";
        return false;
    }
    beginIntent(State::MousePlay, "cursor idle and reachable");
    m_playPhase = PlayPhase::Approach;
    m_playStartMs = m_now;
    m_drift = QPoint();
    if (const std::optional<QPoint> p = m_snapshot.resolve(*goal)) {
        const SurfaceSegment *seg = segmentOf(*goal);
        m_playStandAlong = (seg && seg->id.kind != SurfaceKind::Floor) ? p->y() : p->x();
    }
    startRoute(*goal, false, true);   // 접근은 walk
    return true;
}

void CatBrain::runMousePlay()
{
    if (m_now - m_playStartMs > Config::kPlayMaxMs) {
        enterAutonomous("play timeout");
        return;
    }

    switch (m_playPhase) {
    case PlayPhase::Approach: {
        const RouteStatus st = stepRoute();
        if (st == RouteStatus::Running)
            return;
        if (st != RouteStatus::Arrived) {
            enterAutonomous("play target unreachable");
            return;
        }
        qCInfo(lcBrain) << "play: arrived, crouch";
        m_body.faceToward(m_cursor);
        m_body.play(CatAnim::Crouch);
        m_playPhase = PlayPhase::Crouch;
        m_playEndMs = m_now + Config::kPlayCrouchMs;
        return;
    }
    case PlayPhase::Crouch:
        if (m_now >= m_playEndMs)
            startPlayAction();
        return;
    case PlayPhase::Poke:
        if (!m_body.isBusy()) {
            m_pokeArmed = false;
            finishPlayAction();
        }
        return;
    case PlayPhase::Wander: {
        if (m_body.isBusy())
            return;
        const SurfaceSegment *seg = bodySegment();
        if (!seg || m_wanderLegs.empty()) {
            finishPlayAction();
            return;
        }
        const int target = m_wanderLegs.front();
        m_wanderLegs.erase(m_wanderLegs.begin());
        m_body.moveAlong(target - Locomotion::originOf(*seg), false);
        return;
    }
    case PlayPhase::Roll:
        if (m_now >= m_playEndMs)
            finishPlayAction();
        return;
    }
}

void CatBrain::finishPlayAction()
{
    m_body.faceToward(m_cursor);
    m_body.play(CatAnim::Crouch);
    m_playPhase = PlayPhase::Crouch;
    m_playEndMs = m_now + randBetween(Config::kPlayPauseMinMs, Config::kPlayPauseMaxMs);
}

void CatBrain::startPlayAction()
{
    const SurfaceSegment *seg = bodySegment();
    if (!seg) {
        enterAutonomous("play: no surface");
        return;
    }
    const bool floor = seg->id.kind == SurfaceKind::Floor;
    // TODO(decision): 벽 위에서는 뒹굴기를 하지 않는다 (회전된 roll 이 어색함)
    const int wPoke = Config::kPlayWeightPoke;
    const int wWander = Config::kPlayWeightWander;
    const int wRoll = floor ? Config::kPlayWeightRoll : 0;
    const int pick = randBetween(0, wPoke + wWander + wRoll - 1);

    if (pick < wPoke) {
        qCInfo(lcBrain) << "play: poke";
        m_body.faceToward(m_cursor);
        m_body.play(CatAnim::PawSwipe);
        m_pokeArmed = true;   // 타격 프레임(onBodyFrame)에서 nudge
        m_playPhase = PlayPhase::Poke;
    } else if (pick < wPoke + wWander) {
        // 커서 둘레 ±kPlayWanderSpritePx 를 왕복 → 마지막은 처음 섰던 자리로
        const int w = px(Config::kPlayWanderSpritePx);
        const int cursorAlong = floor ? m_cursor.x() : m_cursor.y();
        const int meAlong = floor ? m_body.anchor().x() : m_body.anchor().y();
        int side = meAlong <= cursorAlong ? 1 : -1;   // 지금 반대편부터
        m_wanderLegs.clear();
        const int legs = randBetween(Config::kPlayWanderLegsMin, Config::kPlayWanderLegsMax);
        for (int i = 0; i < legs; ++i) {
            const int along = cursorAlong + side * randBetween(w / 2, w);
            m_wanderLegs.push_back(std::clamp(along, seg->a, seg->b - 1));
            side = -side;
        }
        m_wanderLegs.push_back(std::clamp(m_playStandAlong, seg->a, seg->b - 1));
        qCInfo(lcBrain) << "play: wander" << legs << "legs";
        m_playPhase = PlayPhase::Wander;
    } else {
        qCInfo(lcBrain) << "play: roll";
        m_body.play(CatAnim::Roll);
        m_playPhase = PlayPhase::Roll;
        m_playEndMs = m_now + randBetween(Config::kRollMinMs, Config::kRollMaxMs);
    }
}

void CatBrain::onBodyFrame(CatAnim anim, int frame)
{
    if (m_intent != State::MousePlay || m_playPhase != PlayPhase::Poke || !m_pokeArmed)
        return;
    if (anim == CatAnim::PawSwipe && frame == Config::kPawHitFrame) {
        m_pokeArmed = false;
        doNudge();
    }
}

void CatBrain::doNudge()
{
    // 무작위 축 / 부호로 kNudgeMinPx..kNudgeMaxPx. 누적이 커지면 반대로 밀어 커서가 멀리 흘러가지 않게 한다
    const bool horizontal = randBetween(0, 1) == 0;
    const int mag = randBetween(Config::kNudgeMinPx, Config::kNudgeMaxPx);
    int sign = randBetween(0, 1) == 0 ? -1 : 1;
    const int drift = horizontal ? m_drift.x() : m_drift.y();
    if (std::abs(drift + sign * mag) > Config::kNudgeDriftMaxPx)
        sign = -signOf(drift);
    const int dx = horizontal ? sign * mag : 0;
    const int dy = horizontal ? 0 : sign * mag;
    if (m_mouse.nudge(dx, dy)) {   // 버튼이 눌려 있으면 하지 않는다
        m_cursor += QPoint(dx, dy);
        m_drift += QPoint(dx, dy);
        qCDebug(lcBrain) << "nudge" << dx << dy;
    }
}

// ── Autonomous ───────────────────────────────────────────

void CatBrain::runAutonomous()
{
    if (m_act != AutoAct::None) {
        if (!updateAutoAct())
            return;
        m_act = AutoAct::None;
    } else if (m_body.isBusy()) {
        return;   // 착지 애니메이션 등이 끝나길 기다린다
    }

    // 다른 의도에 밀려 평가하지 못한 커서 장난을 여기서 평가
    if (m_idlePending && tryStartMousePlay())
        return;
    startNextAutoAct();
}

bool CatBrain::updateAutoAct()
{
    switch (m_act) {
    case AutoAct::None:
        return true;
    case AutoAct::Idle:
    case AutoAct::Sit:
    case AutoAct::Sprawl:
        return m_now >= m_actEndMs;
    case AutoAct::Sleep:
        if (!m_sleeping) {
            if (!m_body.isBusy()) {   // sit 이 끝났다 → sleep 루프
                m_body.play(CatAnim::Sleep);
                m_sleeping = true;
                m_actEndMs = m_now + randBetween(Config::kSleepMinMs, Config::kSleepMaxMs);
            }
            return false;
        }
        return m_now >= m_actEndMs;
    case AutoAct::PawSwipe:
    case AutoAct::Wander:
        return !m_body.isBusy();
    case AutoAct::Explore: {
        const RouteStatus st = stepRoute();
        if (st == RouteStatus::Running)
            return false;
        if (st == RouteStatus::Arrived)
            return true;
        startIdleAct();   // 계획 실패/포기: 잠시 쉰다 (매 tick 재계획하지 않는다)
        return false;
    }
    }
    return true;
}

void CatBrain::startIdleAct()
{
    m_body.stop();
    m_act = AutoAct::Idle;
    m_actEndMs = m_now + randBetween(Config::kIdleMinMs, Config::kIdleMaxMs);
}

bool CatBrain::startWander()
{
    const SurfaceSegment *seg = bodySegment();
    if (!seg)
        return false;
    const bool floor = seg->id.kind == SurfaceKind::Floor;
    const int cur = floor ? m_body.anchor().x() : m_body.anchor().y();
    const int minD = px(Config::kWanderMinSpritePx);
    const int maxD = px(Config::kWanderMaxSpritePx);

    int dir = randBetween(0, 1) == 0 ? -1 : 1;
    int target = cur;
    for (int attempt = 0; attempt < 2; ++attempt) {
        target = std::clamp(cur + dir * randBetween(minD, maxD), seg->a, seg->b - 1);
        if (std::abs(target - cur) >= minD / 2)
            break;
        dir = -dir;   // 벽에 가까워 너무 짧다 → 반대쪽
    }
    if (std::abs(target - cur) < minD / 2)
        return false;

    qCInfo(lcBrain) << "act wander" << (target - cur);
    m_body.moveAlong(target - Locomotion::originOf(*seg), false);   // 벽이면 climb
    m_act = AutoAct::Wander;
    return true;
}

bool CatBrain::startExplore()
{
    const std::optional<SurfacePoint> att = m_body.attachment();
    if (!att)
        return false;
    const std::optional<SurfacePoint> goal =
        m_planner.randomReachable(m_snapshot, *att, rng());
    if (!goal)
        return false;
    const std::optional<QPoint> dest = m_snapshot.resolve(*goal);
    if (!dest)
        return false;

    const bool run = (*dest - m_body.anchor()).manhattanLength() > px(Config::kExploreRunMinSpritePx);
    qCInfo(lcBrain) << "act explore to" << *dest << (run ? "(run)" : "(walk)");
    startRoute(*goal, run, false);
    m_act = AutoAct::Explore;
    return true;
}

void CatBrain::startNextAutoAct()
{
    const SurfaceSegment *seg = bodySegment();
    const bool onWall = seg && seg->id.kind != SurfaceKind::Floor;

    struct Option
    {
        AutoAct act;
        int weight;
    };
    std::vector<Option> opts;
    if (onWall) {
        // 벽 위에서는 이동만 (앉기/자기 금지)
        opts = {{AutoAct::Wander, Config::kWeightWander}, {AutoAct::Explore, Config::kWeightExplore}};
    } else {
        opts = {{AutoAct::Idle, Config::kWeightIdle},         {AutoAct::Wander, Config::kWeightWander},
                {AutoAct::Explore, Config::kWeightExplore},   {AutoAct::Sit, Config::kWeightSit},
                {AutoAct::Sleep, Config::kWeightSleep},       {AutoAct::PawSwipe, Config::kWeightPawSwipe},
                {AutoAct::Sprawl, Config::kWeightSprawl}};
    }
    int total = 0;
    for (const Option &o : opts)
        total += o.weight;
    int pick = randBetween(0, total - 1);
    AutoAct chosen = opts.front().act;
    for (const Option &o : opts) {
        if (pick < o.weight) {
            chosen = o.act;
            break;
        }
        pick -= o.weight;
    }

    switch (chosen) {
    case AutoAct::Idle:
        qCInfo(lcBrain) << "act idle";
        startIdleAct();
        break;
    case AutoAct::Wander:
        if (!startWander() && !startExplore())
            startIdleAct();
        break;
    case AutoAct::Explore:
        if (!startExplore() && !startWander())
            startIdleAct();
        break;
    case AutoAct::Sit:
        qCInfo(lcBrain) << "act sit";
        m_body.play(CatAnim::Sit);
        m_act = AutoAct::Sit;
        m_actEndMs = m_now + randBetween(Config::kSitMinMs, Config::kSitMaxMs);
        break;
    case AutoAct::Sleep:
        qCInfo(lcBrain) << "act sleep";
        m_body.play(CatAnim::Sit);   // 앉은 뒤 잔다
        m_act = AutoAct::Sleep;
        m_sleeping = false;
        break;
    case AutoAct::PawSwipe:
        qCInfo(lcBrain) << "act paw_swipe";
        m_body.play(CatAnim::PawSwipe);
        m_act = AutoAct::PawSwipe;
        break;
    case AutoAct::Sprawl:
        qCInfo(lcBrain) << "act sprawl";
        m_body.play(CatAnim::Sprawl);
        m_act = AutoAct::Sprawl;
        m_actEndMs = m_now + randBetween(Config::kSprawlMinMs, Config::kSprawlMaxMs);
        break;
    case AutoAct::None:
        break;
    }
}
