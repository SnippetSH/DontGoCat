// QuitPopup 크기 / 배치 + TrayController 클릭 규칙 테스트 (offscreen 플랫폼, 실제 화면에 팝업을 띄우지 않는다).
// 종료 버튼 크기 = Config::kQuitButtonW × kQuitButtonH (모든 배율에서 64×16 고정),
// 팝업 하단 = 종료 버튼 하단 = 바닥 y.
// 클릭 규칙: 버튼 클릭 → 항상 종료 (시도 아님), 고양이 몸 클릭 → 방해 중이면 재시도, Shift+고양이 클릭 → 종료.
#include "TestCheck.hpp"

#include "Config.hpp"
#include "QuitPopup.hpp"
#include "TrayController.hpp"

#include "AutoStart.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QCheckBox>
#include <QPushButton>
#include <QSlider>
#include <QThread>

#include <windows.h>

#include <cstdio>
#include <vector>

static void testPopupLayout()
{
    std::printf("[1] popup layout\n");
    const int floorY = 1040;
    const QRect monitor(0, 0, 1920, 1080);
    const QRect tray(1800, 1000, 32, 32);   // 모니터 오른쪽 아래의 트레이 아이콘

    QuitPopup p;
    QSize firstPopupSize;
    for (int s = Config::kMinScale; s <= Config::kMaxScale; ++s) {
        p.setScale(s);
        p.showAt(tray, floorY, monitor);
        const QRect b = p.quitButtonDesktopRect();
        const QRect r = p.desktopRect();
        std::printf("scale %d button %dx%d bottom+1=%d popup %d,%d %dx%d bottom+1=%d\n", s, b.width(), b.height(),
                    b.bottom() + 1, r.x(), r.y(), r.width(), r.height(), r.bottom() + 1);
        CHECK(b.size() == QSize(64, 16), "scale %d button size %dx%d", s, b.width(), b.height());
        CHECK(b.size() == QSize(Config::kQuitButtonW, Config::kQuitButtonH), "scale %d button size = Config", s);
        CHECK(b.bottom() + 1 == floorY, "scale %d button bottom %d", s, b.bottom() + 1);
        CHECK(r.bottom() + 1 == floorY, "scale %d popup bottom %d", s, r.bottom() + 1);
        CHECK(r.right() < monitor.right() + 1 && r.left() >= monitor.left(), "scale %d popup inside monitor", s);
        CHECK(r.contains(b), "scale %d button inside popup", s);
        // 자동 시작 체크박스: 배율 줄 아래, 종료 버튼 위
        auto *cb = p.findChild<QCheckBox *>(QStringLiteral("autostart"));
        CHECK(cb != nullptr, "scale %d autostart checkbox exists", s);
        if (cb) {
            const QRect c(cb->mapToGlobal(QPoint(0, 0)), cb->size());
            int scalesBottom = -1;
            for (auto *sb : p.findChildren<QPushButton *>(QStringLiteral("scale")))
                scalesBottom = qMax(scalesBottom, QRect(sb->mapToGlobal(QPoint(0, 0)), sb->size()).bottom());
            CHECK(cb->text() == QString::fromUtf8("윈도우 시작 시 실행"), "scale %d checkbox text", s);
            CHECK(c.top() > scalesBottom, "scale %d checkbox below scale row (%d > %d)", s, c.top(), scalesBottom);
            CHECK(c.bottom() < b.top(), "scale %d checkbox above quit button", s);
            CHECK(r.contains(c), "scale %d checkbox inside popup", s);
        }
        // 음량 슬라이더: 종료 버튼 위, 팝업 안
        auto *vol = p.findChild<QSlider *>(QStringLiteral("volume"));
        CHECK(vol != nullptr, "scale %d volume slider exists", s);
        if (vol) {
            const QRect v(vol->mapToGlobal(QPoint(0, 0)), vol->size());
            CHECK(v.bottom() < b.top(), "scale %d slider above quit button", s);
            CHECK(r.contains(v), "scale %d slider inside popup", s);
        }
        if (s == Config::kMinScale)
            firstPopupSize = r.size();
        else
            CHECK(r.size() == firstPopupSize, "scale %d popup size unchanged (%dx%d)", s, r.width(), r.height());
    }
}

