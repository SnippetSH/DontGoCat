#include "CatApp.hpp"

#include "Config.hpp"

#include <QCoreApplication>
#include <QCursor>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QScreen>
#include <QSettings>
#include <QUrl>
#include <QWidget>

#include <algorithm>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

Q_LOGGING_CATEGORY(lcStats, "ccat.stats")

namespace {

constexpr int kSpawnSearchStepPx = 64;   // 주 모니터 중앙에서 좌우로 바닥이 뚫린 지점을 찾는 간격

// 로그용 활동 이름
const char *activityName(CatBrain::Activity a)
{
    switch (a) {
    case CatBrain::Activity::Roaming: return "Roaming";
    case CatBrain::Activity::Sleeping: return "Sleeping";
    case CatBrain::Activity::Playing: return "Playing";
    case CatBrain::Activity::Other: break;
    }
    return "Other";
}

} // namespace

NativeMsgAction nativeMsgAction(unsigned message, quint64 wParam)
{
    constexpr unsigned kWmClose = 0x0010;
    constexpr unsigned kWmQueryEndSession = 0x0011;
    constexpr unsigned kWmEndSession = 0x0016;
    constexpr unsigned kWmSysCommand = 0x0112;
    constexpr quint64 kScClose = 0xF060;
    constexpr quint64 kScMask = 0xFFF0;   // 하위 4비트는 시스템이 쓴다
    switch (message) {
    case kWmSysCommand: return (wParam & kScMask) == kScClose ? NativeMsgAction::HideSwallow : NativeMsgAction::None;
    case kWmClose: return NativeMsgAction::QuitSwallow;
    case kWmQueryEndSession: return NativeMsgAction::SaveOnly;
    case kWmEndSession: return wParam != 0 ? NativeMsgAction::SaveAndQuit : NativeMsgAction::None;
    default: return NativeMsgAction::None;
    }
}

// 정상 종료 요청(taskkill /IM, /F 없음) / 세션 종료를 받아 통계를 저장하고 종료한다 (README 5.14).
// 이 프로세스의 창 메시지만 들어오므로 대상 창을 따로 거르지 않는다.
class CatApp::NativeFilter : public QAbstractNativeEventFilter
{
public:
    explicit NativeFilter(CatApp &app) : m_app(app) {}

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override
    {
#ifdef Q_OS_WIN
        if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")
            return false;
        const MSG *msg = static_cast<const MSG *>(message);
        // 디스패처 메시지(스레드 메시지)는 창이 없다: 창 메시지만 본다
        if (!msg || !msg->hwnd)
            return false;
        switch (nativeMsgAction(msg->message, static_cast<quint64>(msg->wParam))) {
        // result 는 null 일 수 있다: Post 된 메시지(taskkill 의 WM_CLOSE)는 Qt 이벤트 디스패처가 result 없이 필터를 부른다
        case NativeMsgAction::QuitSwallow:
            if (result)
                *result = 0;
            QCoreApplication::quit();
            return true;
        case NativeMsgAction::HideSwallow:   // Alt+F4 는 종료가 아니라 기존처럼 그 창만 숨긴다 (고양이의 종료 방해, R4)
            if (QWidget *w = QWidget::find(reinterpret_cast<WId>(msg->hwnd)))
                w->hide();
            if (result)
                *result = 0;
            return true;
        case NativeMsgAction::SaveOnly:
            m_app.saveStats();
            return false;
        case NativeMsgAction::SaveAndQuit:
            m_app.saveStats();
            QCoreApplication::quit();
            return false;
        case NativeMsgAction::None: break;
        }
#else
        Q_UNUSED(eventType)
        Q_UNUSED(message)
        Q_UNUSED(result)
#endif
        return false;
    }

private:
    CatApp &m_app;
};

CatApp::~CatApp()
{
    if (m_nativeFilter)
        qApp->removeNativeEventFilter(m_nativeFilter.get());
}

