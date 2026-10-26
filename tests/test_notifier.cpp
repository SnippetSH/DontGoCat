// BubbleUpdateNotifier 흐름 테스트 (합성 스냅샷, 가상 시간, 가짜 Player 로 소리 없음).
//   - notify → 브레인이 말하기 시작 → 말풍선 표시 (문구에 새 버전) + 울음 1회
//   - open / later / skip / 시간 초과 → 각각 OpenPage / Later / Skip / Later 가 정확히 한 번, 브레인은 Announce 를 떠난다
//   - 끼어듦(잡기) → Later 한 번, 말풍선 숨김
//   - 진행 중 notify 가 또 오면 앞의 것은 Later
//   - 진행 중이 아닐 때의 시그널은 무시
#include "TestCheck.hpp"
#include "TestSnapshot.hpp"

#include "BubbleUpdateNotifier.hpp"
#include "CatBrain.hpp"
#include "CatVoice.hpp"
#include "Config.hpp"
#include "Locomotion.hpp"
#include "MouseWatcher.hpp"
#include "PathPlanner.hpp"
#include "SpeechBubble.hpp"

#include <QApplication>
#include <QRandomGenerator>

#include <cstdio>
#include <vector>

struct Rig
{
    Locomotion body;
    PathPlanner planner;
    MouseWatcher mouse;
    CatBrain brain{body, planner, mouse};
    SpeechBubble bubble;
    int meows = 0;
    CatVoice voice{nullptr, [this](int) { ++meows; }};
    BubbleUpdateNotifier notifier{brain, bubble, voice, [this] { return placement(); }};
    DesktopSnapshot snap = baseSnap(36);
    QRandomGenerator rng{7};
    qint64 t = 0;
    std::vector<UpdateChoice> choices;

    Rig()
    {
        brain.setRandomGenerator(&rng);
        mouse.setNudgeHook([](int, int) { return true; });
        body.setScale(3);
        planner.setScale(3);
        brain.setScale(3);
        bubble.setScale(3);
        body.onSnapshot(snap);
        brain.onSnapshot(snap);
        body.spawn(snap, QPoint(1000, 100));
        for (int i = 0; i < 400 && (body.mode() == Locomotion::Mode::Airborne || body.isBusy()); ++i)
            step();
        t = 0;
    }
    BubblePlacement placement() const
    {
        BubblePlacement p;
        p.catRect = QRect(900, 900, 96, 96);
        p.monitor = QRect(0, 0, 1920, 1080);
        return p;
    }
    void step()
    {
        t += 16;
        body.tick(16);
        brain.tick(t);
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
    void notify(const QString &v = QStringLiteral("9.9.9"))
    {
        notifier.notify(v, QStringLiteral("1.0.0"), [this](UpdateChoice c) { choices.push_back(c); });
    }
    // notify 후 말풍선이 뜰 때까지 진행
    bool startShown(const QString &v = QStringLiteral("9.9.9"))
    {
        notify(v);
        return until([this] { return bubble.isVisible(); }, 120000);
    }
};

static void testChoice(const char *name, const std::function<void(Rig &)> &act, UpdateChoice expect)
{
    std::printf("[choice] %s\n", name);
    Rig r;
    CHECK(r.startShown(), "bubble shown");
    CHECK(r.bubble.text().contains(QStringLiteral("9.9.9")), "text has version");
    CHECK(r.meows == 1, "meow once (%d)", r.meows);
    CHECK(r.brain.state() == CatBrain::State::Announce, "brain announcing");
    act(r);
    CHECK(r.choices.size() == 1 && r.choices[0] == expect, "choice (%d)", int(r.choices.size()));
    CHECK(r.brain.state() != CatBrain::State::Announce, "brain left Announce");
    // 늦게 온 시그널은 무시
    emit r.bubble.openClicked();
    emit r.bubble.timedOut();
    CHECK(r.choices.size() == 1, "stale ignored (%d)", int(r.choices.size()));
    CHECK(r.meows == 1, "still one meow");
}

static void testInterrupt()
{
    std::printf("[interrupt] grab\n");
    Rig r;
    CHECK(r.startShown(), "bubble shown");
    CHECK(r.brain.grab(r.body.anchor()), "grabbed");
    r.step();
    CHECK(r.choices.size() == 1 && r.choices[0] == UpdateChoice::Later, "Later once (%d)", int(r.choices.size()));
    CHECK(!r.bubble.isVisible(), "bubble hidden");
    emit r.bubble.skipClicked();
    CHECK(r.choices.size() == 1, "stale ignored");
    r.brain.releaseGrab();
    CHECK(r.meows == 1, "one meow only");
}

static void testReplace()
{
    std::printf("[replace] second notify\n");
    Rig r;
    CHECK(r.startShown(QStringLiteral("2.0.0")), "first shown");
    r.notify(QStringLiteral("3.0.0"));
    CHECK(r.choices.size() == 1 && r.choices[0] == UpdateChoice::Later, "first got Later (%d)", int(r.choices.size()));
    CHECK(r.until([&] { return r.bubble.isVisible(); }, 120000), "second shown");
    CHECK(r.bubble.text().contains(QStringLiteral("3.0.0")), "second text");
    emit r.bubble.skipClicked();
    CHECK(r.choices.size() == 2 && r.choices[1] == UpdateChoice::Skip, "second Skip");
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    testChoice("open", [](Rig &r) { emit r.bubble.openClicked(); }, UpdateChoice::OpenPage);
    testChoice("later", [](Rig &r) { emit r.bubble.laterClicked(); }, UpdateChoice::Later);
    testChoice("skip", [](Rig &r) { emit r.bubble.skipClicked(); }, UpdateChoice::Skip);
    testChoice("timeout", [](Rig &r) { emit r.bubble.timedOut(); }, UpdateChoice::Later);
    testInterrupt();
    testReplace();
    return testing::testResult("test_notifier");
}
