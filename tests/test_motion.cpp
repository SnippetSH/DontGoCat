// Locomotion + PathPlanner 테스트 (합성 스냅샷, 가상 시간). 실제 데스크탑을 건드리지 않는다.
// 실제 데스크탑 스캔 점검은 `--real-desktop` 옵션을 줄 때만 실행한다 (ctest 에서는 사용하지 않음).
#include "TestCheck.hpp"
#include "TestSnapshot.hpp"

#include "CatSprite.hpp"
#include "Config.hpp"
#include "DesktopScanner.hpp"
#include "Locomotion.hpp"
#include "PathPlanner.hpp"

#include <QCoreApplication>
#include <QDebug>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <random>

// A: 바닥에 붙은 큰 창, D: 낮은 창(점프 가능), B/C 중간, E: 도달 불가
static const quintptr hA = 1, hB = 2, hC = 3, hD = 4, hE = 5;
static const QRect rA(300, 400, 600, 640), rB(1100, 600, 500, 300), rC(1500, 300, 300, 400),
    rD(1000, 900, 200, 140), rE(100, 100, 200, 100);

static DesktopSnapshot fullSnap()
{
    DesktopSnapshot s = baseSnap();
    addWindow(s, hA, rA);
    addWindow(s, hD, QRect(960 + 400, 900, 200, 140));   // 바닥 위 낮은 창 (1360..1560, 지붕 900)
    addWindow(s, hB, rB);
    addWindow(s, hC, rC);
    addWindow(s, hE, rE);
    return s;
}

static void run(Locomotion &loco, int ticks, int dt = 16)
{
    for (int i = 0; i < ticks; ++i)
        loco.tick(dt);
}

static bool runUntilIdle(Locomotion &loco, int maxTicks = 5000, int dt = 16)
{
    for (int i = 0; i < maxTicks; ++i) {
        if (!loco.isBusy())
            return true;
        loco.tick(dt);
    }
    return !loco.isBusy();
}

static SurfacePoint floorPt(const DesktopSnapshot &s, int x)
{
    return Locomotion::pointOn(s.segments[0], x);
}

static const SurfaceSegment *seg(const DesktopSnapshot &s, quintptr owner, SurfaceKind k)
{
    for (const SurfaceSegment &x : s.segments)
        if (x.id.owner == owner && x.id.kind == k)
            return &x;
    return nullptr;
}

static Locomotion makeOnFloor(const DesktopSnapshot &s, int x)
{
    Locomotion l;
    l.setScale(3);
    l.spawn(s, QPoint(x, kFloorY));
    runUntilIdle(l);
    return l;
}

// ── 0. 스프라이트 앵커 ────────────────────────────────────
static void testSpriteAnchor()
{
    std::printf("[sprite anchor]\n");
    int standingMin = 99;
    for (CatAnim a : {CatAnim::Idle, CatAnim::Walk, CatAnim::Run, CatAnim::Sit, CatAnim::Land, CatAnim::JumpUp,
                      CatAnim::Fall, CatAnim::PawSwipe, CatAnim::Sleep, CatAnim::Roll, CatAnim::Sprawl, CatAnim::Crouch,
                      CatAnim::Climb, CatAnim::Mantle}) {
        int lo = 99, hi = -1;
        QString perFrame;
        for (int f = 0; f < animInfo(a).frameCount(); ++f) {
            const QRect r = CatSprite::opaqueBounds(CatSprite::render(keyPose(a, f)));
            lo = std::min(lo, r.bottom());
            hi = std::max(hi, r.bottom());
            perFrame += QString::number(r.bottom()) + " ";
        }
        std::printf("  %-10s bottom rows min=%d max=%d  [%s]\n", animInfo(a).name, lo, hi, perFrame.toLatin1().data());
        if (a == CatAnim::Idle || a == CatAnim::Walk || a == CatAnim::Run)
            standingMin = std::min(standingMin, hi);
    }
    CHECK(standingMin == CatSprite::Height - 2, "standing lowest row %d", standingMin);
    CHECK(CatSprite::anchorIn(CatGravity::Down) == QPoint(16, 23), "down");
    CHECK(CatSprite::anchorIn(CatGravity::Left) == QPoint(1, 16), "left");
    CHECK(CatSprite::anchorIn(CatGravity::Right) == QPoint(23, 16), "right");
    // 오른쪽 측면 벽 (gravity Left): 서 있는 22 줄이 회전 후 1열 → 그 열의 바깥 경계가 앵커 x
    const QImage cw = CatSprite::renderOriented(keyPose(CatAnim::Idle, 0), CatGravity::Left);
    int firstCol = 99;
    for (int y = 0; y < cw.height(); ++y)
        for (int x = 0; x < cw.width(); ++x)
            if (qAlpha(cw.pixel(x, y)) > 0)
                firstCol = std::min(firstCol, x);
    CHECK(firstCol == CatSprite::anchorIn(CatGravity::Left).x(), "firstCol=%d", firstCol);
    const QImage ccw = CatSprite::renderOriented(keyPose(CatAnim::Idle, 0), CatGravity::Right);
    int lastCol = -1;
    for (int y = 0; y < ccw.height(); ++y)
        for (int x = 0; x < ccw.width(); ++x)
            if (qAlpha(ccw.pixel(x, y)) > 0)
                lastCol = std::max(lastCol, x);
    CHECK(lastCol + 1 == CatSprite::anchorIn(CatGravity::Right).x(), "lastCol=%d", lastCol);
}

