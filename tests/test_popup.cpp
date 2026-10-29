// QuitPopup 크기 / 배치 + TrayController 클릭 규칙 테스트 (offscreen 플랫폼, 실제 화면에 팝업을 띄우지 않는다).
// 종료 버튼 크기 = Config::kQuitButtonW × kQuitButtonH (모든 배율에서 64×16 고정),
// 팝업 하단 = 종료 버튼 하단 = 바닥 y.
// 클릭 규칙: 버튼 클릭 → 항상 종료 (시도 아님), 고양이 몸 클릭 → 방해 중이면 재시도, Shift+고양이 클릭 → 종료.
#include "TestCheck.hpp"

#include "CatOverlay.hpp"
#include "CatStats.hpp"
#include "CatSprite.hpp"
#include "Config.hpp"
#include "QuitPopup.hpp"
#include "TrayController.hpp"

#include "AutoStart.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTemporaryDir>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

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
    std::vector<bool> attempted;   // quitAttempted(blocked) 기록
    QObject::connect(&tray, &TrayController::quitAttempted, [&](bool b) { attempted.push_back(b); });
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
    CHECK(attempted.size() == 1 && attempted[0], "hover emits quitAttempted(true) (%zu)", attempted.size());

    // 방해 중 버튼(보이는 부분) 클릭 → 재판정 없이 종료
    emit popup->quitClicked();
    CHECK(quit == 2, "button click while blocking quits (quit=%d)", quit);
    CHECK(rolls == 1 && react == 0 && released == 0, "button click is not an attempt (rolls=%d react=%d released=%d)", rolls,
          react, released);
    CHECK(tray.isBlocking(), "block state untouched by button click");
    CHECK(attempted.size() == 1, "button click does not emit quitAttempted (%zu)", attempted.size());

    // 방해 중 고양이 몸 클릭 → 시도: 유지
    tray.onCatClicked(Qt::NoModifier);
    CHECK(rolls == 2 && react == 1 && released == 0 && quit == 2, "cat click → blockReact (rolls=%d react=%d)", rolls, react);
    CHECK(tray.isBlocking(), "still blocking after react");
    CHECK(attempted.size() == 2 && attempted[1], "cat click emits quitAttempted(true) (%zu)", attempted.size());

    // Shift+고양이 클릭 → 즉시 종료, 시도 아님
    tray.onCatClicked(Qt::ShiftModifier);
    CHECK(quit == 3 && rolls == 2 && react == 1, "shift+cat click quits (quit=%d rolls=%d)", quit, rolls);
    CHECK(attempted.size() == 2, "shift+cat click does not emit quitAttempted (%zu)", attempted.size());

    // 방해 중 고양이 몸 클릭 → 시도: 실패 → 해제
    roll = 99;
    tray.onCatClicked(Qt::NoModifier);
    CHECK(rolls == 3 && released == 1 && react == 1 && quit == 3, "cat click → blockReleased (rolls=%d released=%d)", rolls,
          released);
    CHECK(!tray.isBlocking(), "block released");
    CHECK(attempted.size() == 3 && !attempted[2], "failed retry emits quitAttempted(false) (%zu)", attempted.size());

    // 해제 후: 고양이 클릭 무시, 버튼 클릭 종료 (시도 아님)
    tray.onCatClicked(Qt::NoModifier);
    CHECK(rolls == 3 && react == 1 && released == 1 && quit == 3, "cat click after release ignored");
    emit popup->quitClicked();
    CHECK(quit == 4 && rolls == 3, "button click after release quits (quit=%d)", quit);

    // 해제 후 Shift+고양이 클릭도 종료
    tray.onCatClicked(Qt::ShiftModifier);
    CHECK(quit == 5 && rolls == 3, "shift+cat click when idle quits (quit=%d)", quit);
    CHECK(attempted.size() == 3, "no more quitAttempted after idle clicks (%zu)", attempted.size());
}

