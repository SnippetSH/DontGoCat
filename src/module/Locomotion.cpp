#include "Locomotion.hpp"

#include "Config.hpp"

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr double kMsPerSec = 1000.0;
constexpr int kMaxFrameAdvancesPerTick = 64;   // tick 이 크게 밀려도 무한 루프가 되지 않게
constexpr double kMinFlightSec = 1e-3;
constexpr double kDashArriveEpsPx = 0.5;       // 대시 도착 판정 (이 안이면 목표 위)

int signOf(int v)
{
    return (v > 0) - (v < 0);
}

double gravityPxPerS2(int scale)
{
    return Config::kGravityPxPerS2 * scale / 3.0;
}

// 벽 선에서 바깥쪽(고양이가 있는 쪽)을 보는 방향
CatFacing outwardFacing(SurfaceKind wall)
{
    return wall == SurfaceKind::WallRightSide ? CatFacing::Right : CatFacing::Left;
}

CatFacing flipped(CatFacing f)
{
    return f == CatFacing::Left ? CatFacing::Right : CatFacing::Left;
}

} // namespace

// ── 정적 기하/물리 ───────────────────────────────────────

double Locomotion::pxPerSecond(CatAnim anim, int scale)
{
    const CatAnimInfo &info = animInfo(anim);
    if (info.totalMs() <= 0)
        return 0.0;
    return double(info.cycleDistancePx) * scale / (info.totalMs() / kMsPerSec);
}

double Locomotion::fallSeconds(double heightPx, int scale)
{
    return std::sqrt(2.0 * std::max(0.0, heightPx) / gravityPxPerS2(scale));
}

JumpArc Locomotion::planJump(QPointF from, QPointF to, int scale)
{
    // 꼭짓점: 더 높은 끝점(y 가 작은 쪽)보다 extra 만큼 위. 올라가는 시간 + 내려오는 시간 = 비행 시간
    //   y(t) = from.y + v0y t + g t²/2,  v0y = -g tUp  →  y(T) = to.y (위 식을 풀어 확인)
    const double g = gravityPxPerS2(scale);
    const double extra = std::max(double(Config::kJumpApexExtraMinSpritePx) * scale,
                                  Config::kJumpApexExtraFrac * std::abs(to.x() - from.x()));
    const double apexY = std::min(from.y(), to.y()) - extra;
    const double tUp = std::sqrt(2.0 * (from.y() - apexY) / g);
    const double tDown = std::sqrt(2.0 * (to.y() - apexY) / g);

    JumpArc arc;
    arc.flightSec = tUp + tDown;
    arc.v0 = QPointF((to.x() - from.x()) / arc.flightSec, -g * tUp);
    return arc;
}

int Locomotion::originOf(const SurfaceSegment &seg)
{
    return seg.id.kind == SurfaceKind::Floor ? seg.ownerRect.left() : seg.ownerRect.top();
}

SurfacePoint Locomotion::pointOn(const SurfaceSegment &seg, int along)
{
    SurfacePoint p;
    p.surface = seg.id;
    p.offset = along - originOf(seg);
    return p;
}

std::optional<CornerLink> Locomotion::cornerLink(const SurfaceSegment &wall, const SurfaceSegment &floor, int scale)
{
    if (wall.id.kind == SurfaceKind::Floor || floor.id.kind != SurfaceKind::Floor)
        return std::nullopt;

    const bool right = wall.id.kind == SurfaceKind::WallRightSide;
    const int line = wall.line;

    CornerLink link;
    if (wall.id.owner != 0 && wall.id.owner == floor.id.owner) {
        // 벽 위끝 ↔ 같은 창 지붕 (mantle). 지붕 구간이 모서리(= 벽 선)까지 이어져 있어야 한다.
        // 오른쪽 측면: 창 안쪽이 왼쪽 → 지붕은 line 보다 왼쪽. 왼쪽 측면은 반대.
        const bool touchesCorner = right ? floor.b >= line : floor.a <= line;
        if (!touchesCorner || std::abs(wall.a - floor.line) > Config::kCornerTolerancePx)
            return std::nullopt;

        const int inset = Config::kMantleInsetSpritePx * scale;
        const int x = std::clamp(right ? line - inset : line + inset, floor.a, floor.b - 1);
        link.kind = CornerLink::Kind::WallTop;
        link.wall = pointOn(wall, wall.a);
        link.floor = pointOn(floor, x);
        return link;
    }

    // 벽 아래끝 ↔ 바닥 (다른 창 지붕 또는 모니터 바닥)
    if (std::abs(floor.line - wall.b) > Config::kCornerTolerancePx)
        return std::nullopt;

    const int stand = Config::kCornerStandOffsetSpritePx * scale;
    const int x = right ? line + stand : line - stand;
    if (!floor.contains(x))
        return std::nullopt;

    const int y = std::clamp(floor.line - Config::kCornerWallClearSpritePx * scale, wall.a, wall.b - 1);
    link.kind = CornerLink::Kind::WallBottom;
    link.wall = pointOn(wall, y);
    link.floor = pointOn(floor, x);
    return link;
}

