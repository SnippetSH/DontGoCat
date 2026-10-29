#include "TrayController.hpp"

#include "AutoStart.hpp"
#include "CatSprite.hpp"
#include "Config.hpp"
#include "QuitPopup.hpp"


#include <QDesktopServices>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QScreen>
#include <QUrl>

#include <cmath>
#include <utility>

Q_LOGGING_CATEGORY(lcTray, "ccat.tray")

namespace {
constexpr int kReopenGapPx = 8;   // 다시 실행 팝업과 고양이 창 사이 간격
} // namespace

TrayController::TrayController(QObject *parent, QuitGuard::Rng rng)
    : QObject(parent)
    , m_popup(new QuitPopup)
    , m_guard(std::move(rng))
{
    m_clock.start();

    m_tray.setIcon(QuitPopup::faceIcon());
    m_tray.setToolTip(QStringLiteral("DontGoCat"));
    // 표준 QMenu 는 쓰지 않는다. 좌클릭/우클릭 모두 팝업 토글
    connect(&m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason != QSystemTrayIcon::Trigger && reason != QSystemTrayIcon::Context)
            return;
#ifdef Q_OS_MACOS
        m_clickedTrayRect = m_tray.geometry();   // 방금 눌렸으니 보이는 상태 (trayRect 설명 참고)
        m_trayRectCacheMs = -1;
#endif
        togglePopup();
    });
    m_tray.show();

    connect(m_popup, &QuitPopup::quitHovered, this, &TrayController::onQuitHovered);
    connect(m_popup, &QuitPopup::quitClicked, this, &TrayController::onQuitClicked);
    connect(m_popup, &QuitPopup::scaleSelected, this, [this](int scale) {
        setScale(scale);
        emit scaleChanged(m_scale);
    });
    connect(m_popup, &QuitPopup::autoStartToggled, this, [this](bool enabled) {
        // 레지스트리 쓰기에 실패하면 체크박스를 원래대로 되돌린다
        if (!AutoStart::setEnabled(enabled, m_autoStartKey))
            m_popup->setAutoStartChecked(!enabled);
    });
    connect(m_popup, &QuitPopup::soundToggled, this, [this](bool enabled) {
        m_soundEnabled = enabled;
        emit soundChanged(enabled);
    });
    connect(m_popup, &QuitPopup::volumeSelected, this, [this](int percent) {
        m_volume = percent;
        emit volumeChanged(percent);
    });
    connect(m_popup, &QuitPopup::showTrayIconClicked, this, &TrayController::restoreTrayIcon);
    connect(m_popup, &QuitPopup::closed, this, [this]() {
        m_popupClosedMs = m_clock.elapsed();
        if (m_blocking) {
            m_blocking = false;
            emit blockReleased();
        }
    });
}

TrayController::~TrayController()
{
    m_tray.hide();
    delete m_popup;
}

void TrayController::setScale(int scale)
{
    m_scale = scale;
    m_popup->setScale(scale);
}

void TrayController::updateSnapshot(const DesktopSnapshot &snapshot)
{
    m_snapshot = snapshot;
    m_trayRectCacheMs = -1;   // 트레이 사각형 대체값이 바뀌었을 수 있다
}

QRect TrayController::trayRect() const
{
    const qint64 now = m_clock.elapsed();
    if (m_trayRectCacheMs >= 0 && now - m_trayRectCacheMs < Config::kTrayRectCacheMs)
        return m_trayRectCache;

#ifdef Q_OS_MACOS
    // macOS 26+: 사용자가 메뉴 막대에서 숨긴(Cmd+드래그 / 설정 스위치) 아이콘은 geometry() 를 읽을 때마다 되살아난다 (실측).
    // 주기적으로 읽지 않고 마지막으로 아이콘을 클릭했을 때의 위치만 쓴다 (그 전에는 아래 대체값)
    QRect rect = m_clickedTrayRect;
#else
    QRect rect = m_tray.geometry();
#endif
    if (rect.isEmpty())
        rect = m_snapshot.trayRect;
    if (rect.isEmpty()) {
        // TODO(decision): 어디서도 못 구하면 주 모니터 우하단 모서리의 가상 아이콘으로 대체
        const QScreen *screen = QGuiApplication::primaryScreen();
        const QRect bounds = screen ? screen->geometry() : QRect(0, 0, 1920, 1080);
        rect = QRect(bounds.right() + 1 - Config::kFallbackTrayPx, bounds.bottom() + 1 - Config::kFallbackTrayPx,
                     Config::kFallbackTrayPx, Config::kFallbackTrayPx);
    }
    m_trayRectCache = rect;
    m_trayRectCacheMs = now;
    return rect;
}

