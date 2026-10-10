#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "CatOverlay.hpp"

#include "Config.hpp"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>

namespace {

HWND toHwnd(WId id)
{
    return reinterpret_cast<HWND>(id);
}

// 확장 스타일 비트 켜기/끄기. 바뀐 게 있을 때만 쓰고 true 를 돌려준다
bool setExStyleBits(HWND hwnd, LONG_PTR bits, bool on)
{
    const LONG_PTR before = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const LONG_PTR after = on ? (before | bits) : (before & ~bits);
    if (after == before)
        return false;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, after);
    return true;
}

} // namespace

CatOverlay::CatOverlay(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint
                          | Qt::WindowDoesNotAcceptFocus)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setScale(m_scale);
    applyNativeStyle();   // 네이티브 창을 미리 만들어 표시 전에 스타일을 적용 (showEvent 에서도 다시 확인)
}

void CatOverlay::setScale(int scale)
{
    m_scale = qBound(Config::kMinScale, scale, Config::kMaxScale);
    setFixedSize(CatSprite::Width * m_scale, CatSprite::Width * m_scale);
    if (m_hasFrame)
        place();
    update();
}

void CatOverlay::setFrame(const QImage &orientedSprite, QPoint anchorDesktop, CatGravity gravity)
{
    const bool changed = (gravity != m_gravity) || (orientedSprite != m_sprite);
    m_sprite = orientedSprite;
    m_gravity = gravity;
    m_anchorDesktop = anchorDesktop;
    m_hasFrame = true;
    place();
    if (changed)
        update();
}

void CatOverlay::place()
{
    // 스프라이트는 창 왼쪽 위(0,0)부터 그린다. 앵커가 anchorDesktop 에 오도록 창 왼쪽 위를 계산.
    const QPoint topLeft = m_anchorDesktop - CatSprite::anchorIn(m_gravity) * m_scale;
    if (m_placed && topLeft == m_topLeft)
        return;
    m_topLeft = topLeft;
    m_placed = true;
    // 활성화/Z 순서/크기를 건드리지 않고 이동만 한다
    SetWindowPos(toHwnd(winId()), nullptr, topLeft.x(), topLeft.y(), 0, 0,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);
}

void CatOverlay::setClickThrough(bool on)
{
    m_clickThrough = on;
    // WS_EX_TRANSPARENT 만 토글. 레이어드 창이라 해제해도 알파 0 픽셀은 계속 통과된다
    setExStyleBits(toHwnd(winId()), WS_EX_TRANSPARENT, on);
}

bool CatOverlay::hitsOpaque(QPoint desktop) const
{
    if (!isVisible() || !m_hasFrame || m_sprite.isNull())
        return false;
    const QPoint local = desktop - m_topLeft;   // 창 로컬 (물리 px)
    if (local.x() < 0 || local.y() < 0
        || local.x() >= m_sprite.width() * m_scale || local.y() >= m_sprite.height() * m_scale)
        return false;
    return qAlpha(m_sprite.pixel(local.x() / m_scale, local.y() / m_scale)) > 0;   // 정수 나눗셈 (QPoint / int 는 반올림)
}

void CatOverlay::ensureTopmost()
{
    if (!isVisible())
        return;
    SetWindowPos(toHwnd(winId()), HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

WindowHandle CatOverlay::handle() const
{
    return WindowHandle(winId());
}

void CatOverlay::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);   // 알파 0 영역을 확실히 비움
    if (m_sprite.isNull())
        return;
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);   // 정수 배율 nearest
    painter.drawImage(QRect(QPoint(0, 0), m_sprite.size() * m_scale), m_sprite);
}

void CatOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        // TODO(decision): 왼쪽 버튼만 clicked 로 취급
        return;
    }
    // 창이 포커스를 받지 않으므로 키보드 상태는 전역에서 직접 읽는다
    Qt::KeyboardModifiers mods = QGuiApplication::queryKeyboardModifiers();
    if (GetKeyState(VK_SHIFT) & 0x8000)
        mods |= Qt::ShiftModifier;
    emit clicked(mods);
}

void CatOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    // 누른 창이 마우스를 캡처하므로 커서가 창 밖에서 떼어져도 온다 (잡기 해제, README 5.13)
    if (event->button() == Qt::LeftButton)
        emit released();
}

void CatOverlay::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    applyNativeStyle();
}

void CatOverlay::applyNativeStyle()
{
    // README 3.4: 레이어드 + (클릭 통과) + 포커스 안 받음 + 작업표시줄/Alt+Tab 미표시.
    // WA_TranslucentBackground 가 이미 WS_EX_LAYERED 를 쓰므로 OR 만 한다 (다른 비트는 건드리지 않음)
    HWND hwnd = toHwnd(winId());
    setExStyleBits(hwnd, WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, true);
    setExStyleBits(hwnd, WS_EX_TRANSPARENT, m_clickThrough);
}