// ── 생성 / 설정 ──────────────────────────────────────────

Locomotion::Locomotion() = default;

void Locomotion::setScale(int scale)
{
    m_scale = scale;   // Held 의 앵커는 anchor() 가 그립 + 오프셋 × 배율로 매번 계산하므로 그립 점은 그대로 유지된다
}

double Locomotion::gravity() const
{
    return gravityPxPerS2(m_scale);
}

// ── 시작 / 스냅샷 ────────────────────────────────────────

void Locomotion::spawn(const DesktopSnapshot &snapshot, QPoint desktopPoint)
{
    m_snapshot = snapshot;
    m_hasPosition = true;

    // 이미 바닥 선 위라면 떨어질 필요 없이 그 자리에 선다
    const SurfaceSegment *floor = m_snapshot.floorBelow(desktopPoint);
    if (floor && floor->line == desktopPoint.y()) {
        m_mode = Mode::Attached;
        attachTo(pointOn(*floor, desktopPoint.x()), m_facing);
        finishToIdle();
        return;
    }
    beginFall(QPointF(desktopPoint), QPointF());
}

void Locomotion::onSnapshot(const DesktopSnapshot &snapshot)
{
    m_snapshot = snapshot;
    if (!m_hasPosition)
        return;

    if (m_mode == Mode::Held)
        return;   // 잡혀 있는 동안은 발판과 무관 (놓으면 그 자리에서 낙하)

    if (m_mode == Mode::Airborne) {
        if (m_cmd == Cmd::Jump)
            retargetJump();
        return;
    }

    if (const std::optional<QPoint> p = m_snapshot.resolve(m_point)) {
        m_lastAnchor = *p;   // 창이 움직였다면 고양이도 같이 (offset 기준)
        return;
    }

    // 발판이 사라졌다 (창 닫힘/최소화/가려짐/세그먼트 이탈) → 마지막 앵커에서 떨어진다.
    // 같은 높이에 다른 바닥이 이어져 있으면 그 위에 그대로 선다.
    const SurfaceSegment *floor = m_snapshot.floorBelow(m_lastAnchor);
    if (floor && floor->line == m_lastAnchor.y()) {
        attachTo(pointOn(*floor, m_lastAnchor.x()), m_facing);
        finishToIdle();
        return;
    }
    beginFall(QPointF(m_lastAnchor), QPointF());
}

void Locomotion::followOwner(const QRect &liveOwnerRect)
{
    if (!m_hasPosition || m_mode != Mode::Attached || m_point.surface.owner == 0)
        return;
    const std::optional<QRect> known = m_snapshot.ownerRect(m_point.surface);
    if (!known || known->size() != liveOwnerRect.size())
        return;
    const QPoint d = liveOwnerRect.topLeft() - known->topLeft();
    if (d.isNull())
        return;

    const WindowHandle owner = m_point.surface.owner;
    for (SurfaceSegment &seg : m_snapshot.segments) {
        if (seg.id.owner != owner)
            continue;
        const bool floor = seg.id.kind == SurfaceKind::Floor;
        const int dLine = floor ? d.y() : d.x();
        const int dAlong = floor ? d.x() : d.y();
        seg.line += dLine;
        seg.a += dAlong;
        seg.b += dAlong;
        seg.ownerRect.translate(d);
    }
    if (const std::optional<QPoint> p = m_snapshot.resolve(m_point))
        m_lastAnchor = *p;
}

// ── 애니메이션 시계 ──────────────────────────────────────