int TrayController::trayFloorY() const
{
    return floorYAt(trayRect().center());
}

const MonitorInfo *TrayController::monitorInfoAt(QPoint p) const
{
    for (const MonitorInfo &m : m_snapshot.monitors) {
        if (m.bounds.contains(p))
            return &m;
    }
    return nullptr;
}

int TrayController::floorYAt(QPoint p) const
{
    if (const MonitorInfo *m = monitorInfoAt(p))
        return m->floorY;
    // 스냅샷에 없으면 Qt 화면의 작업 영역 하단
    const QScreen *screen = QGuiApplication::screenAt(p);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry().bottom() + 1 : 0;
}

bool TrayController::trayIconVisible() const
{
#ifdef Q_OS_MACOS
    return false;   // 숨긴 아이콘은 위치를 묻기만 해도 되살아나 확인할 수 없다 (trayRect 설명) → 모르는 것으로 본다
#else
    const QRect icon = m_tray.geometry();
    if (icon.isEmpty())
        return false;
    for (const QScreen *screen : QGuiApplication::screens()) {
        if (screen->geometry().intersects(icon))
            return true;
    }
    return false;
#endif
}

bool TrayController::isPopupOpen() const
{
    return m_popup->isVisible();
}

QRect TrayController::quitButtonRect() const
{
    return m_popup->quitButtonDesktopRect();
}

QRect TrayController::popupRect() const
{
    return m_popup->isVisible() ? m_popup->desktopRect() : QRect();
}

void TrayController::releaseBlock()
{
    if (!m_blocking)
        return;
    m_blocking = false;
    emit blockReleased();
}

void TrayController::onCursor(QPoint pos)
{
    const QRect rect = trayRect();
    const QPoint c = rect.center();
    const double dx = pos.x() - c.x();
    const double dy = pos.y() - c.y();
    const double r = Config::kTrayApproachRadiusPx;
    const bool near = rect.contains(pos) || (dx * dx + dy * dy <= r * r);

    if (near) {
        m_leftSince.invalidate();
        if (!m_near) {
            m_near = true;
            emit trayApproach(true);
        }
        return;
    }

    if (!m_near)
        return;
    // 벗어난 시각 기록. 벗어난 지 kTrayLeaveDelayMs 가 지났고 팝업이 닫혀 있으면 false
    if (!m_leftSince.isValid())
        m_leftSince.start();
    if (m_leftSince.elapsed() >= Config::kTrayLeaveDelayMs && !isPopupOpen()) {
        m_near = false;
        m_leftSince.invalidate();
        emit trayApproach(false);
    }
}

void TrayController::onCatClicked(Qt::KeyboardModifiers modifiers)
{
    if (modifiers & Qt::ShiftModifier) {
        emit quitRequested();   // Shift+클릭은 방해 여부와 상관없이 즉시 종료
        return;
    }
    if (!m_blocking)
        return;   // 방해 중이 아니면 고양이 클릭은 무시한다
    // 방해 중 고양이 몸 클릭 = 재시도: 재판정
    const bool blocked = m_guard.attempt(m_clock.elapsed());
    emit quitAttempted(blocked);
    if (blocked) {
        emit blockReact();         // 방해 유지 → 고양이 반응
    } else {
        m_blocking = false;        // 방해 실패 → 고양이가 비켜 앉는다
        emit blockReleased();
    }
}