// ── 1. 낙하/착지/터널링 ──────────────────────────────────
static void testFall()
{
    std::printf("[fall / spawn]\n");
    DesktopSnapshot s = fullSnap();
    for (int dt : {16, 50, 250}) {
        Locomotion l;
        l.setScale(3);
        l.spawn(s, QPoint(400, 0));   // A 지붕(400) 위 → A 지붕에 착지해야 함
        double prevY = -1;
        bool mono = true;
        for (int i = 0; i < 2000 && l.isBusy(); ++i) {
            l.tick(dt);
            const double y = l.anchor().y();
            if (y < prevY)
                mono = false;
            prevY = y;
        }
        CHECK(mono, "monotone dt=%d", dt);
        CHECK(l.mode() == Locomotion::Mode::Attached, "dt=%d", dt);
        CHECK(l.anchor() == QPoint(400, 400), "dt=%d anchor=(%d,%d)", dt, l.anchor().x(), l.anchor().y());
        CHECK(l.attachment() && l.attachment()->surface.owner == hA, "on A");
    }
    // 모니터 밖에서 스폰 → 가장 가까운 모니터 바닥
    Locomotion l;
    l.setScale(3);
    l.spawn(s, QPoint(-500, 200));
    runUntilIdle(l);
    CHECK(l.mode() == Locomotion::Mode::Attached && l.anchor().y() == kFloorY, "snap (%d,%d)", l.anchor().x(), l.anchor().y());
    // 모든 바닥 아래에서 스폰
    Locomotion l2;
    l2.setScale(3);
    l2.spawn(s, QPoint(500, 1500));
    runUntilIdle(l2);
    CHECK(l2.mode() == Locomotion::Mode::Attached && l2.anchor().y() == kFloorY, "below-all (%d,%d)", l2.anchor().x(), l2.anchor().y());
    // Land 애니 후 idle
    CHECK(l.frame().anim == CatAnim::Idle, "anim after land %d", int(l.frame().anim));
}

// ── 2. 걷기 / 방향 ───────────────────────────────────────
static void testWalk()
{
    std::printf("[walk / facing]\n");
    DesktopSnapshot s = fullSnap();
    Locomotion l = makeOnFloor(s, 100);
    CHECK(l.anchor() == QPoint(100, kFloorY), "start");

    std::vector<int> xs;
    l.onFrame = [&](CatAnim a, int) {
        if (a == CatAnim::Walk)
            xs.push_back(l.anchor().x());
    };
    const int target = 100 + 3 * 10 + 2;
    l.moveAlong(target, false);
    CHECK(l.isBusy(), "busy");
    CHECK(l.frame().facing == CatFacing::Right, "face right");
    runUntilIdle(l);
    xs.push_back(l.anchor().x());
    CHECK(l.anchor().x() == target, "stop at target %d", l.anchor().x());
    // 프레임 변경마다 증가량: 3,3,... 마지막 2
    std::vector<int> deltas;
    int prev = 100;
    for (size_t i = 0; i < xs.size(); ++i) {
        if (xs[i] != prev) {
            deltas.push_back(xs[i] - prev);
            prev = xs[i];
        }
    }
    bool okDelta = !deltas.empty();
    for (size_t i = 0; i + 1 < deltas.size(); ++i)
        okDelta = okDelta && deltas[i] == 3;
    okDelta = okDelta && deltas.back() == 2 && deltas.size() == 11;
    CHECK(okDelta, "deltas n=%zu", deltas.size());
    CHECK(l.frame().anim == CatAnim::Idle, "idle after");

    // 왼쪽 + run
    l.moveAlong(50, true);
    CHECK(l.frame().facing == CatFacing::Left && l.frame().anim == CatAnim::Run, "left run");
    std::vector<int> xr;
    l.onFrame = [&](CatAnim, int) { xr.push_back(l.anchor().x()); };
    runUntilIdle(l);
    CHECK(l.anchor().x() == 50, "run target %d", l.anchor().x());
    bool step6 = true;
    for (size_t i = 1; i + 1 < xr.size(); ++i)
        if (std::abs(xr[i] - xr[i - 1]) != 6 && xr[i] != xr[i - 1])
            step6 = false;
    CHECK(step6, "run step 6");

    // 세그먼트 밖 목표는 clamp
    l.onFrame = nullptr;
    l.moveAlong(-9999, true);
    runUntilIdle(l);
    CHECK(l.anchor().x() == 0 && l.mode() == Locomotion::Mode::Attached, "clamp left %d", l.anchor().x());

    // 같은 위치 목표 → 즉시 완료
    l.moveAlong(0, false);
    CHECK(!l.isBusy(), "no-op move");

    // 벽 방향표: A 오른쪽 벽 (WallRightSide)
    const SurfaceSegment *rw = seg(s, hA, SurfaceKind::WallRightSide);
    const SurfaceSegment *lw = seg(s, hA, SurfaceKind::WallLeftSide);
    struct Case { const SurfaceSegment *w; int dir; CatFacing expect; CatGravity g; const char *name; };
    for (const Case &c : {Case{rw, -1, CatFacing::Left, CatGravity::Left, "right wall up"},
                          Case{rw, 1, CatFacing::Right, CatGravity::Left, "right wall down"},
                          Case{lw, -1, CatFacing::Right, CatGravity::Right, "left wall up"},
                          Case{lw, 1, CatFacing::Left, CatGravity::Right, "left wall down"}}) {
        Locomotion w;
        w.setScale(3);
        w.spawn(s, QPoint(c.w->line, 700));   // 벽 선 위 700 → 낙하하므로 대신 점프로 부착
        // 직접 부착시키기 위해 점프 사용: 바닥에서 벽으로
        Locomotion j = makeOnFloor(s, c.w == rw ? 940 : 260);
        j.jumpTo(Locomotion::pointOn(*c.w, 800));
        runUntilIdle(j);
        CHECK(j.mode() == Locomotion::Mode::Attached && j.attachment() && j.attachment()->surface.kind == c.w->id.kind, "%s attach", c.name);
        const int start = j.anchor().y();
        j.moveAlong(j.attachment()->offset + c.dir * 60, false);
        CHECK(j.frame().facing == c.expect, "%s facing", c.name);
        CHECK(j.frame().gravity == c.g, "%s gravity", c.name);
        CHECK(j.frame().anim == CatAnim::Climb, "%s anim", c.name);
        runUntilIdle(j);
        CHECK(j.anchor().y() == start + c.dir * 60, "%s moved %d", c.name, j.anchor().y() - start);
        CHECK(j.anchor().x() == c.w->line, "x on line");
    }
}