void Locomotion::startAnim(CatAnim anim, int frame, double frameScale, bool reverse)
{
    const bool changed = anim != m_anim || frame != m_frame;
    m_anim = anim;
    m_frame = frame;
    m_animMs = 0.0;
    m_frameScale = frameScale;
    m_reverse = reverse;
    m_frozen = false;
    if (changed && onFrame)
        onFrame(m_anim, m_frame);
}

void Locomotion::ensureAnim(CatAnim anim, double frameScale)
{
    if (m_anim == anim && !m_frozen && !m_reverse) {
        m_frameScale = frameScale;   // 이어서 재생 (재시작 없음)
        return;
    }
    startAnim(anim, 0, frameScale);
}

void Locomotion::finishToIdle()
{
    m_cmd = Cmd::None;
    m_playThenIdle = false;

    const std::optional<SurfacePoint> att = attachment();
    if (att && att->surface.kind != SurfaceKind::Floor) {
        // 벽 위의 기본 자세 = Climb 첫 프레임 정지
        if (!(m_anim == CatAnim::Climb && m_frozen && m_frame == 0 && !m_reverse)) {
            startAnim(CatAnim::Climb);
            m_frozen = true;
        }
        return;
    }
    ensureAnim(CatAnim::Idle, 1.0);
}

void Locomotion::advanceFrames(qint64 dtMs)
{
    if (m_frozen) {
        m_animMs = 0.0;
        return;
    }
    m_animMs += double(dtMs);

    for (int guard = 0; guard < kMaxFrameAdvancesPerTick; ++guard) {
        if (m_frozen) {
            m_animMs = 0.0;
            break;
        }
        const CatAnimInfo &info = animInfo(m_anim);
        const double dur = std::max(1.0, info.frameMs[m_frame] * m_frameScale);
        if (m_animMs < dur)
            break;
        m_animMs -= dur;

        // 벽↔바닥 회전 전환: 첫 Sit 프레임 시간이 지나면 면을 바꾼다
        if (m_cmd == Cmd::Corner && m_cornerPhase == 0) {
            finishCornerSit();
            continue;
        }

        const int n = info.frameCount();
        int next = m_reverse ? m_frame - 1 : m_frame + 1;
        if (next < 0 || next >= n) {
            if (!info.loop) {
                onAnimEnded();
                continue;
            }
            next = m_reverse ? n - 1 : 0;
        }
        m_frame = next;
        if (onFrame)
            onFrame(m_anim, m_frame);
        onFrameAdvanced();
    }
}

bool Locomotion::wantsMoveStep() const
{
    return m_mode == Mode::Attached && (m_cmd == Cmd::Move || m_cmd == Cmd::WalkOff)
        && (m_anim == CatAnim::Walk || m_anim == CatAnim::Run || m_anim == CatAnim::Climb);
}

void Locomotion::onFrameAdvanced()
{
    if (wantsMoveStep())
        stepMove();
}

void Locomotion::onAnimEnded()
{
    m_frozen = true;   // one-shot 은 끝 프레임 유지
    switch (m_cmd) {
    case Cmd::Play:
        if (m_playThenIdle)
            finishToIdle();
        else
            m_cmd = Cmd::None;
        break;
    case Cmd::Corner:
        if (m_cornerPhase == 1)
            finishCornerMantle();
        break;
    default:
        break;   // JumpUp 등: 끝 프레임에서 대기 (updateAirAnim 이 Fall 로 바꿈)
    }
}

// ── 부착 위치 보조 ───────────────────────────────────────

std::optional<int> Locomotion::alongOf(const SurfacePoint &p) const
{
    const std::optional<QRect> rect = m_snapshot.ownerRect(p.surface);
    if (!rect)
        return std::nullopt;
    return (p.surface.kind == SurfaceKind::Floor ? rect->left() : rect->top()) + p.offset;
}

const SurfaceSegment *Locomotion::currentSegment() const
{
    if (m_mode != Mode::Attached)
        return nullptr;
    const std::optional<int> along = alongOf(m_point);
    return along ? m_snapshot.findSegment(m_point.surface, *along) : nullptr;
}

int Locomotion::currentAlong() const
{
    return alongOf(m_point).value_or(0);
}

void Locomotion::setAlong(const SurfaceSegment &seg, int along)
{
    m_point.offset = along - originOf(seg);
    m_lastAnchor = seg.pointAt(along);
}

