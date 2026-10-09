#include "TrayController.hpp"

#include "AutoStart.hpp"
#include "Config.hpp"
#include "QuitPopup.hpp"

#include <QGuiApplication>
#include <QScreen>

#include <cmath>
#include <utility>

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
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::Context)
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

    QRect rect = m_tray.geometry();
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
    const QPoint center = trayRect().center();
    for (const MonitorInfo &m : m_snapshot.monitors) {
        if (m.bounds.contains(center))
            return m.floorY;
    }
    // 스냅샷에 없으면 Qt 화면의 작업 영역 하단
    const QScreen *screen = QGuiApplication::screenAt(center);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry().bottom() + 1 : 0;
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
    if (m_guard.attempt(m_clock.elapsed())) {
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

    const QRect tray = trayRect();
    QRect monitor;
    for (const MonitorInfo &m : m_snapshot.monitors) {
        if (m.bounds.contains(tray.center())) {
            monitor = m.bounds;
            break;
        }
    }
    if (monitor.isEmpty()) {
        const QScreen *screen = QGuiApplication::screenAt(tray.center());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        monitor = screen ? screen->geometry() : tray;
    }

    m_popup->setAutoStartChecked(AutoStart::isEnabled(m_autoStartKey));   // 체크 상태 = 레지스트리 값 존재 여부
    m_popup->setScale(m_scale);
    m_popup->showAt(tray, trayFloorY(), monitor);
}

void TrayController::onQuitHovered()
{
    if (m_blocking)
        return;
    if (m_guard.attempt(m_clock.elapsed())) {
        m_blocking = true;
        emit blockRequested(quitButtonRect());
    }
}

void TrayController::onQuitClicked()
{
    // 고양이가 덮지 않은(눈에 보이는) 버튼 부분을 눌렀다 → 방해 여부와 상관없이 종료 (재판정 없음)
    emit quitRequested();
}
