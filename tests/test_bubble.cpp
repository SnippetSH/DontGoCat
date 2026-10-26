// SpeechBubble 테스트 (README 5.15): 크기 / 클릭 영역 / 클릭 판정 / 배치 / 시간 초과 (offscreen 플랫폼)
#include "TestCheck.hpp"

#include "CatSprite.hpp"
#include "Config.hpp"
#include "SpeechBubble.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QMouseEvent>

#include <cstdio>
#include <cstdlib>

namespace {

const QRect kMonitor(0, 0, 1920, 1080);
const QString kText = QString::fromUtf8("새 버전 1.4.0 나왔다냥!");

void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

void sendMouse(QWidget *w, QEvent::Type type, const QPoint &pos)
{
    QMouseEvent e(type, QPointF(pos), QPointF(w->mapToGlobal(pos)), Qt::LeftButton,
                  type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w, &e);
}

void click(QWidget *w, const QPoint &pressAt, const QPoint &releaseAt)
{
    sendMouse(w, QEvent::MouseButtonPress, pressAt);
    sendMouse(w, QEvent::MouseButtonRelease, releaseAt);
}

struct Counts
{
    int open = 0, later = 0, skip = 0, timed = 0;
};

void connectCounts(SpeechBubble &b, Counts &c)
{
    QObject::connect(&b, &SpeechBubble::openClicked, [&]() { ++c.open; });
    QObject::connect(&b, &SpeechBubble::laterClicked, [&]() { ++c.later; });
    QObject::connect(&b, &SpeechBubble::skipClicked, [&]() { ++c.skip; });
    QObject::connect(&b, &SpeechBubble::timedOut, [&]() { ++c.timed; });
}

QRect catAt(int x, int y)
{
    return QRect(x, y, CatSprite::Width * 3, CatSprite::Height * 3);
}

void testSize()
{
    std::printf("[1] size\n");
    SpeechBubble b;
    b.setText(kText);
    const QRect cat = catAt(800, 600);
    QSize first;
    int firstTextWidthBody = 0;
    for (int s = Config::kMinScale; s <= Config::kMaxScale; ++s) {
        b.setScale(s);
        b.showFor(cat, CatGravity::Down, kMonitor);
        const QSize sz = b.size();
        std::printf("scale %d size %dx%d body %dx%d\n", s, sz.width(), sz.height(), b.bodyRect().width(),
                    b.bodyRect().height());
        CHECK(b.scale() == s, "scale stored");
        CHECK(b.bodyRect().width() > 0 && b.bodyRect().height() > 0, "body size positive");
        CHECK(b.rect().contains(b.bodyRect()), "body inside widget");
        if (s == Config::kMinScale) {
            first = sz;
            firstTextWidthBody = b.bodyRect().width();
        } else {
            // 테두리 / 꼬리만 배율을 따른다: 높이는 커지고, 폭 증가는 테두리 증가분(양쪽)뿐
            CHECK(sz.height() > first.height(), "scale %d taller (%d > %d)", s, sz.height(), first.height());
            const int u = Config::kBubbleBorderSpritePx;
            CHECK(b.bodyRect().width() == firstTextWidthBody + 2 * u * (s - Config::kMinScale),
                  "scale %d body width grows only by border (%d)", s, b.bodyRect().width());
            CHECK(sz.height() - b.bodyRect().height()
                      == Config::kBubbleTailSpritePx * s,
                  "scale %d tail length = %d*scale", s, Config::kBubbleTailSpritePx);
        }
    }
    b.dismiss();

    // 문구가 길면 커진다
    b.setScale(3);
    b.setText(QStringLiteral("a"));
    const int narrow = b.bodyRect().width();
    b.setText(kText);
    CHECK(b.bodyRect().width() > narrow, "longer text makes it wider (%d > %d)", b.bodyRect().width(), narrow);
    CHECK(b.text() == kText, "text() returns text");
}

void testRects()
{
    std::printf("[2] rects\n");
    SpeechBubble b;
    b.setText(kText);
    for (int s = Config::kMinScale; s <= Config::kMaxScale; ++s) {
        b.setScale(s);
        for (CatGravity g : {CatGravity::Down, CatGravity::Left, CatGravity::Right}) {
            b.showFor(catAt(800, 600), g, kMonitor);
            const QRect body = b.bodyRect(), close = b.closeRect(), skip = b.skipRect();
            CHECK(!close.isEmpty() && !skip.isEmpty(), "rects not empty");
            CHECK(body.contains(close), "scale %d g %d close inside body", s, int(g));
            CHECK(body.contains(skip), "scale %d g %d skip inside body", s, int(g));
            CHECK(!close.intersects(skip), "scale %d g %d close/skip disjoint", s, int(g));
            CHECK(close.right() > body.center().x() && close.top() < body.center().y(), "close at top right");
            CHECK(skip.top() > close.bottom(), "skip below close row");
        }
    }
    b.dismiss();
}

void testClicks()
{
    std::printf("[3] clicks\n");
    SpeechBubble b;
    b.setText(kText);
    b.setScale(3);
    Counts c;
    connectCounts(b, c);
    const QRect cat = catAt(800, 600);

    b.showFor(cat, CatGravity::Down, kMonitor);
    CHECK(b.isVisible(), "visible");
    click(&b, b.closeRect().center(), b.closeRect().center());
    CHECK(c.later == 1 && c.skip == 0 && c.open == 0 && !b.isVisible(), "close → later + hidden (%d)", c.later);

    b.showFor(cat, CatGravity::Down, kMonitor);
    click(&b, b.skipRect().center(), b.skipRect().center());
    CHECK(c.skip == 1 && c.later == 1 && c.open == 0 && !b.isVisible(), "skip → skipClicked + hidden");

    b.showFor(cat, CatGravity::Down, kMonitor);
    const QPoint inBody = b.bodyRect().topLeft() + QPoint(b.bodyRect().width() / 2, b.skipRect().top() - b.bodyRect().top() - 2);
    click(&b, inBody, inBody);
    CHECK(c.open == 1 && c.later == 1 && c.skip == 1 && !b.isVisible(), "body → openClicked + hidden");

    b.showFor(cat, CatGravity::Down, kMonitor);
    click(&b, b.bodyRect().center(), b.bodyRect().center());
    CHECK(c.open == 2 && !b.isVisible(), "body center → open");

    // 누른 곳과 뗀 곳이 다르면 아무 일도 없다
    b.showFor(cat, CatGravity::Down, kMonitor);
    click(&b, b.closeRect().center(), b.skipRect().center());
    click(&b, b.skipRect().center(), b.bodyRect().topLeft() + QPoint(3, 3));
    click(&b, b.closeRect().center(), b.bodyRect().topLeft() + QPoint(b.bodyRect().width() / 2, b.skipRect().top() - b.bodyRect().top() - 2));
    CHECK(c.open == 2 && c.later == 1 && c.skip == 1 && b.isVisible(), "press/release mismatch does nothing");

    // 꼬리 밖 투명 영역 (몸통 모서리 한 칸) 은 말풍선이 아니다
    const int before = c.open;
    click(&b, QPoint(0, 0), QPoint(0, 0));
    CHECK(c.open == before && b.isVisible(), "stepped corner is transparent / ignored");
    b.dismiss();
    CHECK(c.timed == 0, "no timedOut");
}

void testPlacement()
{
    std::printf("[4] placement\n");
    SpeechBubble b;
    b.setText(kText);
    b.setScale(3);
    const int gap = Config::kBubbleGapSpritePx * 3;

    // 바닥/지붕: 고양이 위 가운데
    QRect cat = catAt(900, 700);
    b.showFor(cat, CatGravity::Down, kMonitor);
    QRect w = b.geometry();
    CHECK(w.bottom() + 1 == cat.top() - gap, "Down: tail tip %d == cat.top - gap %d", w.bottom() + 1, cat.top() - gap);
    CHECK(std::abs(w.center().x() - cat.center().x()) <= 1, "Down: centered (%d vs %d)", w.center().x(), cat.center().x());
    CHECK(kMonitor.contains(w), "Down: inside monitor");
    CHECK(b.placedFor() == CatGravity::Down, "placedFor Down");
    // 꼬리는 고양이 중심선 쪽
    const int tailCenterX = w.x() + b.bodyRect().width() / 2;
    CHECK(std::abs(tailCenterX - cat.center().x()) <= 3, "Down: tail aims at cat");

    // 모니터 왼쪽 / 오른쪽 가장자리 → clamp
    cat = catAt(-20, 700);
    b.showFor(cat, CatGravity::Down, kMonitor);
    w = b.geometry();
    CHECK(w.left() == kMonitor.left() && kMonitor.contains(w), "left edge clamped (%d)", w.left());
    CHECK(w.x() + b.rect().width() > 0, "still has width");
    {
        // 꼬리가 몸통 바깥 / 모서리로 나가지 않았는지: 꼬리 중심이 고양이를 향하되 몸통 변 안
        const QRect body = b.bodyRect();
        CHECK(body.left() >= 0 && body.right() <= b.width() - 1, "left clamp: body in window");
    }
    cat = catAt(kMonitor.right() - 30, 700);
    b.showFor(cat, CatGravity::Down, kMonitor);
    w = b.geometry();
    CHECK(w.right() == kMonitor.right() && kMonitor.contains(w), "right edge clamped (%d)", w.right());

    // 모니터 위쪽 → 고양이 아래로 뒤집힘
    cat = catAt(900, 10);
    b.showFor(cat, CatGravity::Down, kMonitor);
    w = b.geometry();
    CHECK(w.top() == cat.bottom() + 1 + gap, "flip: below cat (top %d)", w.top());
    CHECK(kMonitor.contains(w), "flip: inside monitor");
    CHECK(b.bodyRect().top() > b.rect().top(), "flip: tail on top");

    // 벽: Left = 벽이 고양이 왼쪽 → 말풍선은 오른쪽, Right = 반대
    cat = catAt(900, 400);
    b.showFor(cat, CatGravity::Left, kMonitor);
    w = b.geometry();
    CHECK(w.left() >= cat.right(), "Left wall: bubble right of cat (%d >= %d)", w.left(), cat.right());
    CHECK(w.left() == cat.right() + 1 + gap, "Left wall: tail tip at gap");
    CHECK(std::abs(w.center().y() - cat.center().y()) <= 1, "Left wall: vertically centered");
    CHECK(kMonitor.contains(w), "Left wall: inside monitor");
    CHECK(b.placedFor() == CatGravity::Left, "placedFor Left");
    CHECK(b.bodyRect().left() > 0, "Left wall: tail on the left");

    b.showFor(cat, CatGravity::Right, kMonitor);
    w = b.geometry();
    CHECK(w.right() <= cat.left(), "Right wall: bubble left of cat (%d <= %d)", w.right(), cat.left());
    CHECK(w.right() + 1 == cat.left() - gap, "Right wall: tail tip at gap");
    CHECK(kMonitor.contains(w), "Right wall: inside monitor");
    CHECK(b.placedFor() == CatGravity::Right, "placedFor Right");
    CHECK(b.bodyRect().right() < b.width() - 1, "Right wall: tail on the right");

    // 벽 + 모니터 위/아래 가장자리 → 세로 clamp, 꼬리는 몸통 변 안
    cat = catAt(900, 0);
    b.showFor(cat, CatGravity::Left, kMonitor);
    w = b.geometry();
    CHECK(kMonitor.contains(w), "wall top edge clamped");
    cat = catAt(900, kMonitor.bottom() - 40);
    b.showFor(cat, CatGravity::Left, kMonitor);
    w = b.geometry();
    CHECK(kMonitor.contains(w), "wall bottom edge clamped");

    // follow: 보이는 동안 위치 갱신, 안 보이면 보이게 하지 않는다
    cat = catAt(900, 700);
    b.showFor(cat, CatGravity::Down, kMonitor);
    const QPoint p0 = b.pos();
    b.follow(cat.translated(100, 0), CatGravity::Down, kMonitor);
    CHECK(b.pos() == p0 + QPoint(100, 0), "follow moves with cat");
    b.dismiss();
    b.follow(cat, CatGravity::Left, kMonitor);
    CHECK(!b.isVisible(), "follow does not show");
    CHECK(b.placedFor() == CatGravity::Left, "placedFor updates on follow");

    // setScale 중 재배치
    b.showFor(cat, CatGravity::Down, kMonitor);
    b.setScale(4);
    CHECK(b.geometry().bottom() + 1 == cat.top() - Config::kBubbleGapSpritePx * 4, "setScale re-places while visible");
    b.dismiss();
}

void testTimeout()
{
    std::printf("[5] timeout\n");
    SpeechBubble b;
    b.setText(kText);
    Counts c;
    connectCounts(b, c);
    const QRect cat = catAt(900, 700);

    b.setTimeoutMs(80);
    b.showFor(cat, CatGravity::Down, kMonitor);
    CHECK(b.isVisible(), "visible");
    pump(400);
    CHECK(c.timed == 1 && !b.isVisible(), "timedOut once and hidden (%d)", c.timed);
    CHECK(c.open == 0 && c.later == 0 && c.skip == 0, "no click signals on timeout");

    // dismiss: 시그널 없음
    b.showFor(cat, CatGravity::Down, kMonitor);
    b.dismiss();
    CHECK(!b.isVisible(), "dismiss hides");
    pump(300);
    CHECK(c.timed == 1, "dismiss stops timer, no signal (%d)", c.timed);

    // 보이는 중 showFor → 타이머 재시작 (한 번만 울린다)
    b.showFor(cat, CatGravity::Down, kMonitor);
    pump(50);
    b.showFor(cat, CatGravity::Down, kMonitor);
    pump(50);
    CHECK(b.isVisible() && c.timed == 1, "re-showFor restarts timer");
    pump(300);
    CHECK(c.timed == 2 && !b.isVisible(), "fires once after restart (%d)", c.timed);

    // hover 중 정지
    b.setTimeoutMs(150);
    b.showFor(cat, CatGravity::Down, kMonitor);
    pump(50);
    QEnterEvent enter(QPointF(5, 5), QPointF(5, 5), QPointF(b.pos() + QPoint(5, 5)));
    QApplication::sendEvent(&b, &enter);
    pump(400);
    CHECK(b.isVisible() && c.timed == 2, "paused while hovered");
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&b, &leave);
    pump(40);
    CHECK(b.isVisible() && c.timed == 2, "remaining time kept after leave (not restarted full)");
    pump(300);
    CHECK(!b.isVisible() && c.timed == 3, "fires after leave (%d)", c.timed);

    // 클릭으로 닫으면 타이머도 정지
    b.setTimeoutMs(60);
    b.showFor(cat, CatGravity::Down, kMonitor);
    click(&b, b.bodyRect().center(), b.bodyRect().center());
    pump(250);
    CHECK(c.timed == 3 && c.open == 1, "click cancels timer");
}

} // namespace

int main(int argc, char *argv[])
{
    qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
    QApplication app(argc, argv);
    SpeechBubble first;
    CHECK(!first.isVisible(), "hidden at start");
    testSize();
    testRects();
    testClicks();
    testPlacement();
    testTimeout();
    return testing::testResult("test_bubble");
}