// 종료 버튼 hover / 클릭, 고양이 클릭을 직접 흘려 보내 TrayController 판정을 검사한다.
static void testClickRules()
{
    std::printf("[2] click rules\n");

    int roll = 0;    // 0 → 방해(0 < 확률), 99 → 방해 실패
    int rolls = 0;   // QuitGuard::attempt 호출 횟수
    TrayController tray(nullptr, [&]() {
        ++rolls;
        return roll;
    });

    QuitPopup *popup = nullptr;
    for (QWidget *w : QApplication::topLevelWidgets()) {
        if (auto *q = qobject_cast<QuitPopup *>(w))
            popup = q;
    }
    CHECK(popup != nullptr, "tray owns a QuitPopup");
    if (!popup)
        return;

    int requested = 0, react = 0, released = 0, quit = 0;
    QObject::connect(&tray, &TrayController::blockRequested, [&](QRect) { ++requested; });
    QObject::connect(&tray, &TrayController::blockReact, [&]() { ++react; });
    QObject::connect(&tray, &TrayController::blockReleased, [&]() { ++released; });
    QObject::connect(&tray, &TrayController::quitRequested, [&]() { ++quit; });

    // 방해 중 아닐 때: 고양이 클릭은 무시, 버튼 클릭은 종료
    tray.onCatClicked(Qt::NoModifier);
    CHECK(rolls == 0 && react == 0 && released == 0 && quit == 0, "cat click while idle ignored");
    emit popup->quitClicked();
    CHECK(quit == 1 && rolls == 0, "button click while idle quits (quit=%d rolls=%d)", quit, rolls);

    // hover 진입 판정은 그대로: 시도 1회 → 방해
    roll = 0;
    emit popup->quitHovered();
    CHECK(rolls == 1 && requested == 1 && tray.isBlocking(), "hover → blockRequested (rolls=%d)", rolls);

    // 방해 중 버튼(보이는 부분) 클릭 → 재판정 없이 종료
    emit popup->quitClicked();
    CHECK(quit == 2, "button click while blocking quits (quit=%d)", quit);
    CHECK(rolls == 1 && react == 0 && released == 0, "button click is not an attempt (rolls=%d react=%d released=%d)", rolls,
          react, released);
    CHECK(tray.isBlocking(), "block state untouched by button click");

    // 방해 중 고양이 몸 클릭 → 시도: 유지
    tray.onCatClicked(Qt::NoModifier);
    CHECK(rolls == 2 && react == 1 && released == 0 && quit == 2, "cat click → blockReact (rolls=%d react=%d)", rolls, react);
    CHECK(tray.isBlocking(), "still blocking after react");

    // Shift+고양이 클릭 → 즉시 종료, 시도 아님
    tray.onCatClicked(Qt::ShiftModifier);
    CHECK(quit == 3 && rolls == 2 && react == 1, "shift+cat click quits (quit=%d rolls=%d)", quit, rolls);

    // 방해 중 고양이 몸 클릭 → 시도: 실패 → 해제
    roll = 99;
    tray.onCatClicked(Qt::NoModifier);
    CHECK(rolls == 3 && released == 1 && react == 1 && quit == 3, "cat click → blockReleased (rolls=%d released=%d)", rolls,
          released);
    CHECK(!tray.isBlocking(), "block released");

    // 해제 후: 고양이 클릭 무시, 버튼 클릭 종료 (시도 아님)
    tray.onCatClicked(Qt::NoModifier);
    CHECK(rolls == 3 && react == 1 && released == 1 && quit == 3, "cat click after release ignored");
    emit popup->quitClicked();
    CHECK(quit == 4 && rolls == 3, "button click after release quits (quit=%d)", quit);

    // 해제 후 Shift+고양이 클릭도 종료
    tray.onCatClicked(Qt::ShiftModifier);
    CHECK(quit == 5 && rolls == 3, "shift+cat click when idle quits (quit=%d)", quit);
}

