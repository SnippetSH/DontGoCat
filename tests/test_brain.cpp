// CatBrain 테스트 (합성 스냅샷, 가상 시간). 실제 커서 / 트레이 / 데스크탑을 건드리지 않는다.
// MouseWatcher 는 start() 하지 않고, nudge 는 setNudgeHook 으로 가로채 SetCursorPos 를 호출하지 않는다.
#include "TestCheck.hpp"
#include "TestSnapshot.hpp"

#include "CatBrain.hpp"
#include "CatSprite.hpp"
#include "Config.hpp"
#include "Locomotion.hpp"
#include "MouseWatcher.hpp"
#include "PathPlanner.hpp"

#include <QCoreApplication>
#include <QRandomGenerator>
#include <QDebug>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <numeric>
#include <set>
#include <string>
#include <vector>

static quint32 g_seed = 1;
static std::vector<QString> g_log;
static void handler(QtMsgType, const QMessageLogContext &ctx, const QString &msg)
{
    if (ctx.category && std::string(ctx.category) == "ccat.brain")
        g_log.push_back(msg);
}
static int logCount(const char *needle)
{
    int n = 0;
    for (const QString &s : g_log)
        if (s.contains(QString::fromLatin1(needle)))
            ++n;
    return n;
}

static const quintptr hA = 1, hD = 4, hE = 5, hB = 2;
static DesktopSnapshot fullSnap(bool withD = true)
{
    DesktopSnapshot s = baseSnap(36);   // 바닥 양 끝 36px 안쪽
    addWindow(s, hA, QRect(300, 400, 600, 640));          // big window on floor (wall climbing)
    if (withD)
        addWindow(s, hD, QRect(1360, 900, 200, 140));     // low window (jumpable)
    addWindow(s, hB, QRect(1000, 600, 300, 200));         // floating window
    addWindow(s, hE, QRect(100, 100, 200, 100));          // unreachable (high)
    return s;
}

struct Sim
{
    Locomotion body;
    PathPlanner planner;
    MouseWatcher mouse;
    CatBrain brain{body, planner, mouse};
    DesktopSnapshot snap;
    qint64 t = 0;
    std::vector<bool> ctLog;
    std::vector<bool> visLog;
    int abandoned = 0;
    std::set<CatAnim> animsSeen;
    int badAnchor = 0;
    int maxStepPx = 0;
    int nudgeCalls = 0;        // 가짜 nudge 훅 호출 수 (실제 커서는 움직이지 않는다)
    QPoint nudgeSum;           // 누적 이동량
    int badNudge = 0;          // 한 번에 kNudgeMinPx..kNudgeMaxPx 한 축이 아닌 호출
    QRandomGenerator rng{g_seed};   // 시드 고정 난수 (global() 은 건드리지 않는다)

    explicit Sim(const DesktopSnapshot &s) : snap(s)
    {
        brain.setRandomGenerator(&rng);
        mouse.setNudgeHook([this](int dx, int dy) {
            ++nudgeCalls;
            nudgeSum += QPoint(dx, dy);
            const int mag = std::abs(dx) + std::abs(dy);
            if ((dx != 0 && dy != 0) || mag < Config::kNudgeMinPx || mag > Config::kNudgeMaxPx)
                ++badNudge;
            return true;
        });
        body.setScale(3);
        planner.setScale(3);
        brain.setScale(3);
        QObject::connect(&brain, &CatBrain::wantClickThrough, [this](bool on) { ctLog.push_back(on); });
        QObject::connect(&brain, &CatBrain::visibilityChanged, [this](bool v) { visLog.push_back(v); });
        QObject::connect(&brain, &CatBrain::blockAbandoned, [this]() { ++abandoned; });
        body.onSnapshot(snap);
        brain.onSnapshot(snap);
    }
    void spawnAt(QPoint p)
    {
        body.spawn(snap, p);
        for (int i = 0; i < 400 && (body.mode() == Locomotion::Mode::Airborne || body.isBusy()); ++i)
            step();
        t = 0;
    }
    void setSnap(const DesktopSnapshot &s)
    {
        snap = s;
        body.onSnapshot(snap);
        brain.onSnapshot(snap);
    }
    void step()
    {
        t += 16;
        const QPoint before = body.anchor();
        body.tick(16);
        brain.tick(t);
        const CatFrame f = body.frame();
        animsSeen.insert(f.anim);
        if (body.mode() == Locomotion::Mode::Attached && body.attachment()) {
            if (!snap.resolve(*body.attachment()))
                ++badAnchor;
            maxStepPx = (std::max)(maxStepPx, int((body.anchor() - before).manhattanLength()));
        }
    }
    bool until(const std::function<bool()> &pred, int maxMs)
    {
        for (int ms = 0; ms < maxMs; ms += 16) {
            if (pred())
                return true;
            step();
        }
        return pred();
    }
    void run(int ms)
    {
        for (int i = 0; i < ms; i += 16)
            step();
    }
};

