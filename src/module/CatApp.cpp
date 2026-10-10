#include "CatApp.hpp"

#include "Config.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>

#include <algorithm>

namespace {

constexpr int kSpawnSearchStepPx = 64;   // 주 모니터 중앙에서 좌우로 바닥이 뚫린 지점을 찾는 간격

} // namespace

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
    connect(&m_tray, &TrayController::trayApproach, this, [this](bool near) {
        m_brain.onTrayApproach(near, trayFloorPoint());
    });
    connect(&m_tray, &TrayController::blockRequested, &m_brain, &CatBrain::onBlockRequested);
    connect(&m_tray, &TrayController::blockReact, &m_brain, &CatBrain::onBlockReact);
    connect(&m_tray, &TrayController::blockReleased, &m_brain, &CatBrain::onBlockReleased);
    connect(&m_tray, &TrayController::quitRequested, qApp, &QCoreApplication::quit);
    connect(&m_tray, &TrayController::scaleChanged, this, [this](int newScale) {
        setScale(newScale);
        QSettings().setValue(Config::kSettingsScaleKey, m_scale);
    });

    // 두뇌 → 트레이 / 오버레이
    connect(&m_brain, &CatBrain::blockAbandoned, &m_tray, &TrayController::releaseBlock);
    connect(&m_brain, &CatBrain::wantClickThrough, &m_overlay, &CatOverlay::setClickThrough);
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

    // 오버레이 → 트레이 (버튼을 덮은 고양이 클릭)
    connect(&m_overlay, &CatOverlay::clicked, &m_tray, &TrayController::onCatClicked);
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
}

void CatApp::onSnapshot(const DesktopSnapshot &snapshot)
{
    m_body.onSnapshot(snapshot);
    m_brain.onSnapshot(snapshot);
    m_tray.updateSnapshot(snapshot);
    m_overlay.ensureTopmost();
}

void CatApp::tick()
{
    const qint64 now = m_clock.elapsed();
    // 절전 복귀 등으로 한꺼번에 밀린 시간은 잘라낸다
    const qint64 dt = std::clamp<qint64>(now - m_lastTickMs, 0, Config::kMaxTickDtMs);
    m_lastTickMs = now;

    m_tray.onCursor(m_mouse.pos());   // 떠나는 지연 판정은 주기적인 호출이 필요
    m_body.tick(dt);
    m_brain.setPopupRect(m_tray.popupRect());
    m_brain.tick(now);                // 이동이 끝난 직후 같은 tick 에 다음 step 을 이어 주므로 frame() 전에 호출
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
    }
}