// 자동 시작 체크박스: 표시 시 레지스트리 상태 반영, 토글 → 등록/삭제, 실패 시 되돌림.
// 실제 Run 키 대신 HKCU\Software\CCatTest\PopupRun 임시 키를 쓴다.
static void testAutoStartUi()
{
    std::printf("[3] autostart checkbox\n");
    const QString testRoot = QStringLiteral("Software\\CCatTest");
    const QString key = testRoot + QStringLiteral("\\PopupRun");
    RegDeleteTreeW(HKEY_CURRENT_USER, testRoot.toStdWString().c_str());

    TrayController tray(nullptr, []() { return 99; });
    tray.setAutoStartKey(key);
    QuitPopup *popup = nullptr;
    for (QWidget *w : QApplication::topLevelWidgets()) {
        if (auto *q = qobject_cast<QuitPopup *>(w))
            popup = q;
    }
    CHECK(popup != nullptr, "tray owns a QuitPopup");
    if (!popup)
        return;
    auto *cb = popup->findChild<QCheckBox *>(QStringLiteral("autostart"));
    CHECK(cb != nullptr, "checkbox exists");
    if (!cb)
        return;

    tray.togglePopup();   // 열기: 값 없음 → 체크 해제
    CHECK(tray.isPopupOpen(), "popup opened");
    CHECK(!cb->isChecked(), "unchecked when no value");
    CHECK(popup->quitButtonDesktopRect().size() == QSize(Config::kQuitButtonW, Config::kQuitButtonH), "quit button size with checkbox");

    cb->click();          // 켜기
    CHECK(cb->isChecked() && AutoStart::isEnabled(key), "click enables (checked=%d)", cb->isChecked());
    CHECK(AutoStart::registeredCommand(key) == AutoStart::currentCommand(), "registered current exe");

    popup->hide();
    QThread::msleep(Config::kReopenGuardMs + 50);   // 재오픈 방지 구간 통과
    tray.togglePopup();   // 다시 열기: 값 있음 → 체크
    CHECK(cb->isChecked(), "checked when value exists");
    // 외부에서 값을 지우면 다음 표시 때 해제로 반영
    AutoStart::setEnabled(false, key);
    popup->hide();
    QThread::msleep(Config::kReopenGuardMs + 50);   // 재오픈 방지 구간 통과
    tray.togglePopup();
    CHECK(!cb->isChecked(), "unchecked after external removal");

    cb->click();          // 켜기 → 끄기
    cb->click();
    CHECK(!cb->isChecked() && !AutoStart::isEnabled(key), "second click disables");

    // 쓰기 실패 → 체크 되돌림
    tray.setAutoStartKey(testRoot + QLatin1Char('\\') + QString(300, QLatin1Char('a')));
    cb->click();
    CHECK(!cb->isChecked(), "checkbox reverted on failure");

    popup->hide();
    RegDeleteTreeW(HKEY_CURRENT_USER, testRoot.toStdWString().c_str());
}

// 음량 슬라이더: 트랙 클릭(값 이동 후 놓기), 끌기, 휠 연속 변경이 각각 volumeSelected 한 번으로 묶이는지
static void testVolumeSlider()
{
    std::printf("[4] volume slider\n");
    QuitPopup p;
    auto *vol = p.findChild<QSlider *>(QStringLiteral("volume"));
    CHECK(vol != nullptr, "slider exists");
    if (!vol)
        return;
    std::vector<int> emitted;
    QObject::connect(&p, &QuitPopup::volumeSelected, [&](int v) { emitted.push_back(v); });
    auto settle = [] {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 400)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    };

    p.setVolume(20);
    settle();
    CHECK(emitted.empty(), "setVolume does not emit (%zu)", emitted.size());

    // 트랙 클릭: 핸들이 먼저 이동(valueChanged, 눌림 전) → 눌림 → 놓기
    vol->setValue(70);
    vol->setSliderDown(true);
    vol->setSliderDown(false);
    settle();
    CHECK(emitted.size() == 1 && emitted.back() == 70, "track click emits once (%zu)", emitted.size());

    // 끌기: 눌린 채 여러 값 → 지연 시간이 지나도 놓기 전에는 알리지 않음 → 놓으면 한 번
    vol->setSliderDown(true);
    for (int v = 72; v <= 90; v += 2)
        vol->setValue(v);
    settle();
    CHECK(emitted.size() == 1, "no emit while dragging (%zu)", emitted.size());
    vol->setSliderDown(false);
    settle();
    CHECK(emitted.size() == 2 && emitted.back() == 90, "drag release emits once (%zu)", emitted.size());

    // 휠 연속 변경 → 한 번
    for (int v = 85; v >= 60; v -= 5)
        vol->setValue(v);
    settle();
    CHECK(emitted.size() == 3 && emitted.back() == 60, "wheel burst emits once (%zu)", emitted.size());

    // 같은 값으로 놓기 → 알리지 않음
    vol->setSliderDown(true);
    vol->setSliderDown(false);
    settle();
    CHECK(emitted.size() == 3, "unchanged release ignored (%zu)", emitted.size());
}

int main(int argc, char **argv)
{
    qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
    QApplication app(argc, argv);

    testPopupLayout();
    testClickRules();
    testAutoStartUi();
    testVolumeSlider();
    return testing::testResult("test_popup");
}