static const char *stName(CatBrain::State s)
{
    switch (s) {
    case CatBrain::State::Hidden: return "Hidden";
    case CatBrain::State::Grabbed: return "Grabbed";
    case CatBrain::State::Falling: return "Falling";
    case CatBrain::State::QuitBlock: return "QuitBlock";
    case CatBrain::State::TrayApproach: return "TrayApproach";
    case CatBrain::State::MousePlay: return "MousePlay";
    default: return "Autonomous";
    }
}

// 1. autonomous soak
static void testAutonomous()
{
    std::printf("[1] autonomous soak (30 min virtual)\n");
    g_log.clear();
    Sim sim(fullSnap());
    sim.spawnAt(QPoint(1000, 100));
    CHECK(sim.body.attachment().has_value(), "spawned");
    const int total = 30 * 60 * 1000;
    int wallTicks = 0, roofTicks = 0, fallingTicks = 0;
    for (int ms = 0; ms < total; ms += 16) {
        sim.step();
        if (auto a = sim.body.attachment()) {
            if (a->surface.kind != SurfaceKind::Floor)
                ++wallTicks;
            else if (a->surface.owner != 0)
                ++roofTicks;
        }
        if (sim.brain.state() == CatBrain::State::Falling)
            ++fallingTicks;
    }
    std::printf("  acts: idle %d wander %d explore %d sit %d sleep %d swipe %d sprawl %d | jump %d corner %d walkoff %d | plan failed %d gave up %d\n",
                logCount("act idle"), logCount("act wander"), logCount("act explore"), logCount("act sit"),
                logCount("act sleep"), logCount("act paw_swipe"), logCount("act sprawl"),
                logCount("Jump offset"), logCount("Corner offset"), logCount("WalkOff offset"),
                logCount("plan failed"), logCount("gave up"));
    std::printf("  wallTicks %d roofTicks %d fallingTicks %d badAnchor %d maxStepPx %d\n", wallTicks, roofTicks,
                fallingTicks, sim.badAnchor, sim.maxStepPx);
    CHECK(sim.badAnchor == 0, "attached with unresolved point");
    CHECK(logCount("act explore") > 5, "explored");
    CHECK(logCount("act sprawl") > 0 && logCount("act sleep") > 0, "rest options chosen");
    CHECK(roofTicks > 0, "visited a roof");
    CHECK(wallTicks > 0, "climbed a wall");
    CHECK(sim.animsSeen.count(CatAnim::Climb) && sim.animsSeen.count(CatAnim::Sprawl), "anims");
    // on wall: never sit/sleep: verified by brain code; check anims while on wall in a separate pass below
}

// 1b. on wall: only movement
static void testWallRestrictions()
{
    std::printf("[1b] wall restrictions\n");
    g_log.clear();
    Sim sim(fullSnap());
    sim.spawnAt(QPoint(1000, 100));
    int badOnWall = 0, wallTicks = 0;
    for (int ms = 0; ms < 20 * 60 * 1000; ms += 16) {
        sim.step();
        auto a = sim.body.attachment();
        if (a && a->surface.kind != SurfaceKind::Floor && sim.body.mode() == Locomotion::Mode::Attached) {
            ++wallTicks;
            const CatAnim an = sim.body.frame().anim;
            if (!sim.body.isBusy() && (an == CatAnim::Sit || an == CatAnim::Sleep || an == CatAnim::Sprawl || an == CatAnim::PawSwipe
                || an == CatAnim::Roll || an == CatAnim::Crouch))
                ++badOnWall;
        }
    }
    std::printf("  wallTicks %d badOnWall %d\n", wallTicks, badOnWall);
    CHECK(wallTicks > 0, "was on wall");
    CHECK(badOnWall == 0, "no sit/sleep/sprawl/swipe on wall");
}