void Locomotion::attachTo(const SurfacePoint &p, CatFacing facing)
{
    m_mode = Mode::Attached;
    m_point = p;
    m_facing = facing;
    if (const std::optional<QPoint> a = m_snapshot.resolve(p))
        m_lastAnchor = *a;
}

CatGravity Locomotion::currentGravity() const
{
    return m_mode == Mode::Attached ? gravityFor(m_point.surface.kind) : CatGravity::Down;
}

CatFacing Locomotion::facingForDirection(SurfaceKind kind, int dir) const
{
    // README 3.3: 바닥은 −x ⇒ Left. 오른쪽 측면(gravity Left): 위 ⇒ Left, 아래 ⇒ Right.
    // 왼쪽 측면(gravity Right): 위 ⇒ Right, 아래 ⇒ Left. dir 은 along 증가 방향이 +
    // (벽의 along = y 이므로 위로 가면 −)
    if (dir == 0)
        return m_facing;
    switch (kind) {
    case SurfaceKind::WallRightSide: return dir < 0 ? CatFacing::Left : CatFacing::Right;
    case SurfaceKind::WallLeftSide:  return dir < 0 ? CatFacing::Right : CatFacing::Left;
    default:                         return dir < 0 ? CatFacing::Left : CatFacing::Right;
    }
}

// ── 이동 명령 ────────────────────────────────────────────

void Locomotion::moveAlong(int targetOffset, bool run, double runFrameScale)
{
    if (m_mode != Mode::Attached || !m_hasPosition)
        return;
    const SurfaceSegment *seg = currentSegment();
    if (!seg)
        return;

    m_cmd = Cmd::Move;
    m_playThenIdle = false;
    m_targetOffset = targetOffset;
    m_run = run;

    // 목표는 현재 세그먼트 안으로 clamp → 의도치 않게 면 밖으로 걸어 나가지 않는다
    const int target = std::clamp(originOf(*seg) + targetOffset, seg->a, seg->b - 1);
    const int rem = target - currentAlong();
    if (rem == 0) {
        finishToIdle();
        return;
    }

    m_facing = facingForDirection(seg->id.kind, signOf(rem));
    const CatAnim anim = seg->id.kind == SurfaceKind::Floor ? (run ? CatAnim::Run : CatAnim::Walk) : CatAnim::Climb;
    ensureAnim(anim, anim == CatAnim::Run ? runFrameScale : 1.0);
}

void Locomotion::stepMove()
{
    const SurfaceSegment *seg = currentSegment();
    if (!seg)
        return;   // 발판 소실은 onSnapshot 이 처리

    int target;
    if (m_cmd == Cmd::WalkOff)
        target = m_walkOffPositive ? seg->b - 1 : seg->a;
    else
        target = std::clamp(originOf(*seg) + m_targetOffset, seg->a, seg->b - 1);

    const int cur = currentAlong();
    const int rem = target - cur;
    if (rem != 0) {
        // 프레임이 넘어갈 때마다 정확히 speedPxPerFrame × scale. 남은 거리가 더 작으면 남은 만큼만
        const int step = animInfo(m_anim).speedPxPerFrame * m_scale;
        const int mv = std::min(step, std::abs(rem));
        setAlong(*seg, cur + signOf(rem) * mv);
        m_facing = facingForDirection(seg->id.kind, signOf(rem));
    }

    if (currentAlong() != target)
        return;

    if (m_cmd == Cmd::WalkOff) {
        // 끝에 닿았다 → 몸 중심이 가장자리를 넘으므로 걷던 속도로 밀려 나가며 낙하
        const double vx = seg->id.kind == SurfaceKind::Floor ? (m_walkOffPositive ? 1.0 : -1.0)
                                                              * pxPerSecond(CatAnim::Walk, m_scale) : 0.0;
        beginFall(QPointF(m_lastAnchor), QPointF(vx, 0.0));
    } else {
        finishToIdle();
    }
}