CatApp::CatApp(std::optional<int> scaleOverride, QObject *parent)
    : QObject(parent)
    , m_brain(m_body, m_planner, m_mouse)
{
    // 배율: --scale > QSettings > 기본값
    int scale = Config::kDefaultScale;
    if (scaleOverride) {
        scale = *scaleOverride;
    } else {
        bool ok = false;
        const int saved = QSettings().value(Config::kSettingsScaleKey).toInt(&ok);
        if (ok)
            scale = saved;
    }
    setScale(scale);

    // 소리: QSettings (기본 켜짐)
    const bool sound = QSettings().value(Config::kSettingsSoundKey, true).toBool();
    m_voice.setEnabled(sound);
    m_tray.setSoundEnabled(sound);
    const int volume = std::clamp(QSettings().value(Config::kSettingsVolumeKey, Config::kMeowVolume).toInt(), 0, 100);
    m_voice.setVolume(volume);
    m_tray.setVolume(volume);

    m_tickTimer.setTimerType(Qt::PreciseTimer);
    m_tickTimer.setInterval(Config::kTickMs);
    connect(&m_tickTimer, &QTimer::timeout, this, &CatApp::tick);

    m_followTimer.setTimerType(Qt::PreciseTimer);
    m_followTimer.setInterval(Config::kFollowMs);
    connect(&m_followTimer, &QTimer::timeout, this, &CatApp::followWindow);

    // 스캔 → 몸 / 두뇌 / 트레이 / 최상위 유지
    connect(&m_scanner, &DesktopScanner::scanned, this, &CatApp::onSnapshot);

    // 마우스 → 두뇌
    connect(&m_mouse, &MouseWatcher::userMoved, &m_brain, &CatBrain::onUserMoved);
    connect(&m_mouse, &MouseWatcher::becameIdle, &m_brain, &CatBrain::onMouseIdle);

    // 트레이 → 두뇌
    connect(&m_tray, &TrayController::trayApproach, this, [this](bool isNear) {
        m_brain.onTrayApproach(isNear, trayFloorPoint());
    });
    connect(&m_tray, &TrayController::blockRequested, &m_brain, &CatBrain::onBlockRequested);
    connect(&m_tray, &TrayController::blockReact, &m_brain, &CatBrain::onBlockReact);
    connect(&m_tray, &TrayController::blockReleased, &m_brain, &CatBrain::onBlockReleased);
    // 종료 방해 시작 / 유지 반응 / 해제에서 운다
    connect(&m_tray, &TrayController::blockRequested, &m_voice, &CatVoice::meow);
    connect(&m_tray, &TrayController::blockReact, &m_voice, &CatVoice::meow);
    connect(&m_tray, &TrayController::blockReleased, &m_voice, &CatVoice::meow);
    connect(&m_tray, &TrayController::quitRequested, qApp, &QCoreApplication::quit);
    connect(&m_tray, &TrayController::scaleChanged, this, [this](int newScale) {
        setScale(newScale);
        QSettings().setValue(Config::kSettingsScaleKey, m_scale);
    });

    connect(&m_tray, &TrayController::soundChanged, this, [this](bool enabled) {
        m_voice.setEnabled(enabled);
        QSettings().setValue(Config::kSettingsSoundKey, enabled);
    });
    connect(&m_tray, &TrayController::volumeChanged, this, [this](int percent) {
        m_voice.setVolume(percent);
        m_voice.meow();   // 바뀐 크기를 바로 들려준다
        QSettings().setValue(Config::kSettingsVolumeKey, m_voice.volume());
    });

    // 두뇌 → 트레이 / 오버레이
    connect(&m_brain, &CatBrain::blockAbandoned, &m_tray, &TrayController::releaseBlock);
    // 클릭 통과는 두뇌 요청 + Shift 잡기 대기 + 잡힘을 매 tick 합성한다 (updateClickThrough, README 3.4)
    connect(&m_brain, &CatBrain::wantClickThrough, this, [this](bool on) { m_brainClickThrough = on; });
    connect(&m_brain, &CatBrain::visibilityChanged, this, [this](bool visible) {
        m_visible = visible;
        if (visible) {
            m_haveImage = false;   // 다시 보일 때 위치부터 맞춰 그린다
            render();
            m_overlay.show();
            m_overlay.ensureTopmost();
        } else {
            m_overlay.hide();
        }
    });

    // 통계 (README 5.14): 누적 복원 → since 가 새로 정해졌을 수 있어 바로 저장
    {
        QSettings settings;
        m_stats.load(settings);
    }
    {
        // 읽어 온 원본 값 (통계가 바깥에서 바뀌었는지 비교하는 기준)
        const CatStatsValues &t = m_stats.total();
        qCInfo(lcStats).nospace().noquote() << "loaded total runMs=" << t.runMs << " roamMs=" << t.roamMs
                                  << " sleepMs=" << t.sleepMs << " playMs=" << t.playMs << " nudges=" << t.nudges
                                  << " quit=" << t.quitBlocks << '/' << t.quitAttempts << " grabs=" << t.grabs
                                  << " jumps=" << t.jumps << " falls=" << t.falls << " distanceM=" << t.distanceM
                                  << " since=" << m_stats.since().toString(Qt::ISODate);
    }
    saveStats();
    m_tray.setStats(&m_stats);
    connect(&m_brain, &CatBrain::nudged, this, [this] { m_stats.addNudge(); });
    connect(&m_tray, &TrayController::quitAttempted, this, [this](bool blocked) { m_stats.addQuitAttempt(blocked); });
    m_body.onMotion = [this](Locomotion::MotionEvent e) {   // onFrame 은 CatBrain 이 쓰므로 건드리지 않는다
        if (e == Locomotion::MotionEvent::Jump)
            m_stats.addJump();
        else
            m_stats.addFall();
    };
    m_statsSaveTimer.setInterval(Config::kStatsSaveIntervalMs);
    connect(&m_statsSaveTimer, &QTimer::timeout, this, &CatApp::saveStats);
    connect(qApp, &QCoreApplication::aboutToQuit, this, &CatApp::saveStats);

    // 정상 종료 요청(WM_CLOSE) / 세션 종료 (README 5.14)
    m_nativeFilter = std::make_unique<NativeFilter>(*this);
    qApp->installNativeEventFilter(m_nativeFilter.get());

    // 새 릴리스 확인 (README 5.15). 표시 방식을 바꿀 때는 여기서 만드는 구현만 바꾼다
    // (대화상자로 돌아가려면 std::make_unique<DialogUpdateNotifier>())
    m_updateNotifier = std::make_unique<BubbleUpdateNotifier>(m_brain, m_bubble, m_voice,
                                                              [this] { return bubblePlacement(); });
    connect(&m_updates, &UpdateChecker::updateAvailable, this, &CatApp::onUpdateAvailable);

    // 오버레이 → Shift 잡기 / 트레이 (버튼을 덮은 고양이 클릭)
    connect(&m_overlay, &CatOverlay::clicked, this, &CatApp::onCatPressed);
    connect(&m_overlay, &CatOverlay::released, this, [this] {
        if (!m_grabbing)
            return;
        m_brain.releaseGrab();
        m_grabbing = false;
    });
}