// ── 3. 창 이동 / 소실 ────────────────────────────────────
static void testWindowFollow()
{
    std::printf("[window follow / loss]\n");
    DesktopSnapshot s = fullSnap();
    // A 지붕 위에 올려놓기
    Locomotion l;
    l.setScale(3);
    l.spawn(s, QPoint(500, 300));
    runUntilIdle(l);
    CHECK(l.anchor() == QPoint(500, 400), "on A roof");

    DesktopSnapshot moved = baseSnap();
    addWindow(moved, hA, rA.translated(50, -30));
    l.onSnapshot(moved);
    CHECK(l.anchor() == QPoint(550, 370), "carried (%d,%d)", l.anchor().x(), l.anchor().y());
    CHECK(l.mode() == Locomotion::Mode::Attached, "still attached");

    // 창이 사라짐 → 낙하 → 바닥. 낙하 중 터널링 확인
    DesktopSnapshot gone = baseSnap();
    l.onSnapshot(gone);
    CHECK(l.mode() == Locomotion::Mode::Airborne, "falls");
    CHECK(l.frame().anim == CatAnim::Fall && l.frame().gravity == CatGravity::Down, "fall anim");
    int minY = l.anchor().y();
    for (int i = 0; i < 500 && l.mode() == Locomotion::Mode::Airborne; ++i) {
        l.tick(100);   // 일부러 큰 dt
        minY = std::max(minY, l.anchor().y());
    }
    CHECK(l.mode() == Locomotion::Mode::Attached && l.anchor() == QPoint(550, kFloorY), "landed (%d,%d)", l.anchor().x(), l.anchor().y());
    runUntilIdle(l);
    CHECK(l.frame().anim == CatAnim::Idle, "idle");

    // 창 (D) 위에서 소실되지만 아래에 다른 창 지붕(A)이 있으면 그 위에 착지
    DesktopSnapshot s2 = baseSnap();
    addWindow(s2, hA, QRect(300, 600, 600, 440));
    addWindow(s2, hB, QRect(400, 200, 300, 200));
    Locomotion m;
    m.setScale(3);
    m.spawn(s2, QPoint(500, 100));
    runUntilIdle(m);
    CHECK(m.anchor() == QPoint(500, 200) && m.attachment()->surface.owner == hB, "on B roof");
    DesktopSnapshot s3 = baseSnap();
    addWindow(s3, hA, QRect(300, 600, 600, 440));
    m.onSnapshot(s3);
    runUntilIdle(m);
    CHECK(m.anchor() == QPoint(500, 600) && m.attachment()->surface.owner == hA, "lands on A (%d,%d)", m.anchor().x(), m.anchor().y());

    // 세그먼트 축소(가림)로 offset 이 밖으로 나가면 낙하
    DesktopSnapshot s4 = baseSnap();
    DesktopSnapshot narrow = s4;
    addWindow(narrow, hA, QRect(300, 600, 600, 440));
    narrow.segments[1].b = 400;   // 지붕 구간을 [300,400) 로 잘라냄
    m.onSnapshot(narrow);
    runUntilIdle(m);
    CHECK(m.anchor().y() == kFloorY || m.anchor().y() == 600, "cut segment fall (%d,%d)", m.anchor().x(), m.anchor().y());
}