// 2. tray approach
static void testTrayApproach()
{
    std::printf("[2] tray approach\n");
    g_log.clear();
    Sim sim(fullSnap());
    sim.spawnAt(QPoint(1000, kFloorY));
    sim.brain.onUserMoved(QPoint(1800, 1000));
    sim.brain.onTrayApproach(true, QPoint(1700, kFloorY));
    CHECK(sim.brain.state() == CatBrain::State::TrayApproach, "state %s", stName(sim.brain.state()));
    sim.animsSeen.clear();
    const bool arrived = sim.until([&] { return sim.body.anchor().x() == 1700 && !sim.body.isBusy(); }, 60000);
    CHECK(arrived, "arrived x=%d", sim.body.anchor().x());
    CHECK(!sim.animsSeen.count(CatAnim::Run), "walk only, no run");
    CHECK(sim.animsSeen.count(CatAnim::Walk), "walked");
    sim.run(1000);
    CHECK(sim.body.frame().anim == CatAnim::Sit, "sitting, anim %d", int(sim.body.frame().anim));
    CHECK(sim.body.frame().facing == CatFacing::Right, "faces cursor (right)");
    sim.brain.onUserMoved(QPoint(100, 1000));
    sim.run(100);
    CHECK(sim.body.frame().facing == CatFacing::Left, "turns toward cursor on the left");
    sim.brain.onTrayApproach(false, QPoint());
    CHECK(sim.brain.state() == CatBrain::State::Autonomous, "back to autonomous");

    // target on a roof (needs jump; walk only for floor moves)
    g_log.clear();
    Sim s2(fullSnap());
    s2.spawnAt(QPoint(1000, kFloorY));
    s2.brain.onTrayApproach(true, QPoint(1450, 900));
    const bool up = s2.until([&] { return s2.body.attachment() && s2.body.attachment()->surface.owner == hD && !s2.body.isBusy(); }, 30000);
    CHECK(up, "reached roof D");
    std::printf("  roof target: %d Jump steps\n", logCount("Jump offset"));
}

// 3. quit block
static void testQuitBlock()
{
    std::printf("[3] quit block\n");
    g_log.clear();
    Sim sim(fullSnap());
    sim.spawnAt(QPoint(500, kFloorY));
    const QRect popup(1610, 900, 190, 140);   // bottom touches 1040
    const QRect button(1657, 1016, 96, 24);   // bottom = 1040
    sim.brain.setPopupRect(popup);
    sim.brain.onUserMoved(QPoint(1700, 960));
    sim.brain.onBlockRequested(button);
    CHECK(sim.brain.state() == CatBrain::State::QuitBlock, "state");
    sim.animsSeen.clear();
    CHECK(sim.until([&] { return sim.ctLog.size() >= 1; }, 60000), "click-through off emitted");
    CHECK(!sim.ctLog.empty() && sim.ctLog.back() == false, "wantClickThrough(false)");
    CHECK(sim.until([&] { return std::abs(sim.body.anchor().x() - 1705) < 6 && !sim.body.isBusy(); }, 60000),
          "arrived at button center x=%d", sim.body.anchor().x());
    CHECK(sim.animsSeen.count(CatAnim::Run), "ran to the button");

    sim.brain.onUserMoved(QPoint(1680, 950));
    sim.run(900);
    int x1 = sim.body.anchor().x();
    std::printf("  cursor 1680 -> cat x %d anim %d\n", x1, int(sim.body.frame().anim));
    CHECK(std::abs(x1 - 1680) <= 3, "tracks cursor x");
    CHECK(sim.body.frame().anim == CatAnim::Crouch, "settled crouch (anim %d)", int(sim.body.frame().anim));
    sim.brain.onUserMoved(QPoint(1790, 950));   // clamp to button.right+1+36 = 1790
    sim.run(32);
    CHECK(sim.body.frame().anim == CatAnim::Run, "dashing again on cursor move (anim %d)", int(sim.body.frame().anim));
    sim.run(900);
    CHECK(std::abs(sim.body.anchor().x() - 1790) <= 3, "clamped x=%d", sim.body.anchor().x());
    sim.brain.onUserMoved(QPoint(1790, 600));   // y far above region (popup top 900-48=852)
    sim.until([&] { return std::abs(sim.body.anchor().x() - 1705) <= 3 && !sim.body.isBusy(); }, 8000);
    sim.run(300);
    std::printf("  outside region: cat x %d anim %d facing %d\n", sim.body.anchor().x(), int(sim.body.frame().anim),
                int(sim.body.frame().facing));
    CHECK(std::abs(sim.body.anchor().x() - 1705) <= 3, "returns to button center");
    CHECK(sim.body.frame().anim == CatAnim::Sit, "sits");
    CHECK(sim.body.frame().facing == CatFacing::Right, "faces cursor");

    sim.brain.onUserMoved(QPoint(1700, 960));
    sim.run(700);
    sim.animsSeen.clear();
    sim.brain.onBlockReact();
    sim.run(900);
    CHECK(sim.animsSeen.count(CatAnim::PawSwipe), "paw swipe on react");
    CHECK(sim.body.frame().anim != CatAnim::PawSwipe, "continues tracking after swipe");

    sim.ctLog.clear();
    sim.brain.onBlockReleased();
    CHECK(!sim.ctLog.empty() && sim.ctLog.back() == true, "click-through restored");
    CHECK(sim.until([&] { return sim.body.frame().anim == CatAnim::Sit && !sim.body.isBusy(); }, 10000), "sat aside");
    const int ax = sim.body.anchor().x();
    std::printf("  aside x=%d (button %d..%d)\n", ax, button.left(), button.right());
    CHECK(ax + 36 <= button.left() || ax - 36 > button.right(), "body clear of the button");
    CHECK(sim.until([&] { return sim.brain.state() == CatBrain::State::Autonomous; }, 10000), "autonomous after aside");

    // unreachable: no floor under button
    g_log.clear();
    Sim s2(fullSnap());
    s2.spawnAt(QPoint(500, kFloorY));
    s2.brain.onBlockRequested(QRect(2500, 1016, 96, 24));
    s2.run(100);
    CHECK(s2.abandoned == 1, "abandoned when no floor (%d)", s2.abandoned);
    CHECK(s2.brain.state() == CatBrain::State::Autonomous, "autonomous after abandon");

    // unreachable: disconnected monitor floor
    {
        DesktopSnapshot s = fullSnap();
        MonitorInfo m2;
        m2.bounds = QRect(3000, 0, 1920, 1080);
        m2.workArea = m2.bounds;
        m2.floorY = kFloorY;
        s.monitors.push_back(m2);
        SurfaceSegment f;
        f.id.owner = 0;
        f.id.monitor = 1;
        f.line = kFloorY;
        f.a = 3000;
        f.b = 4920;
        f.ownerRect = m2.bounds;
        s.segments.push_back(f);
        Sim s3(s);
        s3.spawnAt(QPoint(500, kFloorY));
        s3.brain.onBlockRequested(QRect(4000, 1016, 96, 24));
        CHECK(s3.until([&] { return s3.abandoned > 0; }, 20000), "abandoned for unreachable monitor floor");
        CHECK(s3.ctLog.empty() || s3.ctLog.back() == true, "click-through not left off");
        std::printf("  unreachable: abandoned=%d plan-fail=%d\n", s3.abandoned, logCount("plan failed"));
    }
}