void CatApp::onCatPressed(Qt::KeyboardModifiers modifiers)
{
    // 방해 중 Shift+클릭은 즉시 종료(TrayController), 아닐 때의 Shift+클릭이 잡기 (README 5.6, 5.13)
    if ((modifiers & Qt::ShiftModifier) && !m_tray.isBlocking()) {
        if (m_brain.grab(QCursor::pos())) {
            m_grabbing = true;
            m_stats.addGrab();
            m_voice.grabCry();   // 잡히는 순간에만 (README 5.12)
        }
        return;
    }
    m_tray.onCatClicked(modifiers);
}

void CatApp::updateGrab()
{
    if (!m_grabbing)
        return;
    if (m_body.mode() != Locomotion::Mode::Held)
        m_grabbing = false;   // Hidden 등으로 풀렸다
    else if (!m_mouse.leftButtonDown()) {
        m_brain.releaseGrab();   // released 시그널을 놓친 경우의 안전장치
        m_grabbing = false;
    } else {
        m_brain.moveGrab(QCursor::pos());
    }
}

void CatApp::updateClickThrough()
{
    const bool shiftHover = m_mouse.shiftDown() && !m_mouse.anyButtonDown() && m_visible
        && m_overlay.hitsOpaque(QCursor::pos());
    const bool through = m_brainClickThrough && !shiftHover && !m_grabbing;
    if (through != m_overlay.isClickThrough())
        m_overlay.setClickThrough(through);
}