// ── 4. 점프 ──────────────────────────────────────────────
static void testJump()
{
    std::printf("[jump]\n");
    DesktopSnapshot s = fullSnap();
    // D 지붕 (x 1360..1560, y 900) — 바닥에서 140px 위, 수평 가까움
    const SurfaceSegment *dRoof = seg(s, hD, SurfaceKind::Floor);
    Locomotion l = makeOnFloor(s, 1250);
    const SurfacePoint target = Locomotion::pointOn(*dRoof, 1400);
    l.jumpTo(target);
    CHECK(l.mode() == Locomotion::Mode::Airborne && l.isBusy(), "airborne");
    CHECK(l.frame().anim == CatAnim::JumpUp && l.frame().gravity == CatGravity::Down, "jump anim");
    int minY = 99999;
    bool sawFall = false, sawJumpUp = false;
    bool landSeen = false;
    for (int i = 0; i < 400 && l.isBusy(); ++i) {
        l.tick(16);
        minY = std::min(minY, l.anchor().y());
        sawFall = sawFall || l.frame().anim == CatAnim::Fall;
        sawJumpUp = sawJumpUp || l.frame().anim == CatAnim::JumpUp;
        landSeen = landSeen || l.frame().anim == CatAnim::Land;
    }
    CHECK(l.mode() == Locomotion::Mode::Attached, "landed");
    CHECK(l.anchor() == QPoint(1400, 900), "exact landing (%d,%d)", l.anchor().x(), l.anchor().y());
    CHECK(minY < 900 - 20, "apex above target: %d", minY);
    CHECK(sawFall && sawJumpUp && landSeen, "anims %d %d %d", sawJumpUp, sawFall, landSeen);
    CHECK(l.attachment()->surface.owner == hD, "attached D");

    // 아래로 점프 (D 지붕 → 바닥)
    l.jumpTo(floorPt(s, 1300));
    runUntilIdle(l);
    CHECK(l.anchor() == QPoint(1300, kFloorY), "down jump (%d,%d)", l.anchor().x(), l.anchor().y());

    // 점프 중 목표 창이 움직임 → 새 위치에 정확히 착지
    Locomotion m = makeOnFloor(s, 1250);
    m.jumpTo(target);
    run(m, 10);
    DesktopSnapshot s2 = baseSnap();
    addWindow(s2, hA, rA);
    addWindow(s2, hD, QRect(1360 + 60, 900, 200, 140));
    m.onSnapshot(s2);
    runUntilIdle(m);
    CHECK(m.anchor() == QPoint(1460, 900), "retarget landing (%d,%d)", m.anchor().x(), m.anchor().y());

    // 점프 중 목표 소실 → 낙하해서 바닥
    Locomotion n = makeOnFloor(s, 1250);
    n.jumpTo(target);
    run(n, 10);
    DesktopSnapshot s3 = baseSnap();
    n.onSnapshot(s3);
    runUntilIdle(n);
    CHECK(n.mode() == Locomotion::Mode::Attached && n.anchor().y() == kFloorY, "target lost fall (%d,%d)", n.anchor().x(), n.anchor().y());

    // 벽으로 점프: A 오른쪽 벽
    const SurfaceSegment *rw = seg(s, hA, SurfaceKind::WallRightSide);
    Locomotion w = makeOnFloor(s, 1000);
    w.jumpTo(Locomotion::pointOn(*rw, 900));
    runUntilIdle(w);
    CHECK(w.anchor() == QPoint(900, 900), "wall landing (%d,%d)", w.anchor().x(), w.anchor().y());
    CHECK(w.frame().anim == CatAnim::Climb && w.frame().animFrame == 0 && w.frame().gravity == CatGravity::Left, "climb f0");
    CHECK(w.frame().facing == CatFacing::Left, "facing up on right wall");
    // 벽에서 이륙: gravity Down
    w.jumpTo(floorPt(s, 1000));
    CHECK(w.frame().gravity == CatGravity::Down, "takeoff gravity Down");
    runUntilIdle(w);
    CHECK(w.anchor() == QPoint(1000, kFloorY), "wall→floor jump (%d,%d)", w.anchor().x(), w.anchor().y());

    // 벽에 붙은 상태에서 지지 창이 사라지면 낙하
    Locomotion v = makeOnFloor(s, 1000);
    v.jumpTo(Locomotion::pointOn(*rw, 900));
    runUntilIdle(v);
    v.onSnapshot(baseSnap());
    CHECK(v.mode() == Locomotion::Mode::Airborne, "wall loss falls");
    runUntilIdle(v);
    CHECK(v.anchor() == QPoint(900, kFloorY), "wall loss landing (%d,%d)", v.anchor().x(), v.anchor().y());
}

// ── 5. 모서리 ────────────────────────────────────────────
static void testCorner()
{
    std::printf("[corner]\n");
    DesktopSnapshot s = fullSnap();
    const SurfaceSegment *rw = seg(s, hA, SurfaceKind::WallRightSide);
    const SurfaceSegment *lw = seg(s, hA, SurfaceKind::WallLeftSide);
    const SurfaceSegment *roof = seg(s, hA, SurfaceKind::Floor);
    const SurfaceSegment *floor = &s.segments[0];

    for (const SurfaceSegment *w : {rw, lw}) {
        const bool right = w == rw;
        const auto link = Locomotion::cornerLink(*w, *floor, 3);
        CHECK(link.has_value(), "bottom link exists");
        if (!link)
            continue;
        const QPoint fp = *s.resolve(link->floor);
        const QPoint wp = *s.resolve(link->wall);
        std::printf("  %s bottom link: floor (%d,%d)  wall (%d,%d)\n", right ? "right" : "left", fp.x(), fp.y(), wp.x(), wp.y());
        CHECK(fp.x() == (right ? w->line + 36 : w->line - 36) && fp.y() == kFloorY, "floor pt");
        CHECK(wp.x() == w->line && wp.y() == kFloorY - 48, "wall pt");

        // 벽 → 바닥
        Locomotion l = makeOnFloor(s, right ? 1000 : 200);
        l.jumpTo(Locomotion::pointOn(*w, 800));
        runUntilIdle(l);
        l.moveAlong(link->wall.offset, false);
        runUntilIdle(l);
        CHECK(l.anchor() == wp, "at corner (%d,%d)", l.anchor().x(), l.anchor().y());
        l.corner(link->floor);
        CHECK(l.isBusy() && l.frame().anim == CatAnim::Sit && l.frame().gravity != CatGravity::Down, "sit on wall first");
        runUntilIdle(l);
        CHECK(l.anchor() == fp && l.frame().gravity == CatGravity::Down, "rotated to floor (%d,%d)", l.anchor().x(), l.anchor().y());
        CHECK(l.frame().facing == (right ? CatFacing::Right : CatFacing::Left), "faces away from wall");
        // 바닥 → 벽
        l.corner(link->wall);
        CHECK(l.isBusy() && l.frame().anim == CatAnim::Sit && l.frame().gravity == CatGravity::Down, "sit on floor first");
        runUntilIdle(l);
        CHECK(l.anchor() == wp && l.frame().gravity == gravityFor(w->id.kind), "rotated to wall (%d,%d)", l.anchor().x(), l.anchor().y());
        CHECK(l.frame().anim == CatAnim::Climb && l.frame().animFrame == 0, "climb f0");
    }

    // 벽 위 → 지붕 (mantle)
    for (const SurfaceSegment *w : {rw, lw}) {
        const bool right = w == rw;
        const auto link = Locomotion::cornerLink(*w, *roof, 3);
        CHECK(link.has_value(), "top link exists");
        if (!link)
            continue;
        const QPoint rp = *s.resolve(link->floor);
        const QPoint wp = *s.resolve(link->wall);
        std::printf("  %s top link: roof (%d,%d)  wall (%d,%d)\n", right ? "right" : "left", rp.x(), rp.y(), wp.x(), wp.y());
        CHECK(wp.y() == 400 && rp.y() == 400, "y");
        CHECK(rp.x() == (right ? w->line - 6 : w->line + 6), "roof x");

        Locomotion l = makeOnFloor(s, right ? 1000 : 200);
        l.jumpTo(Locomotion::pointOn(*w, 900));
        runUntilIdle(l);
        l.moveAlong(link->wall.offset, false);
        runUntilIdle(l);
        CHECK(l.anchor() == wp, "at wall top (%d,%d)", l.anchor().x(), l.anchor().y());
        l.corner(link->floor);
        CHECK(l.frame().anim == CatAnim::Mantle && l.frame().gravity == CatGravity::Down && l.anchor() == rp, "mantle in roof frame");
        CHECK(l.frame().facing == (right ? CatFacing::Left : CatFacing::Right), "mantle faces into window");
        std::vector<int> frames;
        l.onFrame = [&](CatAnim a, int f) { if (a == CatAnim::Mantle) frames.push_back(f); };
        runUntilIdle(l);
        CHECK(frames == std::vector<int>({1, 2}), "mantle frames n=%zu", frames.size());
        CHECK(l.anchor() == rp && l.attachment()->surface.owner == hA && l.attachment()->surface.kind == SurfaceKind::Floor, "on roof");
        CHECK(l.frame().anim == CatAnim::Idle, "idle after mantle");

        // 역방향: 지붕 모서리 → 벽 (아래로)
        frames.clear();
        l.corner(link->wall);
        CHECK(l.frame().anim == CatAnim::Mantle && l.frame().animFrame == 2, "reverse starts at last frame");
        runUntilIdle(l);
        CHECK(frames == std::vector<int>({2, 1, 0}), "reverse frames n=%zu", frames.size());
        CHECK(l.anchor() == wp && l.frame().gravity == gravityFor(w->id.kind), "back on wall top");
        CHECK(l.frame().facing == (right ? CatFacing::Right : CatFacing::Left), "faces down");
    }

    // 잘못된 corner 는 무시
    Locomotion l = makeOnFloor(s, 1700);
    l.corner(Locomotion::pointOn(*rw, 800));
    CHECK(!l.isBusy() && l.anchor() == QPoint(1700, kFloorY), "invalid corner ignored");
}