// 3b. quit block approach: 빠른 Run (kBlockRunFrameMsScale) + 같은 바닥 마지막 구간 대시
static double avgFrameMs(CatAnim a)
{
    const CatAnimInfo &i = animInfo(a);
    return std::accumulate(i.frameMs.begin(), i.frameMs.end(), 0) / double(i.frameCount());
}

struct RunEvent { qint64 t; int x; };

static void testBlockApproach()
{
    std::printf("[3b] quit block approach\n");
    const QRect button(1657, 1016, 96, 24);
    const int runStep = animInfo(CatAnim::Run).speedPxPerFrame * 3;
    // 대시는 초당 kDashSpeedSpritePxPerS × scale(3) px → tick(16ms) 당 기대 이동량. 튜닝 값이 바뀌어도 Config 에서 유도한다
    const double dashPerTick = Config::kDashSpeedSpritePxPerS * 3 * 0.016;
    const double dashFastPx = dashPerTick * 0.8;   // 이 이상 움직인 tick 만 "대시 tick" 으로 센다
    CHECK(dashFastPx > runStep, "dash tuning distinguishable from run step (%.1f px/tick vs %d)", dashFastPx, runStep);

    // (a) 같은 바닥: 경로가 바닥 Move 하나 → 대시. 도착하면 곧바로 가드
    {
        g_log.clear();
        Sim sim(fullSnap());
        sim.spawnAt(QPoint(500, kFloorY));
        sim.brain.setPopupRect(QRect(1610, 900, 190, 140));
        sim.brain.onUserMoved(QPoint(1700, 960));
        sim.brain.onBlockRequested(button);
        int maxTickPx = 0;
        const bool arrived = sim.until([&] {
            maxTickPx = (std::max)(maxTickPx, sim.maxStepPx);
            return logCount("block: arrived, guarding") > 0;
        }, 20000);
        CHECK(arrived, "same-floor approach arrived (x=%d)", sim.body.anchor().x());
        CHECK(std::abs(sim.body.anchor().x() - 1705) <= 6, "arrived near button center x=%d", sim.body.anchor().x());
        std::printf("  same-floor: max px/tick %d (dash ~%.1f, run step %d)\n", maxTickPx, dashPerTick, runStep);
        CHECK(maxTickPx >= dashFastPx, "dash used on the final same-floor leg (max %d px/tick, expect >= %.1f)", maxTickPx, dashFastPx);
        CHECK(sim.ctLog.size() >= 1 && sim.ctLog.back() == false, "click-through off on arrival");
    }

    // (b) 지붕에서 출발: 지붕 위 Run 은 kBlockRunFrameMsScale 로 빠르고 디딤발 고정, 바닥 마지막 구간은 대시
    {
        g_log.clear();
        Sim sim(fullSnap());
        sim.spawnAt(QPoint(1400, 100));   // 낮은 창 D 지붕
        CHECK(sim.body.attachment() && sim.body.attachment()->surface.owner == hD, "spawned on roof D");
        std::vector<RunEvent> ev;
        int floorDashTicks = 0;
        sim.body.onFrame = [&](CatAnim a, int) {
            const auto att = sim.body.attachment();
            if (a == CatAnim::Run && att && att->surface.owner == hD && sim.body.mode() == Locomotion::Mode::Attached)
                ev.push_back({sim.t, sim.body.anchor().x()});
        };
        sim.brain.setPopupRect(QRect(1610, 900, 190, 140));
        sim.brain.onUserMoved(QPoint(1700, 960));
        sim.brain.onBlockRequested(button);
        int prevX = sim.body.anchor().x();
        const bool arrived = sim.until([&] {
            const auto att = sim.body.attachment();
            if (sim.body.mode() == Locomotion::Mode::Attached && att && att->surface.owner == 0) {
                if (std::abs(sim.body.anchor().x() - prevX) >= dashFastPx)
                    ++floorDashTicks;
            }
            prevX = sim.body.anchor().x();
            return logCount("block: arrived, guarding") > 0;
        }, 30000);
        CHECK(arrived, "roof approach arrived (x=%d)", sim.body.anchor().x());
        CHECK(std::abs(sim.body.anchor().x() - 1705) <= 6, "arrived near button center x=%d", sim.body.anchor().x());
        CHECK(ev.size() > 8, "enough Run frames on the roof (%zu)", ev.size());
        bool planted = true;
        double sumDt = 0.0;
        int n = 0;
        for (size_t i = 1; i < ev.size(); ++i) {
            if (ev[i].x != ev[i - 1].x && std::abs(ev[i].x - ev[i - 1].x) != runStep)
                planted = false;
            sumDt += double(ev[i].t - ev[i - 1].t);
            ++n;
        }
        const double mean = n ? sumDt / n : 0.0;
        const double expect = avgFrameMs(CatAnim::Run) * Config::kBlockRunFrameMsScale;
        std::printf("  roof run: %d frames, mean %.1f ms/frame (expect ~%.1f), floor dash ticks %d\n", n, mean, expect, floorDashTicks);
        CHECK(planted, "planted-foot step exactly %d px per Run frame", runStep);
        CHECK(n > 0 && mean < avgFrameMs(CatAnim::Run) * 0.6 && std::abs(mean - expect) < 12.0,
              "approach Run frames scaled: %.1f ms (expect ~%.1f)", mean, expect);
        CHECK(floorDashTicks > 2, "final floor leg dashed (%d fast ticks)", floorDashTicks);
    }

    // (c) 평소 자율 달리기는 배율 영향이 없다
    {
        g_log.clear();
        Sim sim(fullSnap());
        sim.spawnAt(QPoint(1000, 100));
        std::vector<RunEvent> ev;
        sim.body.onFrame = [&](CatAnim a, int) {
            if (a == CatAnim::Run && sim.body.mode() == Locomotion::Mode::Attached)
                ev.push_back({sim.t, sim.body.anchor().x()});
        };
        sim.run(10 * 60 * 1000);
        double sumDt = 0.0;
        int n = 0;
        for (size_t i = 1; i < ev.size(); ++i) {
            const qint64 dt = ev[i].t - ev[i - 1].t;
            if (dt <= 100 && std::abs(ev[i].x - ev[i - 1].x) == runStep) {   // 연속된 Run 프레임만
                sumDt += double(dt);
                ++n;
            }
        }
        const double mean = n ? sumDt / n : 0.0;
        std::printf("  autonomous run: %d frames, mean %.1f ms/frame (normal %.1f)\n", n, mean, avgFrameMs(CatAnim::Run));
        CHECK(n > 20, "autonomous run happened (%d frames)", n);
        CHECK(std::abs(mean - avgFrameMs(CatAnim::Run)) < 12.0, "autonomous Run unscaled: %.1f ms", mean);
    }
}