void CatApp::setScale(int scale)
{
    m_scale = std::clamp(scale, Config::kMinScale, Config::kMaxScale);
    m_scanner.setScale(m_scale);
    m_overlay.setScale(m_scale);
    m_body.setScale(m_scale);
    m_planner.setScale(m_scale);
    m_brain.setScale(m_scale);
    m_tray.setScale(m_scale);
    m_bubble.setScale(m_scale);
    m_haveImage = false;
    if (m_tickTimer.isActive())
        m_scanner.scanNow();   // 최소 세그먼트 길이 등이 바뀌므로 즉시 다시 스캔
}

QPoint CatApp::spawnPoint(const DesktopSnapshot &snapshot) const
{
    if (snapshot.monitors.empty())
        return QPoint(0, 0);

    // 주 모니터 (스냅샷에서 같은 사각형 → 없으면 첫 번째)
    const QScreen *screen = QGuiApplication::primaryScreen();
    const QRect primary = screen ? screen->geometry() : snapshot.monitors.front().bounds;
    const MonitorInfo *mon = &snapshot.monitors.front();
    for (const MonitorInfo &m : snapshot.monitors) {
        if (m.bounds.contains(primary.center())) {
            mon = &m;
            break;
        }
    }

    // 바닥 중앙 위, 화면 높이의 1/3 위에서 떨어뜨린다. 창 지붕이 아니라 모니터 바닥에 내려앉도록
    // 중앙에서 좌우로 가며 아래가 뚫린 x 를 찾는다.
    const int y = mon->floorY - mon->bounds.height() / 3;
    const int cx = mon->bounds.center().x();
    for (int k = 0; cx - k * kSpawnSearchStepPx >= mon->bounds.left()
                    || cx + k * kSpawnSearchStepPx <= mon->bounds.right(); ++k) {
        for (const int x : {cx + k * kSpawnSearchStepPx, cx - k * kSpawnSearchStepPx}) {
            if (x < mon->bounds.left() || x > mon->bounds.right())
                continue;
            const SurfaceSegment *floor = snapshot.floorBelow(QPoint(x, y));
            if (floor && floor->id.owner == 0)
                return QPoint(x, y);
        }
    }
    return QPoint(cx, y);
}

QPoint CatApp::trayFloorPoint() const
{
    // 팝업이 뜰 트레이 왼쪽 바닥
    const QRect tray = m_tray.trayRect();
    const int floorY = m_tray.trayFloorY();
    int x = tray.center().x() - Config::kTrayApproachOffsetSpritePx * m_scale;
    for (const MonitorInfo &m : m_scanner.snapshot().monitors) {
        if (m.bounds.contains(tray.center())) {
            x = std::clamp(x, m.bounds.left(), m.bounds.right());
            break;
        }
    }
    return QPoint(x, floorY);
}