// ── 6. walk off edge ────────────────────────────────────
static void testWalkOff()
{
    std::printf("[walk off edge]\n");
    DesktopSnapshot s = fullSnap();
    const SurfaceSegment *roof = seg(s, hA, SurfaceKind::Floor);
    Locomotion l;
    l.setScale(3);
    l.spawn(s, QPoint(roof->b - 100, roof->line));
    runUntilIdle(l);
    CHECK(l.anchor() == QPoint(roof->b - 100, 400), "start on roof");
    l.walkOffEdge(true);
    bool fell = false;
    for (int i = 0; i < 4000 && l.isBusy(); ++i) {
        l.tick(16);
        fell = fell || l.mode() == Locomotion::Mode::Airborne;
    }
    CHECK(fell && l.mode() == Locomotion::Mode::Attached, "walked off and landed");
    CHECK(l.anchor().y() == kFloorY && l.anchor().x() >= roof->b - 1 && l.anchor().x() < roof->b + 60, "landing near edge (%d,%d)", l.anchor().x(), l.anchor().y());
}

// ── 6b. Run 프레임 시간 배율 (QuitBlock 접근) ─────────────
static void testRunFrameScale()
{
    std::printf("[run frame scale]\n");
    DesktopSnapshot s = fullSnap();
    const CatAnimInfo &run = animInfo(CatAnim::Run);
    const int step = run.speedPxPerFrame * 3;

    // 배율이 작을수록 빠르고 (속도 ∝ 1/배율), 프레임마다 정확히 speedPxPerFrame × scale 만 움직인다.
    // 0.1 은 프레임 시간(7ms)이 tick(16ms)보다 짧아 한 tick 에 여러 프레임이 넘어가는 경우를 검사한다.
    double ref = 0.0;
    for (double sc : {1.0, Config::kBlockRunFrameMsScale, 0.1}) {
        Locomotion l = makeOnFloor(s, 100);
        std::vector<int> xs;
        l.onFrame = [&](CatAnim a, int) {
            if (a == CatAnim::Run)
                xs.push_back(l.anchor().x());
        };
        const int dist = step * 40;
        l.moveAlong(100 + dist, true, sc);
        CHECK(l.frame().anim == CatAnim::Run, "run anim sc=%.2f", sc);
        int ms = 0;
        while (l.isBusy() && ms < 60000) {
            l.tick(16);
            ms += 16;
        }
        xs.push_back(l.anchor().x());
        CHECK(l.anchor().x() == 100 + dist, "arrived sc=%.2f x=%d", sc, l.anchor().x());
        bool exact = true;
        for (size_t i = 1; i < xs.size(); ++i)
            if (xs[i] != xs[i - 1] && std::abs(xs[i] - xs[i - 1]) != step)
                exact = false;
        CHECK(exact, "planted-foot step == %d for every frame sc=%.2f", step, sc);
        const double speed = dist * 1000.0 / ms;
        std::printf("  scale %.2f: %d px in %d ms = %.0f px/s\n", sc, dist, ms, speed);
        if (sc == 1.0) {
            ref = speed;
        } else {
            CHECK(std::abs(speed / ref - 1.0 / sc) < 0.2 / sc, "speed ratio %.2f (expect %.2f)", speed / ref, 1.0 / sc);
        }
    }

    // walk(run=false) 는 배율을 무시한다
    Locomotion w = makeOnFloor(s, 100);
    w.moveAlong(400, false, 0.1);
    CHECK(w.frame().anim == CatAnim::Walk, "walk anim");
    int ms = 0;
    while (w.isBusy() && ms < 60000) {
        w.tick(16);
        ms += 16;
    }
    const CatAnimInfo &wi = animInfo(CatAnim::Walk);
    const double walkExpect = 300.0 / (wi.speedPxPerFrame * 3) * std::accumulate(wi.frameMs.begin(), wi.frameMs.end(), 0) / wi.frameCount();
    CHECK(std::abs(ms - walkExpect) < walkExpect * 0.1, "walk unscaled: %d ms (expect ~%.0f)", ms, walkExpect);
}