// 4. mouse play
static void testMousePlay()
{
    std::printf("[4] mouse play\n");

    g_log.clear();
    Sim sim(fullSnap());
    sim.spawnAt(QPoint(1500, kFloorY));
    const QPoint cur(1000, kFloorY - 40);
    sim.brain.onUserMoved(cur);
    sim.run(200);
    sim.brain.onMouseIdle(cur);
    // 자율 행동(탐험 경로 등)이 진행 중이면 끝난 뒤에 평가하므로 충분히 기다린다
    CHECK(sim.until([&] { return sim.brain.state() == CatBrain::State::MousePlay; }, 60000), "play started (%s)", stName(sim.brain.state()));
    sim.animsSeen.clear();
    // 서는 자리 = 커서에서 kPlayStandOffsetSpritePx 만큼 떨어진, 시작할 때 고양이가 있던 쪽 (커서와 같거나 왼쪽이면 왼쪽에 선다)
    const bool fromLeft = sim.body.anchor().x() <= cur.x();
    const int standX = cur.x() + (fromLeft ? -1 : 1) * Config::kPlayStandOffsetSpritePx * 3;
    CHECK(sim.until([&] { return sim.body.frame().anim == CatAnim::Crouch && !sim.body.isBusy() && std::abs(sim.body.anchor().x() - standX) <= 3; }, 60000),
          "arrived at stand point (x %d, expect %d)", sim.body.anchor().x(), standX);
    CHECK(sim.body.frame().facing == (fromLeft ? CatFacing::Right : CatFacing::Left), "faces the cursor");
    CHECK(!sim.animsSeen.count(CatAnim::Run), "approach is walk");
    sim.animsSeen.clear();
    sim.run(40000);
    // 장난 종류(툭툭 / 배회 / 뒹굴기)는 가중치 추첨이라 한 판(최대 45초)에 전부 나온다는 보장이 없다.
    // 모두 관찰될 때까지 시드만 바꿔 같은 상황을 몇 판 더 돌린다 (고정 시드면 항상 같은 결과).
    auto allPlayAnims = [](const std::set<CatAnim> &a) {
        return a.count(CatAnim::PawSwipe) && a.count(CatAnim::Roll) && a.count(CatAnim::Walk);
    };
    std::set<CatAnim> played = sim.animsSeen;
    int nudgeCalls = sim.nudgeCalls, badNudge = sim.badNudge;
    bool driftOk = std::abs(sim.nudgeSum.x()) <= Config::kNudgeDriftMaxPx && std::abs(sim.nudgeSum.y()) <= Config::kNudgeDriftMaxPx;
    for (quint32 extra = 1; extra <= 8 && !(allPlayAnims(played) && logCount("play: poke") > 0); ++extra) {
        Sim more(fullSnap());
        more.rng.seed(g_seed + extra);
        more.spawnAt(QPoint(1500, kFloorY));
        more.brain.onUserMoved(cur);
        more.run(200);
        more.brain.onMouseIdle(cur);
        more.until([&] { return more.body.frame().anim == CatAnim::Crouch && !more.body.isBusy(); }, 60000);
        more.animsSeen.clear();
        more.run(40000);
        played.insert(more.animsSeen.begin(), more.animsSeen.end());
        nudgeCalls += more.nudgeCalls;
        badNudge += more.badNudge;
        driftOk = driftOk && std::abs(more.nudgeSum.x()) <= Config::kNudgeDriftMaxPx && std::abs(more.nudgeSum.y()) <= Config::kNudgeDriftMaxPx;
    }
    std::printf("  play: poke %d wander %d roll %d nudges %d\n", logCount("play: poke"), logCount("play: wander"),
                logCount("play: roll"), logCount("nudge"));
    CHECK(logCount("play: poke") > 0, "pokes");
    CHECK(allPlayAnims(played), "all play anims");
    CHECK(logCount("nudge") > 0 && nudgeCalls == logCount("nudge"), "nudged (log %d hook %d)", logCount("nudge"), nudgeCalls);
    CHECK(badNudge == 0, "each nudge is 1..2 px on one axis");
    CHECK(driftOk, "nudge drift bounded");
    sim.brain.onUserMoved(QPoint(1200, 500));
    CHECK(sim.brain.state() == CatBrain::State::Autonomous, "stopped on user move");
    sim.step();
    CHECK(sim.body.frame().anim != CatAnim::Roll, "no longer rolling");

    g_log.clear();
    Sim s2(fullSnap());
    s2.spawnAt(QPoint(1500, kFloorY));
    s2.brain.onMouseIdle(QPoint(1500, 300));
    CHECK(s2.brain.state() == CatBrain::State::Autonomous, "no play for unreachable cursor");

    // beside a wall: window A left side x=300, cursor 40px left at y=700
    g_log.clear();
    Sim s3(fullSnap());
    s3.spawnAt(QPoint(1500, kFloorY));
    s3.brain.onMouseIdle(QPoint(300 - 40, 700));
    CHECK(s3.brain.state() == CatBrain::State::MousePlay, "wall cursor reachable (%s)", stName(s3.brain.state()));
    CHECK(s3.until([&] { return s3.body.frame().anim == CatAnim::Crouch && s3.body.attachment() && s3.body.attachment()->surface.kind == SurfaceKind::WallLeftSide; }, 90000),
          "climbed beside cursor");
    std::printf("  wall: anchor %d,%d (cursor 260,700)\n", s3.body.anchor().x(), s3.body.anchor().y());
    s3.run(15000);
    std::printf("  wall play: poke %d wander %d roll %d\n", logCount("play: poke"), logCount("play: wander"), logCount("play: roll"));
    CHECK(logCount("play: roll") == 0, "no roll on wall");

    // pending idle evaluated later: cursor idle while tray approach active
    g_log.clear();
    Sim s4(fullSnap());
    s4.spawnAt(QPoint(1500, kFloorY));
    s4.brain.onTrayApproach(true, QPoint(1700, kFloorY));
    s4.brain.onMouseIdle(QPoint(1000, kFloorY - 40));
    CHECK(s4.brain.state() == CatBrain::State::TrayApproach, "tray approach outranks play");
    s4.brain.onTrayApproach(false, QPoint());
    s4.run(500);
    CHECK(s4.brain.state() == CatBrain::State::MousePlay, "pending idle evaluated after tray (%s)", stName(s4.brain.state()));

}