void CatApp::start()
{
    m_scanner.start();   // 즉시 1회 스캔 → onSnapshot (몸은 아직 위치가 없어 무시)
    const DesktopSnapshot &snapshot = m_scanner.snapshot();
    m_body.spawn(snapshot, spawnPoint(snapshot));

    m_clock.start();
    m_lastTickMs = 0;
    m_haveImage = false;
    render();                // 첫 프레임 위치를 잡은 뒤에 보여 준다 (엉뚱한 곳에서 깜빡이지 않게)
    m_overlay.show();
    m_overlay.ensureTopmost();

    m_mouse.start();
    m_tickTimer.start();
    m_followTimer.start();

    m_lastTravelledPx = m_body.travelledPx();   // spawn 이후 값부터 차이를 센다
    m_statsSaveTimer.start();

    m_updates.start(QStringLiteral(CCAT_VERSION));   // Config::kUpdateCheckDelayMs 뒤 1회
}

void CatApp::onUpdateAvailable(const QString &version)
{
    m_updateNotifier->notify(version, QStringLiteral(CCAT_VERSION), [version](UpdateChoice choice) {
        switch (choice) {
        case UpdateChoice::OpenPage:
            QDesktopServices::openUrl(QUrl(QString::fromLatin1(Config::kReleasesPageUrl)));
            break;
        case UpdateChoice::Skip: UpdateChecker::skipVersion(version); break;
        case UpdateChoice::Later: break;
        }
    });
}

void CatApp::onSnapshot(const DesktopSnapshot &snapshot)
{
    m_body.onSnapshot(snapshot);
    m_brain.onSnapshot(snapshot);
    m_tray.updateSnapshot(snapshot);
    m_overlay.ensureTopmost();
    if (m_bubble.isVisible())
        m_bubble.ensureTopmost();
}

void CatApp::tick()
{
    const qint64 now = m_clock.elapsed();
    // 절전 복귀 등으로 한꺼번에 밀린 시간은 잘라낸다
    const qint64 dt = std::clamp<qint64>(now - m_lastTickMs, 0, Config::kMaxTickDtMs);
    m_lastTickMs = now;

    m_tray.onCursor(m_mouse.pos());   // 떠나는 지연 판정은 주기적인 호출이 필요
    updateGrab();                     // 잡힌 동안 그립 = 커서 (몸이 움직이기 전에 반영)
    m_body.tick(dt);
    m_brain.setPopupRect(m_tray.popupRect());
    m_brain.tick(now);                // 이동이 끝난 직후 같은 tick 에 다음 step 을 이어 주므로 frame() 전에 호출
    updateStats(dt);
    updateClickThrough();
    if (m_visible)
        render();
}

void CatApp::followWindow()
{
    const std::optional<SurfacePoint> at = m_body.attachment();
    if (!at || at->surface.owner == 0)
        return;
    if (const std::optional<QRect> live = DesktopScanner::liveWindowRect(at->surface.owner)) {
        m_body.followOwner(*live);
        if (m_visible)
            render();
    }
}

void CatApp::render()
{
    const CatFrame f = m_body.frame();
    if (!m_haveImage || f.anim != m_lastAnim || f.animFrame != m_lastFrame || f.facing != m_lastFacing
        || f.gravity != m_lastGravity) {
        m_image = CatSprite::renderOriented(f.pose, f.gravity, f.facing);
        m_lastAnim = f.anim;
        m_lastFrame = f.animFrame;
        m_lastFacing = f.facing;
        m_lastGravity = f.gravity;
        m_haveImage = true;
        m_lastAnchor = QPoint(-1 << 20, -1 << 20);   // 새 이미지는 반드시 전달
    }
    if (f.anchor != m_lastAnchor) {
        m_overlay.setFrame(m_image, f.anchor, f.gravity);
        m_lastAnchor = f.anchor;
        if (m_bubble.isVisible()) {   // 앉은 창이 움직이면 말풍선도 따라간다
            const BubblePlacement p = bubblePlacement();
            m_bubble.follow(p.catRect, p.gravity, p.monitor);
        }
    }
}