// ── 7. 대시 ──────────────────────────────────────────────
static void testDash()
{
    std::printf("[dash]\n");
    DesktopSnapshot s = fullSnap();
    Locomotion l = makeOnFloor(s, 100);
    int restarts = 0;
    CatAnim lastAnim = CatAnim::Idle;
    int lastFrame = -1;
    int frameBacks = 0;
    l.onFrame = [&](CatAnim a, int f) {
        if (a != lastAnim)
            ++restarts;
        else if (f != (lastFrame + 1) % animInfo(a).frameCount())
            ++frameBacks;
        lastAnim = a;
        lastFrame = f;
    };
    l.dashAlong(1000);
    CHECK(l.isBusy() && l.frame().anim == CatAnim::Run && l.frame().facing == CatFacing::Right, "dash start");
    // 10 tick(0.16s) 동안 retarget 반복 → 속도 ≈ kDashSpeedSpritePxPerS × 3 (목표 1000 에 닿기 전까지만 잰다)
    int t0x = l.anchor().x();
    for (int i = 0; i < 10; ++i) {
        l.dashAlong(1000 + (i % 3));   // 목표가 계속 조금 바뀜
        l.tick(16);
    }
    const double v = (l.anchor().x() - t0x) / (10 * 0.016);
    std::printf("  dash speed %.0f px/s (expect %d)\n", v, Config::kDashSpeedSpritePxPerS * 3);
    CHECK(std::abs(v - Config::kDashSpeedSpritePxPerS * 3) < 100, "speed %f", v);
    CHECK(restarts <= 1 && frameBacks == 0, "no restart: restarts=%d frameBacks=%d", restarts, frameBacks);
    // 역방향 retarget
    l.dashAlong(100);
    CHECK(l.frame().facing == CatFacing::Left, "turn left");
    runUntilIdle(l);
    CHECK(l.anchor().x() == 100 && !l.isBusy() && l.frame().anim == CatAnim::Idle, "arrived x=%d", l.anchor().x());
    // 도착 후 같은 목표 호출은 no-op, crouch 를 끊지 않음
    l.play(CatAnim::Crouch);
    l.dashAlong(100);
    CHECK(l.frame().anim == CatAnim::Crouch, "crouch kept");
    l.dashAlong(300);
    CHECK(l.frame().anim == CatAnim::Run && l.isBusy(), "dash resumes");
    // clamp
    l.dashAlong(100000);
    runUntilIdle(l);
    CHECK(l.anchor().x() == 1919, "clamped %d", l.anchor().x());
    l.stop();
}

// ── 8. 기타 명령 ─────────────────────────────────────────
static void testMisc()
{
    std::printf("[play / face / stop]\n");
    DesktopSnapshot s = fullSnap();
    Locomotion l = makeOnFloor(s, 500);
    l.play(CatAnim::Sit);
    CHECK(l.isBusy(), "sit busy");
    runUntilIdle(l);
    CHECK(l.frame().anim == CatAnim::Sit && l.frame().animFrame == 3, "sit holds last frame");
    l.play(CatAnim::Sleep);
    CHECK(!l.isBusy(), "loop play not busy");
    run(l, 100);
    CHECK(l.frame().anim == CatAnim::Sleep, "sleep");
    l.stop();
    CHECK(l.frame().anim == CatAnim::Idle, "stop->idle");
    l.faceToward(QPoint(100, 0));
    CHECK(l.frame().facing == CatFacing::Left, "faceToward left");
    l.faceToward(QPoint(900, 0));
    CHECK(l.frame().facing == CatFacing::Right, "faceToward right");
    l.face(CatFacing::Left);
    CHECK(l.frame().facing == CatFacing::Left, "face");
    // 이동 중 stop
    l.moveAlong(900, false);
    run(l, 30);
    l.stop();
    CHECK(!l.isBusy() && l.frame().anim == CatAnim::Idle, "stop move");
    // 배율 변경
    l.setScale(2);
    const int x0 = l.anchor().x();
    l.moveAlong(x0 + 100, false);
    std::vector<int> xs;
    l.onFrame = [&](CatAnim, int) { xs.push_back(l.anchor().x()); };
    run(l, 40);
    int d = 0;
    for (size_t i = 1; i < xs.size(); ++i)
        if (xs[i] != xs[i - 1])
            d = xs[i] - xs[i - 1];
    CHECK(d == 2, "scale 2 step=%d", d);
}

// ── 9. 플래너 ────────────────────────────────────────────
static int execRoute(Locomotion &l, const DesktopSnapshot &s, const Route &r)
{
    int ticks = 0;
    for (const RouteStep &st : r) {
        applyRouteStep(l, st);
        for (int i = 0; i < 20000 && l.isBusy(); ++i, ++ticks)
            l.tick(16);
        (void)s;
    }
    return ticks;
}