void TrayController::togglePopup()
{
    if (m_popup->isVisible()) {
        m_popup->hide();   // closed → blockReleased 처리
        return;
    }
    // 트레이 클릭으로 팝업이 비활성화되어 방금 닫힌 직후라면 그 클릭은 "닫기"였다
    if (m_popupClosedMs >= 0 && m_clock.elapsed() - m_popupClosedMs < Config::kReopenGuardMs)
        return;

    openPopupAt(trayRect());
}

void TrayController::showPopupForReopen(QPoint catAnchor)
{
    const bool iconVisible = trayIconVisible();
    qCInfo(lcTray) << "reopen: tray icon visible" << iconVisible << "cat" << catAnchor;
    if (m_popup->isVisible()) {
        m_popup->raise();
        m_popup->activateWindow();
        return;
    }
    if (iconVisible) {
        openPopupAt(trayRect());
        return;
    }
    // 고양이 창(정사각형, 앵커가 가로 중심)과 겹치지 않게 옆에 둔다. 폭은 setScale 의 relayout 으로 정해진다
    // 고양이 앵커(발)는 바닥선 위에 있어 모니터 사각형 밖일 수 있다 → 1px 위 점으로 모니터를 고른다
    m_popup->setScale(m_scale);
    const QPoint probe = catAnchor - QPoint(0, 1);
    const MonitorInfo *mon = monitorInfoAt(probe);
    const int monRight = mon ? mon->bounds.x() + mon->bounds.width() : catAnchor.x() + m_popup->width() * 2;
    const int offset = CatSprite::Width * m_scale / 2 + kReopenGapPx + m_popup->width() / 2;
    const bool right = catAnchor.x() + offset + m_popup->width() / 2 <= monRight;
    const int x = catAnchor.x() + (right ? offset : -offset);
    openPopupAt(QRect(QPoint(x, probe.y()), QSize(1, 1)));
}

void TrayController::restoreTrayIcon()
{
#ifdef Q_OS_MACOS
    // macOS 26+ 는 Cmd+드래그로 지운 아이템을 시스템(제어 센터)이 숨기고 있어 앱이 되돌릴 수 없다 (실측).
    // 아이템을 지우고 다시 만들어도 돌아오지 않고, 오히려 설정의 "메뉴 막대에서 허용" 스위치가 실행 중에 먹지 않게 된다
    // → 아이템은 그대로 두고, 사용자가 스위치를 켜도록 그 설정 화면만 연다
    qCInfo(lcTray) << "open menu bar settings";
    QDesktopServices::openUrl(QUrl(QStringLiteral("x-apple.systempreferences:com.apple.ControlCenter-Settings.extension")));
#endif
}

void TrayController::openPopupAt(const QRect &anchor)
{
    QRect monitor;
    if (const MonitorInfo *m = monitorInfoAt(anchor.center()))
        monitor = m->bounds;
    if (monitor.isEmpty()) {
        const QScreen *screen = QGuiApplication::screenAt(anchor.center());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        monitor = screen ? screen->geometry() : anchor;
    }

    m_popup->setAutoStartChecked(AutoStart::isEnabled(m_autoStartKey));   // 체크 상태 = 레지스트리 값 존재 여부
    m_popup->setSoundChecked(m_soundEnabled);
    m_popup->setVolume(m_volume);
    m_popup->setScale(m_scale);
    m_popup->showAt(anchor, floorYAt(anchor.center()), monitor);
}

void TrayController::onQuitHovered()
{
    if (m_blocking)
        return;
    const bool blocked = m_guard.attempt(m_clock.elapsed());
    emit quitAttempted(blocked);
    if (blocked) {
        m_blocking = true;
        emit blockRequested(quitButtonRect());
    }
}

void TrayController::onQuitClicked()
{
    // 고양이가 덮지 않은(눈에 보이는) 버튼 부분을 눌렀다 → 방해 여부와 상관없이 종료 (재판정 없음)
    emit quitRequested();
}

void TrayController::setStats(const CatStats *stats)
{
    m_popup->setStatsSource(stats);
}