void Locomotion::dashAlong(int targetOffset)
{
    if (m_mode != Mode::Attached || !m_hasPosition)
        return;
    const SurfaceSegment *seg = currentSegment();
    if (!seg)
        return;
    if (seg->id.kind != SurfaceKind::Floor) {
        moveAlong(targetOffset, true);   // 벽은 climb
        return;
    }

    const bool dashing = m_cmd == Cmd::Dash;
    const double target = std::clamp(originOf(*seg) + targetOffset, seg->a, seg->b - 1) - originOf(*seg);
    if (!dashing) {
        m_dashPos = m_point.offset;
        // 이미 목표 위면 아무것도 안 한다 (crouch 같은 진행 중 동작을 끊지 않음)
        if (std::abs(target - m_dashPos) < kDashArriveEpsPx)
            return;
    }

    m_cmd = Cmd::Dash;
    m_playThenIdle = false;
    m_targetOffset = targetOffset;

    const double rem = target - m_dashPos;
    if (std::abs(rem) > Config::kDashFacingDeadbandSpritePx * m_scale || !dashing)
        m_facing = facingForDirection(SurfaceKind::Floor, rem < 0 ? -1 : 1);
    ensureAnim(CatAnim::Run, Config::kDashFrameMsScale);   // 이미 대시 중이면 재시작 없음
}

void Locomotion::tickDash(double dtSec)
{
    const SurfaceSegment *seg = currentSegment();
    if (!seg)
        return;

    const int origin = originOf(*seg);
    const double target = std::clamp(origin + m_targetOffset, seg->a, seg->b - 1) - origin;
    const double rem = target - m_dashPos;
    const double step = double(Config::kDashSpeedSpritePxPerS) * m_scale * dtSec;

    if (std::abs(rem) <= std::max(step, kDashArriveEpsPx)) {
        m_dashPos = target;
        setAlong(*seg, origin + int(std::lround(m_dashPos)));
        finishToIdle();
        return;
    }
    m_dashPos += (rem < 0 ? -step : step);
    setAlong(*seg, std::clamp(origin + int(std::lround(m_dashPos)), seg->a, seg->b - 1));
    if (std::abs(rem) > Config::kDashFacingDeadbandSpritePx * m_scale)
        m_facing = facingForDirection(SurfaceKind::Floor, rem < 0 ? -1 : 1);
}

void Locomotion::walkOffEdge(bool towardPositive)
{
    if (m_mode != Mode::Attached || !m_hasPosition)
        return;
    const SurfaceSegment *seg = currentSegment();
    if (!seg)
        return;

    m_cmd = Cmd::WalkOff;
    m_playThenIdle = false;
    m_walkOffPositive = towardPositive;
    m_facing = facingForDirection(seg->id.kind, towardPositive ? 1 : -1);

    ensureAnim(seg->id.kind == SurfaceKind::Floor ? CatAnim::Walk : CatAnim::Climb, 1.0);
    const int target = towardPositive ? seg->b - 1 : seg->a;
    if (currentAlong() == target)
        stepMove();   // 이미 끝 → 곧바로 낙하
}

// ── 모서리 전환 ──────────────────────────────────────────

void Locomotion::corner(const SurfacePoint &target)
{
    if (m_mode != Mode::Attached || !m_hasPosition)
        return;
    const SurfaceSegment *cur = currentSegment();
    const std::optional<int> targetAlong = alongOf(target);
    const SurfaceSegment *dst = targetAlong ? m_snapshot.findSegment(target.surface, *targetAlong) : nullptr;
    if (!cur || !dst) {
        qWarning() << "Locomotion::corner: 현재/목표 세그먼트가 없음";
        return;
    }

    const bool fromWall = cur->id.kind != SurfaceKind::Floor;
    const SurfaceSegment &wall = fromWall ? *cur : *dst;
    const SurfaceSegment &floor = fromWall ? *dst : *cur;
    const std::optional<CornerLink> link = cornerLink(wall, floor, m_scale);
    if (!link) {
        qWarning() << "Locomotion::corner: 두 면이 모서리로 이어지지 않음";
        return;
    }
    // 접점에서 너무 멀면 거부 (플래너가 접점까지 이동시킨 뒤 호출하는 것이 약속. 작은 오차만 접점으로 맞춘다)
    const std::optional<QPoint> startPt = m_snapshot.resolve(fromWall ? link->wall : link->floor);
    if (!startPt || (anchor() - *startPt).manhattanLength() > Config::kCornerSnapMaxSpritePx * m_scale) {
        qWarning() << "Locomotion::corner: 접점에서 너무 멀리 있음";
        return;
    }

    m_cmd = Cmd::Corner;
    m_playThenIdle = false;
    m_corner = *link;
    m_cornerToFloor = fromWall;

    // 접점으로 맞춘다 (플래너가 이미 이 점까지 이동시켰으므로 보통 차이 없음)
    attachTo(fromWall ? link->wall : link->floor, m_facing);
    const SurfaceKind wallKind = wall.id.kind;

    if (link->kind == CornerLink::Kind::WallTop) {
        // 넘어가기: Mantle 은 바닥(지붕) 프레임 기준이므로 지붕 접점에 붙은 채 재생한다.
        // 오른쪽 측면이면 고양이는 창 안쪽(왼쪽)을 보고 앞발은 지붕에, 뒷발은 모서리에 걸린다.
        attachTo(link->floor, flipped(outwardFacing(wallKind)));
        m_cornerPhase = 1;
        const int last = animInfo(CatAnim::Mantle).frameCount() - 1;
        if (fromWall)
            startAnim(CatAnim::Mantle, 0);
        else
            startAnim(CatAnim::Mantle, last, 1.0, true);   // 지붕 모서리 → 벽: 프레임 역재생
    } else {
        // 벽 아래끝 ↔ 바닥: 제자리에서 앉는 프레임을 잠깐 보인 뒤 면을 바꾼다
        m_cornerPhase = 0;
        startAnim(CatAnim::Sit);
    }
}