static void testPlanner()
{
    std::printf("[planner]\n");
    DesktopSnapshot s = fullSnap();
    PathPlanner pp;
    pp.setScale(3);

    auto dumpRoute = [&](const char *name, const Route &r) {
        std::printf("  %s: %zu steps:", name, r.size());
        for (const RouteStep &st : r) {
            const char *k = st.kind == RouteStep::Kind::Move ? "Move" : st.kind == RouteStep::Kind::Corner ? "Corner" : st.kind == RouteStep::Kind::Jump ? "Jump" : "WalkOff";
            const auto p = s.resolve(st.target);
            std::printf(" %s(%d,%d)", k, p ? p->x() : -1, p ? p->y() : -1);
        }
        std::printf("\n");
    };

    // 바닥 → A 지붕 (벽 오르기 + 모서리 + mantle)
    const SurfaceSegment *roofA = seg(s, hA, SurfaceKind::Floor);
    const SurfacePoint from = floorPt(s, 1700);   // 오른쪽 끝
    const SurfacePoint to = Locomotion::pointOn(*roofA, 600);
    auto t0 = std::chrono::steady_clock::now();
    auto route = pp.plan(s, from, to, true);
    auto t1 = std::chrono::steady_clock::now();
    CHECK(route.has_value(), "floor→roof A route");
    if (route) {
        dumpRoute("floor→A roof", *route);
        std::printf("  plan time %.3f ms\n", std::chrono::duration<double, std::milli>(t1 - t0).count());
        Locomotion l = makeOnFloor(s, 1700);
        const int ticks = execRoute(l, s, *route);
        CHECK(l.anchor() == QPoint(600, 400) && l.mode() == Locomotion::Mode::Attached, "executed → (%d,%d) ticks=%d", l.anchor().x(), l.anchor().y(), ticks);
        CHECK(l.attachment() && l.attachment()->surface.owner == hA, "on A roof");
    }

    // 도달 불가: E 지붕
    const SurfaceSegment *roofE = seg(s, hE, SurfaceKind::Floor);
    auto none = pp.plan(s, from, Locomotion::pointOn(*roofE, 50), true);
    CHECK(!none.has_value(), "E unreachable");
    // E 벽도 마찬가지
    const SurfaceSegment *wallE = seg(s, hE, SurfaceKind::WallRightSide);
    CHECK(!pp.plan(s, from, Locomotion::pointOn(*wallE, 20), false).has_value(), "E wall unreachable");

    // 바닥 → D 지붕 (점프 1회)
    const SurfaceSegment *roofD = seg(s, hD, SurfaceKind::Floor);
    auto rd = pp.plan(s, floorPt(s, 1250), Locomotion::pointOn(*roofD, 1450), false);
    CHECK(rd.has_value(), "to D roof");
    if (rd) {
        dumpRoute("floor→D roof", *rd);
        bool hasJump = false;
        for (const RouteStep &st : *rd)
            hasJump = hasJump || st.kind == RouteStep::Kind::Jump;
        CHECK(hasJump, "uses jump");
        Locomotion l = makeOnFloor(s, 1250);
        execRoute(l, s, *rd);
        CHECK(l.anchor() == QPoint(1450, 900), "D executed (%d,%d)", l.anchor().x(), l.anchor().y());
    }

    // A 지붕 → 바닥 (walk-off 또는 점프로 내려옴)
    auto down = pp.plan(s, Locomotion::pointOn(*roofA, 450), floorPt(s, 1000), true);
    CHECK(down.has_value(), "A roof → floor");
    if (down) {
        dumpRoute("A roof→floor", *down);
        Locomotion l;
        l.setScale(3);
        l.spawn(s, QPoint(450, 400));
        runUntilIdle(l);
        execRoute(l, s, *down);
        CHECK(l.anchor() == QPoint(1000, kFloorY), "down executed (%d,%d)", l.anchor().x(), l.anchor().y());
    }

    // 먼 목표: 오른쪽 위 C 지붕 (B 벽 → B 지붕 → C 왼쪽 벽 → C 지붕 …)
    const SurfaceSegment *roofC = seg(s, hC, SurfaceKind::Floor);
    auto far = pp.plan(s, floorPt(s, 100), Locomotion::pointOn(*roofC, 1650), true);
    CHECK(far.has_value(), "floor → C roof");
    if (far) {
        dumpRoute("floor→C roof", *far);
        Locomotion l = makeOnFloor(s, 100);
        execRoute(l, s, *far);
        CHECK(l.anchor() == QPoint(1650, 300), "far executed (%d,%d)", l.anchor().x(), l.anchor().y());
    }

    // 같은 위치 → 빈 경로
    auto same = pp.plan(s, from, from, false);
    CHECK(same && same->empty(), "same → empty");

    // nearestReachable / randomReachable
    auto nr = pp.nearestReachable(s, from, QPoint(600, 380));   // A 지붕 바로 위
    CHECK(nr && s.resolve(*nr) && *s.resolve(*nr) == QPoint(600, 400), "nearest above A roof");
    auto nr2 = pp.nearestReachable(s, from, QPoint(200, 150));   // E 근처 (도달 불가) → 가장 가까운 도달 가능 면
    if (nr2) {
        const QPoint p = *s.resolve(*nr2);
        std::printf("  nearest to (200,150): (%d,%d)\n", p.x(), p.y());
    }
    CHECK(nr2.has_value() && nr2->surface.owner != hE, "nearest avoids unreachable E");
    QRandomGenerator rng(1234);
    int onWindows = 0;
    for (int i = 0; i < 200; ++i) {
        auto rp = pp.randomReachable(s, from, rng);
        CHECK(rp.has_value() && s.resolve(*rp).has_value(), "random resolves");
        if (rp && rp->surface.owner != 0)
            ++onWindows;
        if (rp)
            CHECK(rp->surface.owner != hE, "random avoids E");
    }
    std::printf("  random: %d/200 on windows\n", onWindows);

    // 다른 배율
    PathPlanner p1;
    p1.setScale(1);
    CHECK(p1.plan(s, floorPt(s, 1700), Locomotion::pointOn(*roofA, 600), true).has_value(), "scale 1 plan");
    PathPlanner p4;
    p4.setScale(4);
    CHECK(p4.plan(s, floorPt(s, 1700), Locomotion::pointOn(*roofA, 600), true).has_value(), "scale 4 plan");
}