BubblePlacement CatApp::bubblePlacement() const
{
    // 오버레이 창 왼쪽 위부터 회전된 이미지가 그려진다 (CatOverlay::place)
    const CatFrame f = m_body.frame();
    const QPoint topLeft = f.anchor - CatSprite::anchorIn(f.gravity) * m_scale;
    BubblePlacement p;
    p.catRect = QRect(topLeft, m_image.size() * m_scale);
    p.gravity = f.gravity;
    // 앵커는 면 위(바닥이면 모니터 아래 경계 = bottom()+1)에 있어 contains 로는 모니터를 못 찾을 수 있다 → 그림 중심으로 찾는다
    const QPoint at = p.catRect.center();
    for (const MonitorInfo &m : m_scanner.snapshot().monitors) {
        if (m.bounds.contains(at)) {
            p.monitor = m.bounds;
            break;
        }
    }
    if (p.monitor.isNull()) {
        const QScreen *screen = QGuiApplication::screenAt(at);
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        p.monitor = screen ? screen->geometry() : p.catRect;
    }
    return p;
}

// ── 통계 (README 5.14) ───────────────────────────────────
void CatApp::saveStats()
{
    QSettings settings;
    const CatStatsSaveResult r = m_stats.save(settings);
    settings.sync();
    if (settings.status() != QSettings::NoError)
        qCWarning(lcStats) << "save failed, status" << settings.status();
    if (!r.behind.isEmpty())   // 다른 무언가가 저장값을 되돌렸다 (메모리 값으로 다시 썼다)
        qCWarning(lcStats).noquote() << "stored stats went backwards, rewrote from memory:" << r.behind.join(QStringLiteral(", "))
                                     << "(runMs last" << r.lastRunMs << "stored" << r.storedRunMs << ")";
    if (!r.ahead.isEmpty())    // 다른 무언가가 저장값을 키웠다 (누적은 줄지 않으므로 저장값을 따랐다)
        qCWarning(lcStats).noquote() << "stored stats were ahead of memory, kept stored:" << r.ahead.join(QStringLiteral(", "));
    const CatStatsValues &t = m_stats.total();
    qCInfo(lcStats).noquote() << "saved total run" << CatStats::formatDuration(t.runMs)
                              << "roam" << CatStats::formatDuration(t.roamMs)
                              << "sleep" << CatStats::formatDuration(t.sleepMs)
                              << "play" << CatStats::formatDuration(t.playMs);
}

void CatApp::updateStats(qint64 dtMs)
{
    m_stats.addRunMs(dtMs);
    const CatBrain::Activity activity = m_brain.activity();
    if (activity != m_lastActivity) {
        qCInfo(lcStats).noquote() << "activity" << activityName(m_lastActivity) << "->" << activityName(activity)
                                  << "after" << CatStats::formatDuration(m_activityMs);
        m_activityMs = 0;
        m_lastActivity = activity;
    }
    m_activityMs += dtMs;
    switch (activity) {
    case CatBrain::Activity::Roaming: m_stats.addRoamMs(dtMs); break;
    case CatBrain::Activity::Sleeping: m_stats.addSleepMs(dtMs); break;
    case CatBrain::Activity::Playing: m_stats.addPlayMs(dtMs); break;
    case CatBrain::Activity::Other: break;
    }
    const double travelled = m_body.travelledPx();
    const double d = travelled - m_lastTravelledPx;
    if (d > 0.0)
        m_stats.addDistanceM(pxToMeters(d, m_body.anchor()));
    m_lastTravelledPx = travelled;
}

double CatApp::pxToMeters(double px, QPoint at) const
{
    const QScreen *screen = QGuiApplication::screenAt(at);
    double dpi = screen ? screen->physicalDotsPerInch() : 0.0;
    if (dpi <= 0.0)
        dpi = Config::kFallbackDpi;
    return px / dpi * 0.0254;
}