// 5. hidden / falling
static void testHiddenFalling()
{
    std::printf("[5] hidden / falling\n");
    g_log.clear();
    Sim sim(fullSnap());
    sim.spawnAt(QPoint(1450, 100));
    CHECK(sim.body.attachment() && sim.body.attachment()->surface.owner == hD, "on roof D");
    sim.setSnap(fullSnap(false));
    sim.step();
    CHECK(sim.brain.state() == CatBrain::State::Falling, "falling (%s)", stName(sim.brain.state()));
    CHECK(sim.until([&] { return sim.brain.state() != CatBrain::State::Falling; }, 5000), "landed");
    CHECK(sim.body.attachment() && sim.body.attachment()->surface.owner == 0, "on floor");
    CHECK(sim.brain.state() == CatBrain::State::Autonomous, "resumed autonomous");

    DesktopSnapshot fs = fullSnap();
    fs.monitors[0].fullscreen = true;
    sim.setSnap(fs);
    sim.step();
    CHECK(sim.brain.state() == CatBrain::State::Hidden, "hidden");
    CHECK(!sim.visLog.empty() && sim.visLog.back() == false, "visibility false");
    const QPoint p = sim.body.anchor();
    sim.run(3000);
    CHECK(sim.body.anchor() == p, "frozen while hidden");
    sim.setSnap(fullSnap());
    sim.step();
    CHECK(sim.brain.state() == CatBrain::State::Autonomous, "unhidden");
    CHECK(sim.visLog.size() == 2 && sim.visLog.back() == true, "visibility true");
    sim.run(5000);

    Sim s2(fullSnap());
    s2.spawnAt(QPoint(1500, kFloorY));
    s2.brain.onBlockRequested(QRect(1657, 1016, 96, 24));
    s2.run(200);
    DesktopSnapshot fs2 = fullSnap();
    fs2.monitors[0].fullscreen = true;
    s2.setSnap(fs2);
    s2.step();
    CHECK(s2.abandoned == 1, "block abandoned when hidden");
    s2.brain.onBlockRequested(QRect(1657, 1016, 96, 24));
    CHECK(s2.abandoned == 2, "block request while hidden abandoned");
    CHECK(s2.brain.state() == CatBrain::State::Hidden, "still hidden");

    // falling during QuitBlock approach: resume after landing
    Sim s3(fullSnap());
    s3.spawnAt(QPoint(1450, 100));  // on roof D
    s3.brain.onBlockRequested(QRect(1657, 1016, 96, 24));
    CHECK(s3.until([&] { return s3.body.attachment() && s3.body.attachment()->surface.owner == 0 && !s3.body.isBusy(); }, 20000), "left roof D");
    CHECK(s3.until([&] { return std::abs(s3.body.anchor().x() - 1705) < 6 && !s3.body.isBusy(); }, 30000), "arrived after jump down");
}