void Locomotion::finishCornerSit()
{
    const SurfaceKind wallKind = m_corner.wall.surface.kind;
    if (m_cornerToFloor)
        attachTo(m_corner.floor, outwardFacing(wallKind));                 // 벽에서 멀어지는 방향
    else
        attachTo(m_corner.wall, facingForDirection(wallKind, -1));         // 위로 오르는 방향
    finishToIdle();
}

void Locomotion::finishCornerMantle()
{
    if (!m_cornerToFloor) {
        // 역재생 끝: 벽 위끝에 아래를 보고 붙는다
        attachTo(m_corner.wall, facingForDirection(m_corner.wall.surface.kind, 1));
    }
    finishToIdle();
}

// ── 점프 / 낙하 ──────────────────────────────────────────

void Locomotion::jumpTo(const SurfacePoint &target)
{
    if (m_mode != Mode::Attached || !m_hasPosition)
        return;
    const std::optional<QPoint> to = m_snapshot.resolve(target);
    if (!to) {
        qWarning() << "Locomotion::jumpTo: 목표가 현재 면 위에 없음";
        return;
    }

    const QPointF from(anchor());
    const JumpArc arc = planJump(from, QPointF(*to), m_scale);

    m_mode = Mode::Airborne;   // 이륙: 벽에서 떠나도 회전 없음 (gravity Down)
    m_cmd = Cmd::Jump;
    m_playThenIdle = false;
    m_jumpTarget = target;
    m_jumpFrom = from;
    m_jumpV0 = arc.v0;
    m_jumpTotal = arc.flightSec;
    m_jumpT = 0.0;
    m_pos = from;
    m_vel = arc.v0;
    if (to->x() != m_lastAnchor.x())
        m_facing = to->x() < m_lastAnchor.x() ? CatFacing::Left : CatFacing::Right;
    startAnim(CatAnim::JumpUp);
}

void Locomotion::retargetJump()
{
    // 착지할 면이 움직이거나 사라졌을 때: 남은 시간 동안 새 목표에 맞도록 이륙 속도를 다시 잡는다
    const std::optional<QPoint> to = m_snapshot.resolve(m_jumpTarget);
    const double tau = m_jumpTotal - m_jumpT;
    if (!to || tau < kMinFlightSec) {
        if (!to) {
            m_cmd = Cmd::Fall;
            startAnim(CatAnim::Fall);
        }
        return;
    }
    const double g = gravity();
    m_jumpFrom = m_pos;
    m_jumpV0 = QPointF((to->x() - m_pos.x()) / tau, (to->y() - m_pos.y() - 0.5 * g * tau * tau) / tau);
    m_jumpTotal = tau;
    m_jumpT = 0.0;
}

void Locomotion::beginFall(QPointF pos, QPointF vel)
{
    m_mode = Mode::Airborne;
    m_cmd = Cmd::Fall;
    m_playThenIdle = false;
    m_pos = pos;
    m_vel = vel;
    startAnim(CatAnim::Fall);
}