static DesktopSnapshot randomSnap(unsigned seed, int windows)
{
    std::mt19937 rng(seed);
    DesktopSnapshot s = baseSnap();
    // 모니터 2개
    MonitorInfo m2;
    m2.bounds = QRect(1920, 0, 1920, 1080);
    m2.workArea = m2.bounds;
    m2.floorY = 1080;
    s.monitors.push_back(m2);
    SurfaceSegment f2;
    f2.id.monitor = 1;
    f2.line = 1080;
    f2.a = 1920;
    f2.b = 3840;
    f2.ownerRect = m2.bounds;
    s.segments.push_back(f2);
    s.virtualBounds = QRect(0, 0, 3840, 1080);
    for (int i = 0; i < windows; ++i) {
        const int w = 200 + rng() % 700, h = 150 + rng() % 600;
        const int x = rng() % (3840 - w), y = rng() % (1040 - h);
        addWindow(s, quintptr(100 + i), QRect(x, y, w, h));
    }
    return s;
}

#if CCAT_TEST_DEBUG
static constexpr double kPlanWorstLimitMs = 200.0;
#else
static constexpr double kPlanWorstLimitMs = 50.0;
#endif

static void testPerf()
{
    std::printf("[planner perf]\n");
    PathPlanner pp;
    pp.setScale(3);
    for (int windows : {33, 60}) {
        DesktopSnapshot s = randomSnap(7, windows);
        double worst = 0, total = 0;
        int ok = 0, n = 0;
        std::mt19937 rng(5);
        for (int i = 0; i < 40; ++i) {
            const SurfaceSegment &a = s.segments[rng() % s.segments.size()];
            const SurfaceSegment &b = s.segments[rng() % s.segments.size()];
            const SurfacePoint pa = Locomotion::pointOn(a, a.a + int(rng() % a.length()));
            const SurfacePoint pb = Locomotion::pointOn(b, b.a + int(rng() % b.length()));
            auto t0 = std::chrono::steady_clock::now();
            auto r = pp.plan(s, pa, pb, true);
            auto t1 = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            worst = std::max(worst, ms);
            total += ms;
            ++n;
            ok += r.has_value();
        }
        std::printf("  windows=%d segments=%zu: avg %.3f ms worst %.3f ms (reachable %d/%d)\n", windows, s.segments.size(), total / n, worst, ok, n);
        // 시간 측정은 기기/빌드 구성에 따라 흔들리므로 평균·최악 값은 정보로만 출력하고,
        // 탐색이 폭주하는 경우만 잡도록 넉넉한 상한을 둔다 (Debug 는 더 넉넉히).
        CHECK(worst < kPlanWorstLimitMs, "worst %.3f ms (limit %.0f)", worst, kPlanWorstLimitMs);
    }
}

// 수동 점검 전용 (--real-desktop): 실제 창 배치로 스캔 → 착지 → 무작위 목표 경로 탐색
static void testReal()
{
    std::printf("[real scan]\n");
    DesktopScanner sc;
    sc.setScale(3);
    const DesktopSnapshot &s = sc.scanNow();
    std::printf("  monitors=%zu segments=%zu\n", s.monitors.size(), s.segments.size());
    Locomotion l;
    l.setScale(3);
    const QPoint c = s.monitors.empty() ? QPoint(100, 100) : s.monitors[0].bounds.center();
    l.spawn(s, QPoint(c.x(), 0));
    runUntilIdle(l);
    std::printf("  landed at (%d,%d) on owner=%llu\n", l.anchor().x(), l.anchor().y(), (unsigned long long)l.attachment()->surface.owner);
    PathPlanner pp;
    pp.setScale(3);
    QRandomGenerator rng(1);
    int ok = 0, tries = 20;
    double worst = 0;
    for (int i = 0; i < tries; ++i) {
        auto target = pp.randomReachable(s, *l.attachment(), rng);
        if (!target)
            continue;
        auto t0 = std::chrono::steady_clock::now();
        auto r = pp.plan(s, *l.attachment(), *target, true);
        auto t1 = std::chrono::steady_clock::now();
        worst = std::max(worst, std::chrono::duration<double, std::milli>(t1 - t0).count());
        ok += r.has_value();
    }
    std::printf("  random targets planned %d/%d, worst plan %.3f ms\n", ok, tries, worst);
    CHECK(ok == tries, "all random reachable targets routable");
}

int main(int argc, char **argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);   // 시간 초과로 죽어도 진행 로그가 남도록
    QCoreApplication app(argc, argv);
    bool realDesktop = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--real-desktop") == 0)
            realDesktop = true;

    testSpriteAnchor();
    testFall();
    testWalk();
    testWindowFollow();
    testJump();
    testCorner();
    testWalkOff();
    testRunFrameScale();
    testDash();
    testMisc();
    testPlanner();
    testPerf();
    if (realDesktop)
        testReal();   // 실제 데스크탑을 스캔하므로 기본 실행(ctest)에서는 돌지 않는다
    return testing::testResult("test_motion");
}