// 5b. grabbed (R10)
static void testGrabbed()
{
    std::printf("[5b] grabbed\n");
    g_log.clear();
    const auto anchorFor = [](QPoint grip, CatFacing f) {
        return grip + (CatSprite::anchorIn(CatGravity::Down) - CatSprite::hangGripIn(f)) * 3;
    };

    // 자율 행동 중 잡기 → 경로 중단 → 놓기 → Falling → 착지 → Autonomous
    Sim sim(fullSnap());
    sim.spawnAt(QPoint(1000, kFloorY));
    sim.run(2000);
    const QPoint grip(950, 600);   // A(~900) 와 B(1000~) 사이 허공: 아래는 모니터 바닥
    CHECK(sim.brain.grab(grip), "grab accepted");
    CHECK(sim.body.mode() == Locomotion::Mode::Held, "body Held");
    CHECK(sim.brain.state() == CatBrain::State::Grabbed, "state Grabbed (%s)", stName(sim.brain.state()));
    sim.run(500);
    CHECK(sim.brain.state() == CatBrain::State::Grabbed && sim.body.frame().anim == CatAnim::Hang, "still grabbed");
    CHECK(sim.body.anchor() == anchorFor(grip, sim.body.frame().facing), "anchor from grip");
    const QPoint grip2(950, 500);
    sim.brain.moveGrab(grip2);
    sim.run(100);
    CHECK(sim.body.anchor() == anchorFor(grip2, sim.body.frame().facing), "moveGrab follows");
    sim.brain.releaseGrab();
    sim.step();
    CHECK(sim.brain.state() == CatBrain::State::Falling, "Falling after release (%s)", stName(sim.brain.state()));
    CHECK(sim.until([&] { return sim.brain.state() != CatBrain::State::Falling; }, 5000), "landed");
    CHECK(sim.brain.state() == CatBrain::State::Autonomous, "resumed autonomous (%s)", stName(sim.brain.state()));
    CHECK(sim.body.attachment() && sim.body.attachment()->surface.owner == 0, "on the floor");
    sim.run(3000);

    // TrayApproach 중 잡기 → 놓으면 접근 재개
    Sim s2(fullSnap());
    s2.spawnAt(QPoint(500, kFloorY));
    s2.brain.onUserMoved(QPoint(1800, 1000));
    s2.brain.onTrayApproach(true, QPoint(1700, kFloorY));
    s2.run(500);
    CHECK(s2.brain.grab(QPoint(1050, 600)), "grab during tray approach");
    s2.run(1000);
    CHECK(s2.brain.state() == CatBrain::State::Grabbed, "grabbed while approaching");
    s2.brain.releaseGrab();
    CHECK(s2.until([&] { return s2.body.mode() == Locomotion::Mode::Attached && s2.brain.state() == CatBrain::State::TrayApproach; }, 5000),
          "tray approach resumes (%s)", stName(s2.brain.state()));
    CHECK(s2.until([&] { return s2.body.anchor().x() == 1700 && !s2.body.isBusy(); }, 60000), "arrived x=%d", s2.body.anchor().x());

    // QuitBlock 중에는 거절
    Sim s3(fullSnap());
    s3.spawnAt(QPoint(500, kFloorY));
    s3.brain.setPopupRect(QRect(1610, 900, 190, 140));
    s3.brain.onBlockRequested(QRect(1657, 1016, 96, 24));
    s3.run(500);
    CHECK(!s3.brain.grab(QPoint(1050, 600)), "grab refused during QuitBlock");
    CHECK(s3.body.mode() != Locomotion::Mode::Held, "body not Held");
    CHECK(s3.brain.state() == CatBrain::State::QuitBlock, "still QuitBlock (%s)", stName(s3.brain.state()));

    // Hidden 이 되면 잡힘이 풀리고, Hidden 중에는 거절
    Sim s4(fullSnap());
    s4.spawnAt(QPoint(500, kFloorY));
    CHECK(s4.brain.grab(QPoint(1050, 600)), "grab before hidden");
    DesktopSnapshot fs = fullSnap();
    fs.monitors[0].fullscreen = true;
    s4.setSnap(fs);
    s4.step();
    CHECK(s4.brain.state() == CatBrain::State::Hidden, "hidden (%s)", stName(s4.brain.state()));
    CHECK(s4.body.mode() != Locomotion::Mode::Held, "grab released by hidden");
    CHECK(!s4.brain.grab(QPoint(1050, 600)), "grab refused while hidden");
    s4.brain.releaseGrab();   // Held 가 아니면 무시
}