void Locomotion::updateAirAnim()
{
    if (m_cmd != Cmd::Jump)
        return;
    const CatAnim want = m_vel.y() < 0.0 ? CatAnim::JumpUp : CatAnim::Fall;
    if (m_anim != want)
        startAnim(want);
}

void Locomotion::landOn(const SurfaceSegment &seg, int along)
{
    along = std::clamp(along, seg.a, seg.b - 1);
    attachTo(pointOn(seg, along), m_facing);
    m_lastAnchor = seg.pointAt(along);
    m_cmd = Cmd::Play;          // Land 한 번 재생 후 idle
    m_playThenIdle = true;
    startAnim(CatAnim::Land);
}

void Locomotion::snapToMonitorFloor(QPointF pos)
{
    const SurfaceSegment *best = nullptr;
    double bestDist = std::numeric_limits<double>::max();
    int bestAlong = 0;
    for (const SurfaceSegment &seg : m_snapshot.segments) {
        if (seg.id.owner != 0 || seg.id.kind != SurfaceKind::Floor)
            continue;
        const int x = std::clamp(int(std::lround(pos.x())), seg.a, seg.b - 1);
        const double dx = x - pos.x();
        const double dy = seg.line - pos.y();
        const double d = dx * dx + dy * dy;
        if (d < bestDist) {
            bestDist = d;
            best = &seg;
            bestAlong = x;
        }
    }
    if (!best) {
        qWarning() << "Locomotion: 모니터 바닥이 없어 낙하를 멈춘다";
        m_vel = QPointF();
        return;
    }
    qWarning() << "Locomotion: 아래에 바닥이 없어 가장 가까운 모니터 바닥으로 이동" << pos;
    landOn(*best, bestAlong);
}

void Locomotion::tickAir(double dtSec)
{
    const double g = gravity();

    if (m_cmd == Cmd::Jump) {
        m_jumpT += dtSec;
        if (m_jumpT < m_jumpTotal) {
            // 해석적 포물선 → 비행 시간이 끝나면 목표에 정확히 도착
            m_pos = m_jumpFrom + m_jumpV0 * m_jumpT + QPointF(0.0, 0.5 * g * m_jumpT * m_jumpT);
            m_vel = QPointF(m_jumpV0.x(), m_jumpV0.y() + g * m_jumpT);
            updateAirAnim();
            return;
        }

        const std::optional<int> along = alongOf(m_jumpTarget);
        const SurfaceSegment *seg = along ? m_snapshot.findSegment(m_jumpTarget.surface, *along) : nullptr;
        if (!seg) {
            beginFall(m_pos, QPointF());
            return;
        }
        if (seg->id.kind == SurfaceKind::Floor) {
            landOn(*seg, *along);
        } else {
            // 벽: 붙어서 Climb 첫 프레임 (위를 향함)
            attachTo(pointOn(*seg, *along), facingForDirection(seg->id.kind, -1));
            finishToIdle();
        }
        return;
    }

    // 자유 낙하: 프레임 사이 선분 교차로 바닥을 판정 (빠른 낙하도 관통하지 않음)
    const QPointF prev = m_pos;
    m_pos += m_vel * dtSec + QPointF(0.0, 0.5 * g * dtSec * dtSec);
    m_vel.setY(m_vel.y() + g * dtSec);

    const SurfaceSegment *hit = nullptr;
    int hitX = 0;
    for (const SurfaceSegment &seg : m_snapshot.segments) {
        if (seg.id.kind != SurfaceKind::Floor)
            continue;
        // 엄밀히 위에서 아래로 가로지른 바닥만 (출발한 면 자체는 제외)
        if (!(prev.y() < seg.line && m_pos.y() >= seg.line))
            continue;
        const double u = (seg.line - prev.y()) / (m_pos.y() - prev.y());
        const int x = int(std::lround(prev.x() + (m_pos.x() - prev.x()) * u));
        if (!seg.contains(x))
            continue;
        if (!hit || seg.line < hit->line) {
            hit = &seg;
            hitX = x;
        }
    }
    if (hit) {
        landOn(*hit, hitX);
        return;
    }

    // 아래에 바닥이 전혀 없으면(모니터 밖 등) 가장 가까운 모니터 바닥으로
    const QPoint rounded(int(std::lround(m_pos.x())), int(std::lround(m_pos.y())));
    if (!m_snapshot.floorBelow(rounded))
        snapToMonitorFloor(m_pos);
}