// 자동 시작 체크박스: 표시 시 등록 상태 반영, 토글 → 등록/삭제, 실패 시 되돌림.
// Windows: 실제 Run 키 대신 HKCU\Software\CCatTest\PopupRun 임시 키를 쓴다.
// macOS: 실제 LaunchAgents 대신 임시 디렉터리의 PopupRun.plist 를 쓴다.
static void testAutoStartUi()
{
    std::printf("[3] autostart checkbox\n");
#ifdef Q_OS_WIN
    const QString testRoot = QStringLiteral("Software\\CCatTest");
    const QString key = testRoot + QStringLiteral("\\PopupRun");
    const QString badKey = testRoot + QLatin1Char('\\') + QString(300, QLatin1Char('a'));   // 키 이름 길이 제한 초과
    RegDeleteTreeW(HKEY_CURRENT_USER, testRoot.toStdWString().c_str());
#else
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir for plist");
    if (!tmp.isValid())
        return;
    const QString key = tmp.filePath(QStringLiteral("PopupRun.plist"));
    const QString badKey = tmp.filePath(QString(300, QLatin1Char('a')) + QStringLiteral("/PopupRun.plist"));   // 파일 이름 255바이트 초과 → mkpath 실패
#endif

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
    tray.setAutoStartKey(badKey);
    cb->click();
    CHECK(!cb->isChecked(), "checkbox reverted on failure");

    popup->hide();
#ifdef Q_OS_WIN
    RegDeleteTreeW(HKEY_CURRENT_USER, testRoot.toStdWString().c_str());
#endif
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

// CatOverlay::hitsOpaque (Shift 잡기 대기 판정, README 5.4)
static void testHitsOpaque()
{
    std::printf("[5] overlay hitsOpaque\n");
    CatOverlay o;
    o.setScale(3);
    CHECK(!o.hitsOpaque(QPoint(0, 0)), "no frame yet");

    // 32x24 스프라이트: (4,4)~(7,7) 만 불투명
    QImage img(CatSprite::Width, CatSprite::Height, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    for (int y = 4; y <= 7; ++y)
        for (int x = 4; x <= 7; ++x)
            img.setPixel(x, y, qRgba(255, 128, 0, 255));

    const QPoint anchor(500, 500);
    o.setFrame(img, anchor, CatGravity::Down);
    const QPoint topLeft = anchor - CatSprite::anchorIn(CatGravity::Down) * 3;
    CHECK(!o.hitsOpaque(topLeft + QPoint(14, 14)), "hidden window");   // 아직 show 전

    o.show();
    QCoreApplication::processEvents();
    CHECK(o.isVisible(), "visible");
    CHECK(o.hitsOpaque(topLeft + QPoint(4 * 3, 4 * 3)), "opaque first pixel");
    CHECK(o.hitsOpaque(topLeft + QPoint(7 * 3 + 2, 7 * 3 + 2)), "opaque last pixel (scaled)");
    CHECK(!o.hitsOpaque(topLeft + QPoint(4 * 3 - 1, 4 * 3)), "transparent pixel left of region");
    CHECK(!o.hitsOpaque(topLeft + QPoint(8 * 3, 5 * 3)), "transparent pixel right of region");
    CHECK(!o.hitsOpaque(topLeft + QPoint(20 * 3, 10 * 3)), "transparent far pixel");
    CHECK(!o.hitsOpaque(topLeft + QPoint(-1, 14)), "outside left");
    CHECK(!o.hitsOpaque(topLeft + QPoint(14, -1)), "outside top");
    CHECK(!o.hitsOpaque(topLeft + QPoint(CatSprite::Width * 3, 14)), "outside right");
    CHECK(!o.hitsOpaque(topLeft + QPoint(14, CatSprite::Height * 3)), "outside bottom");

    o.hide();
    CHECK(!o.hitsOpaque(topLeft + QPoint(14, 14)), "false when hidden again");
}

static QString statText(QuitPopup &p, const char *name)
{
    auto *l = p.findChild<QLabel *>(QString::fromLatin1(name));
    return l ? l->text() : QStringLiteral("<missing>");
}

// 통계 토글: 접힌 채 시작, 위로 펼침(하단 유지), 폭 / 종료 버튼 사각형 불변, 값 라벨 = CatStats 값 + 형식 함수
static void testStatsToggle()
{
    std::printf("[6] stats toggle\n");
    const int floorY = 1040;
    const QRect monitor(0, 0, 1920, 1080);
    const QRect tray(1800, 1000, 32, 32);

    CatStats stats;
    stats.addRunMs(3 * 3600 * 1000 + 2 * 60 * 1000 + 5000);
    stats.addRoamMs(90000);
    stats.addSleepMs(45000);
    stats.addPlayMs(12000);
    for (int i = 0; i < 1234; ++i)
        stats.addNudge();
    for (int i = 0; i < 15; ++i)
        stats.addQuitAttempt(i < 12);
    stats.addGrab();
    stats.addGrab();
    stats.addJump();
    stats.addFall();
    stats.addFall();
    stats.addDistanceM(12.34);

    QuitPopup p;
    auto *toggle = p.findChild<QPushButton *>(QStringLiteral("statsToggle"));
    auto *panel = p.findChild<QWidget *>(QStringLiteral("statsPanel"));
    CHECK(toggle != nullptr && panel != nullptr, "toggle and panel exist");
    if (!toggle || !panel)
        return;
    CHECK(toggle->focusPolicy() == Qt::NoFocus, "toggle is NoFocus");

    p.setStatsSource(&stats);
    p.setScale(3);
    p.showAt(tray, floorY, monitor);
    QCoreApplication::processEvents();
    CHECK(!p.statsExpanded() && !panel->isVisible(), "starts collapsed");
    CHECK(toggle->text() == QString::fromUtf8("▲"), "collapsed shows up arrow");
    const QRect r0 = p.desktopRect();
    const QRect b0 = p.quitButtonDesktopRect();
    CHECK(r0.bottom() + 1 == floorY, "collapsed bottom = floor");

    toggle->click();
    QCoreApplication::processEvents();
    const QRect r1 = p.desktopRect();
    const QRect b1 = p.quitButtonDesktopRect();
    CHECK(p.statsExpanded() && panel->isVisible(), "click expands");
    CHECK(toggle->text() == QString::fromUtf8("▼"), "expanded shows down arrow");
    CHECK(r1.bottom() + 1 == floorY, "expanded bottom = floor (%d)", r1.bottom() + 1);
    CHECK(r1.height() > r0.height(), "expanded grows (%d > %d)", r1.height(), r0.height());
    CHECK(r1.width() == r0.width() && r1.x() == r0.x(), "width and x unchanged (%d,%d)", r1.width(), r1.x());
    CHECK(b1 == b0, "quit button rect unchanged");
    CHECK(r1.top() >= monitor.top(), "expanded inside monitor");
    // 패널은 기존 내용(배율 줄/종료 버튼) 위, 토글은 패널 위
    int scalesTop = 1 << 30;
    for (auto *sb : p.findChildren<QPushButton *>(QStringLiteral("scale")))
        scalesTop = qMin(scalesTop, sb->mapToGlobal(QPoint(0, 0)).y());
    const QRect pr(panel->mapToGlobal(QPoint(0, 0)), panel->size());
    CHECK(pr.bottom() < scalesTop, "panel above existing content (%d < %d)", pr.bottom(), scalesTop);
    CHECK(toggle->mapToGlobal(QPoint(0, 0)).y() < pr.top(), "toggle above panel");
    CHECK(toggle->mapToGlobal(QPoint(0, 0)).y() <= r1.top() + 2, "toggle at very top");

    // 값 라벨 = 형식 함수 결과
    using S = CatStats;
    const CatStatsValues &t = stats.total();
    const CatStatsValues &s = stats.session();
    CHECK(statText(p, "stat_total_runMs") == S::formatDuration(t.runMs), "total runMs label");
    CHECK(statText(p, "stat_total_roamMs") == S::formatDuration(t.roamMs), "total roamMs label");
    CHECK(statText(p, "stat_total_sleepMs") == S::formatDuration(t.sleepMs), "total sleepMs label");
    CHECK(statText(p, "stat_total_playMs") == S::formatDuration(t.playMs), "total playMs label");
    CHECK(statText(p, "stat_total_nudges") == S::formatCount(t.nudges), "total nudges label");
    CHECK(statText(p, "stat_total_quit") ==
              QString::fromUtf8("%1 / %2 (%3)").arg(S::formatCount(12), S::formatCount(15), S::formatRate(12, 15)),
          "total quit label '%s'", qUtf8Printable(statText(p, "stat_total_quit")));
    CHECK(statText(p, "stat_total_grabs") == S::formatCount(t.grabs), "total grabs label");
    CHECK(statText(p, "stat_total_distanceM") == S::formatDistance(t.distanceM), "total distance label");
    CHECK(statText(p, "stat_total_jumpFall") == QString::fromUtf8("%1 / %2").arg(S::formatCount(1), S::formatCount(2)),
          "total jumpFall label");
    CHECK(statText(p, "stat_session_runMs") == S::formatDuration(s.runMs), "session runMs label");
    CHECK(statText(p, "stat_session_sleepMs") == S::formatDuration(s.sleepMs), "session sleepMs label");
    CHECK(statText(p, "stat_session_nudges") == S::formatCount(s.nudges), "session nudges label");
    CHECK(statText(p, "stat_session_quitBlocks") == S::formatCount(12), "session quitBlocks label");
    CHECK(statText(p, "stat_session_grabs") == S::formatCount(s.grabs), "session grabs label");
    CHECK(statText(p, "stat_session_distanceM") == S::formatDistance(s.distanceM), "session distance label");
    CHECK(p.findChild<QLabel *>(QStringLiteral("stat_since")) != nullptr, "since label exists");

    // 펼쳐진 동안 타이머로 갱신
    stats.addGrab();
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < Config::kStatsRefreshMs + 600)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    CHECK(statText(p, "stat_total_grabs") == S::formatCount(3), "refresh timer updates label");
    CHECK(p.desktopRect().width() == r0.width(), "width stable after refresh");

    // 접으면 원래 크기
    toggle->click();
    QCoreApplication::processEvents();
    CHECK(!p.statsExpanded() && !panel->isVisible(), "click collapses");
    CHECK(p.desktopRect() == r0, "collapsed back to original rect");
    CHECK(p.quitButtonDesktopRect() == b0, "quit button rect same after collapse");

    // API 로 펼침 / showAt 은 항상 접힌 채 시작
    p.setStatsExpanded(true);
    CHECK(p.statsExpanded() && p.desktopRect().width() == r0.width(), "setStatsExpanded(true)");
    p.hide();
    p.showAt(tray, floorY, monitor);
    CHECK(!p.statsExpanded() && p.desktopRect() == r0, "showAt restarts collapsed");

    // 폭 고정: 표시 중에는 값이 훨씬 길어져도(토글/1초 갱신 포함) 폭, x, 종료 버튼 사각형이 그대로
    p.setStatsExpanded(true);
    const QRect rw = p.desktopRect();
    const QRect bw = p.quitButtonDesktopRect();
    for (int i = 0; i < 5; ++i)
        stats.addRunMs(86400000LL * 4000);
    stats.addDistanceM(123456789.0);
    for (int i = 0; i < 100; ++i)
        stats.addJump();
    p.refreshStats();
    QCoreApplication::processEvents();
    CHECK(p.desktopRect().x() == rw.x() && p.desktopRect().width() == rw.width(), "width/x locked after long values");
    CHECK(p.quitButtonDesktopRect() == bw, "quit button rect locked after long values");
    p.setStatsExpanded(false);
    p.setStatsExpanded(true);
    CHECK(p.desktopRect().width() == rw.width() && p.quitButtonDesktopRect() == bw, "width locked across toggle");
    p.hide();
    p.showAt(tray, floorY, monitor);
    CHECK(p.desktopRect().width() >= rw.width(), "showAt recomputes width (may grow)");
    const int shownW = p.desktopRect().width();   // 표시 중 폭 (폰트에 따라 r0 보다 넓을 수 있다)

    // 원본 없음 → "-"
    p.setStatsSource(nullptr);
    p.setStatsExpanded(true);
    CHECK(statText(p, "stat_total_runMs") == QStringLiteral("-"), "null source shows dash");
    CHECK(statText(p, "stat_session_grabs") == QStringLiteral("-"), "null source session dash");
    CHECK(p.desktopRect().width() == shownW, "width locked with null source (%d == %d)", p.desktopRect().width(), shownW);
    p.hide();
}

int main(int argc, char **argv)
{
    qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
    QApplication app(argc, argv);

    testPopupLayout();
    testClickRules();
    testAutoStartUi();
    testVolumeSlider();
    testHitsOpaque();
    testStatsToggle();
    return testing::testResult("test_popup");
}