// 6. replan
static void testReplan()
{
    std::printf("[6] replan on invalidated route\n");
    g_log.clear();
    Sim sim(fullSnap());
    sim.spawnAt(QPoint(1000, kFloorY));
    sim.brain.onTrayApproach(true, QPoint(1450, 900));
    sim.run(300);
    sim.setSnap(fullSnap(false));
    sim.run(5000);
    CHECK(sim.brain.state() == CatBrain::State::Autonomous, "gave up gracefully (%s)", stName(sim.brain.state()));
    CHECK(logCount("route gave up") + logCount("route invalidated") > 0, "logged");
    CHECK(sim.badAnchor == 0, "no bad anchors");
}

int main(int argc, char **argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);   // 시간 초과로 죽어도 진행 로그가 남도록
    QCoreApplication app(argc, argv);
    qInstallMessageHandler(handler);
    // CatBrain 은 Sim 마다 주입한 QRandomGenerator(g_seed) 로 행동을 고른다. 재현 가능하도록 고정 시드 (`--seed N` 으로 변경 가능)
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--seed") == 0)
            g_seed = quint32(std::strtoul(argv[i + 1], nullptr, 10));
    std::printf("seed %u\n", g_seed);
    testAutonomous();
    testWallRestrictions();
    testTrayApproach();
    testQuitBlock();
    testBlockApproach();
    testMousePlay();
    testHiddenFalling();
    testGrabbed();
    testReplan();
    return testing::testResult("test_brain");
}