// ── 일반 명령 ────────────────────────────────────────────

void Locomotion::play(CatAnim anim)
{
    if (m_mode != Mode::Attached || !m_hasPosition)
        return;
    m_playThenIdle = false;
    if (animInfo(anim).loop) {
        m_cmd = Cmd::None;
        ensureAnim(anim, 1.0);
    } else {
        m_cmd = Cmd::Play;
        startAnim(anim);
    }
}

void Locomotion::face(CatFacing facing)
{
    m_facing = facing;
}

void Locomotion::faceToward(QPoint desktopPoint)
{
    const QPoint a = anchor();
    if (m_mode == Mode::Attached && m_point.surface.kind != SurfaceKind::Floor) {
        // 벽: 목표가 위에 있으면 위 방향을 바라본다
        m_facing = facingForDirection(m_point.surface.kind, signOf(desktopPoint.y() - a.y()));
    } else if (desktopPoint.x() != a.x()) {
        m_facing = desktopPoint.x() < a.x() ? CatFacing::Left : CatFacing::Right;
    }
}

void Locomotion::hold(QPoint gripDesktop)
{
    if (!m_hasPosition)
        return;
    m_mode = Mode::Held;
    m_cmd = Cmd::None;
    m_playThenIdle = false;
    m_grip = gripDesktop;
    startAnim(CatAnim::Hang);
}

void Locomotion::moveHeld(QPoint gripDesktop)
{
    if (m_mode == Mode::Held)
        m_grip = gripDesktop;
}

void Locomotion::release()
{
    if (m_mode != Mode::Held)
        return;
    beginFall(QPointF(anchor()), QPointF());   // 던지기 없음: 초속도 0
}

void Locomotion::stop()
{
    if (m_mode == Mode::Held) {
        release();
        return;
    }
    if (m_mode == Mode::Airborne) {
        if (m_cmd == Cmd::Jump) {   // 공중에서는 멈출 수 없으므로 점프를 낙하로 바꾼다
            m_cmd = Cmd::Fall;
            startAnim(CatAnim::Fall);
        }
        return;
    }
    finishToIdle();
}

void Locomotion::tick(qint64 dtMs)
{
    if (!m_hasPosition || dtMs <= 0)
        return;

    advanceFrames(dtMs);

    if (m_mode == Mode::Airborne)
        tickAir(dtMs / kMsPerSec);
    else if (m_cmd == Cmd::Dash)
        tickDash(dtMs / kMsPerSec);
}

// ── 상태 ─────────────────────────────────────────────────

bool Locomotion::isBusy() const
{
    return m_mode != Mode::Attached || m_cmd != Cmd::None;
}

std::optional<SurfacePoint> Locomotion::attachment() const
{
    if (m_mode != Mode::Attached || !m_hasPosition)
        return std::nullopt;
    return m_point;
}

QPoint Locomotion::anchor() const
{
    if (!m_hasPosition)
        return QPoint();
    if (m_mode == Mode::Airborne)
        return QPoint(int(std::lround(m_pos.x())), int(std::lround(m_pos.y())));
    if (m_mode == Mode::Held)
        return m_grip + (CatSprite::anchorIn(CatGravity::Down) - CatSprite::hangGripIn(m_facing)) * m_scale;
    if (const std::optional<QPoint> p = m_snapshot.resolve(m_point))
        return *p;
    return m_lastAnchor;
}

CatFrame Locomotion::frame() const
{
    CatFrame f;
    f.anim = m_anim;
    f.animFrame = m_frame;
    f.pose = keyPose(m_anim, m_frame);
    f.gravity = currentGravity();
    f.facing = m_facing;
    f.anchor = anchor();

    // Climb / Mantle 은 팔·발이 기본 프레임 마지막 줄(지면선 아래 1 스프라이트 px)까지 내려오므로 면에서 띄워 정확히 닿게 한다
    if (m_mode == Mode::Attached && (m_anim == CatAnim::Climb || m_anim == CatAnim::Mantle)) {
        const int lift = Config::kStretchLiftSpritePx * m_scale;
        if (f.gravity == CatGravity::Left)
            f.anchor.rx() += lift;
        else if (f.gravity == CatGravity::Right)
            f.anchor.rx() -= lift;
        else
            f.anchor.ry() -= lift;
    }
    return f;
}
