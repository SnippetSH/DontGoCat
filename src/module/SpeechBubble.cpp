#include <QtGlobal>   // Q_OS_WIN 정의용 (windows.h 보다 먼저)

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "SpeechBubble.hpp"

#include "CatSprite.hpp"
#include "Config.hpp"
#include "DpiSizeLock.hpp"

#ifdef Q_OS_MACOS
#include "MacNative.hpp"
#endif

#include <QEnterEvent>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

#include <algorithm>

namespace {

// 글자 / 테두리 색은 고양이 외곽선 색을 그대로 쓴다 (CatPalette::outline)
const QColor kBorderColor = CatPalette{}.outline;
const QColor kFillColor(0xFF, 0xFF, 0xFF);
const QColor kTextColor = CatPalette{}.outline;
const QColor kMutedColor(0x77, 0x77, 0x77);      // 건너뛰기 링크
const QColor kCloseHoverColor(0xC0, 0x39, 0x2B);  // × 위에 커서가 있을 때

// 본문 줄과 링크 줄 사이 간격 = kBubblePaddingPx / 2 (파일 로컬 규칙, Config 에 따로 두지 않음)

QFont makeFont(int pixelSize, bool underline = false)
{
#ifdef Q_OS_MACOS
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);   // QuitPopup 과 같은 규칙
#else
    QFont font;   // QuitPopup 과 같은 글꼴 목록
    font.setFamilies({QStringLiteral("Segoe UI"), QStringLiteral("Malgun Gothic")});
#endif
    font.setPixelSize(pixelSize);
    font.setUnderline(underline);
    return font;
}

#ifdef Q_OS_WIN
HWND toHwnd(WId id)
{
    return reinterpret_cast<HWND>(id);
}

// 확장 스타일 비트 켜기. 실제 HWND 가 아니면(offscreen 등) 아무것도 하지 않는다
void setExStyleBits(HWND hwnd, LONG_PTR bits)
{
    if (!IsWindow(hwnd))
        return;
    const LONG_PTR before = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((before | bits) != before)
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, before | bits);
}
#endif

void applyNativeStyle(QWidget *w)
{
#ifdef Q_OS_WIN
    // CatOverlay::applyNativeStyle 와 같다: 포커스를 받지 않고 작업표시줄 / Alt+Tab 에 나타나지 않는다
    setExStyleBits(toHwnd(w->winId()), WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
#elif defined(Q_OS_MACOS)
    // 고양이 창과 같은 level / Space 동작 (클릭은 받는다)
    MacNative::configureOverlayWindow(w->winId());
#else
    Q_UNUSED(w);
#endif
}

} // namespace

SpeechBubble::SpeechBubble(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint
                          | Qt::WindowDoesNotAcceptFocus)
    , m_timeout(new QTimer(this))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
#ifdef Q_OS_MACOS
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);   // 앱 비활성 시에도 숨지 않게 (CatOverlay 와 같음)
#endif
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);   // 말풍선 어디든 눌린다
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(Config::kBubbleTimeoutMs);
    connect(m_timeout, &QTimer::timeout, this, [this]() {
        hideBubble();
        emit timedOut();
    });
    applyNativeStyle(this);   // 네이티브 창을 미리 만들어 표시 전에 스타일 적용 (showEvent 에서도 다시 확인)
    relayout();
}

int SpeechBubble::unit() const
{
    return Config::kBubbleBorderSpritePx * m_scale;
}

int SpeechBubble::tailLength() const
{
    // 꼬리는 테두리 한 칸 단위 계단이므로 길이를 칸 수의 배수로 맞춘다
    const int u = unit();
    return std::max(1, Config::kBubbleTailSpritePx * m_scale / u) * u;
}

void SpeechBubble::setScale(int scale)
{
    const int s = qBound(Config::kMinScale, scale, Config::kMaxScale);
    if (s == m_scale)
        return;
    m_scale = s;
    relayout();
}

void SpeechBubble::setText(const QString &message)
{
    m_text = message;
    relayout();
}

void SpeechBubble::showFor(const QRect &catRect, CatGravity gravity, const QRect &monitor)
{
    m_catRect = catRect;
    m_gravity = gravity;
    m_monitor = monitor;
    place();
    if (!isVisible())
        show();
    // 이미 보이면 위치만 다시 잡고 타이머를 처음부터. 커서가 올라가 있으면 떼는 순간부터 센다
    m_remainingMs = m_timeout->interval();
    m_paused = m_hover;
    if (m_hover)
        m_timeout->stop();
    else
        m_timeout->start();
}

void SpeechBubble::follow(const QRect &catRect, CatGravity gravity, const QRect &monitor)
{
    m_catRect = catRect;
    m_gravity = gravity;
    m_monitor = monitor;
    if (isVisible())
        place();
}

void SpeechBubble::hideBubble()
{
    m_timeout->stop();
    m_paused = false;
    m_hover = false;
    m_pressRegion = Region::None;
    m_hoverRegion = Region::None;
    hide();
}

void SpeechBubble::dismiss()
{
    hideBubble();
}

void SpeechBubble::ensureTopmost()
{
#ifdef Q_OS_WIN
    if (!isVisible())
        return;
    HWND hwnd = toHwnd(winId());
    if (IsWindow(hwnd))
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#elif defined(Q_OS_MACOS)
    if (isVisible())
        MacNative::orderFront(winId());
#endif
}

void SpeechBubble::setTimeoutMs(int ms)
{
    m_timeout->setInterval(ms);
}

void SpeechBubble::pauseTimer()
{
    if (m_timeout->isActive()) {
        m_remainingMs = std::max(1, m_timeout->remainingTime());
        m_timeout->stop();
        m_paused = true;
    }
}

void SpeechBubble::resumeTimer()
{
    if (m_paused) {
        m_paused = false;
        m_timeout->start(m_remainingMs);   // 남은 시간만큼 (interval 은 그대로)
    }
}

QRect SpeechBubble::bodyRect() const
{
    return QRect(m_bodyOrigin, m_bodySize);
}

QRect SpeechBubble::closeRect() const
{
    return m_closeLocal.translated(m_bodyOrigin);
}

QRect SpeechBubble::skipRect() const
{
    return m_skipLocal.translated(m_bodyOrigin);
}

// 문구 / 배율로 몸통 크기와 내부 영역을 다시 계산한다. 보이는 중이면 마지막 배치 기준으로 다시 배치
void SpeechBubble::relayout()
{
    const QFontMetrics body(makeFont(Config::kBubbleFontPx));
    const QFontMetrics small(makeFont(Config::kBubbleSmallFontPx));
    const int u = unit();
    const int pad = Config::kBubblePaddingPx;
    const int inset = u + pad;   // 몸통 바깥에서 내용까지

    const int textW = body.horizontalAdvance(m_text);
    const int textH = body.height();
    const int closeSide = small.height();   // × 는 정사각형 영역
    const int skipW = small.horizontalAdvance(QString::fromUtf8(Config::kBubbleSkipText));
    const int skipH = small.height();

    const int rowH = std::max(textH, closeSide);
    const int rowGap = pad / 2;
    const int contentW = std::max(textW + pad + closeSide, skipW);
    const int contentH = rowH + rowGap + skipH;

    m_textLocal = QRect(inset, inset + (rowH - textH) / 2, textW, textH);
    m_closeLocal = QRect(inset + contentW - closeSide, inset + (rowH - closeSide) / 2, closeSide, closeSide);
    m_skipLocal = QRect(inset, inset + rowH + rowGap, skipW, skipH);
    m_bodySize = QSize(contentW + 2 * inset, contentH + 2 * inset);

    if (isVisible())
        place();
    else
        applyShape(m_side, 0);   // 아직 배치 전: 크기만 (꼬리는 몸통 변 시작 쪽)
}

// 꼬리 방향 / 위치에 맞춰 창 크기, 몸통 위치, 모양 갱신. tailLat = 꼬리 가로 시작 위치 (몸통 변 안쪽으로 clamp 한다)
void SpeechBubble::applyShape(TailSide side, int tailLat)
{
    const int u = unit();
    const int n = tailLength() / u;
    const int tailW = 2 * n * u;   // 꼬리 밑변 (테두리 포함)
    const int T = tailLength();
    const bool vertical = (side == TailSide::Bottom || side == TailSide::Top);   // 꼬리가 위/아래
    m_side = side;
    switch (side) {
    case TailSide::Bottom: m_bodyOrigin = QPoint(0, 0); break;
    case TailSide::Top:    m_bodyOrigin = QPoint(0, T); break;
    case TailSide::Right:  m_bodyOrigin = QPoint(0, 0); break;
    case TailSide::Left:   m_bodyOrigin = QPoint(T, 0); break;
    }
    setFixedSize(vertical ? QSize(m_bodySize.width(), m_bodySize.height() + T)
                          : QSize(m_bodySize.width() + T, m_bodySize.height()));

    // 꼬리는 둔 모서리 칸 안쪽의 곧은 테두리 위에만 둔다
    const int start = (vertical ? m_bodyOrigin.x() : m_bodyOrigin.y()) + u;
    const int end = (vertical ? m_bodyOrigin.x() + m_bodySize.width() : m_bodyOrigin.y() + m_bodySize.height()) - u - tailW;
    m_tailLat = end < start ? (start + end) / 2 : std::clamp(tailLat, start, end);

    QVector<QRect> outer, inner;
    buildShape(outer, inner);
    QRegion shape;
    for (const QRect &r : outer)
        shape += r;
    m_shape = shape;
    setMask(shape);   // 모양 밖 투명 영역은 클릭을 받지 않고 아래 창으로 넘어간다
    update();
}

// 테두리색 사각형 (바깥 모양) + 흰색 사각형 (안쪽). 모두 칸(u) 단위 정수 좌표 → 안티앨리어싱 없이 계단 모양
void SpeechBubble::buildShape(QVector<QRect> &outer, QVector<QRect> &inner) const
{
    const int u = unit();
    const int n = tailLength() / u;
    const int bw = m_bodySize.width();
    const int bh = m_bodySize.height();
    const QPoint o = m_bodyOrigin;

    // 몸통: 네 모서리를 한 칸씩 뺀 모양 + 한 칸 안쪽 흰 사각형
    outer << QRect(o.x() + u, o.y(), bw - 2 * u, bh) << QRect(o.x(), o.y() + u, bw, bh - 2 * u);
    inner << QRect(o.x() + u, o.y() + u, bw - 2 * u, bh - 2 * u);

    // 꼬리 좌표: along = 몸통 변에서 바깥쪽으로, lat = 변을 따라 (위젯 좌표)
    auto tailRect = [&](int along, int alongLen, int lat, int latLen) {
        switch (m_side) {
        case TailSide::Bottom: return QRect(lat, o.y() + bh + along, latLen, alongLen);
        case TailSide::Top:    return QRect(lat, o.y() - along - alongLen, latLen, alongLen);
        case TailSide::Right:  return QRect(o.x() + bw + along, lat, alongLen, latLen);
        case TailSide::Left:   return QRect(o.x() - along - alongLen, lat, alongLen, latLen);
        }
        return QRect();
    };

    const int tailW = 2 * n * u;
    // 한 칸씩 좁아지는 계단: 층 k 의 바깥 너비 = tailW - 2ku, 양쪽 한 칸이 테두리
    for (int k = 0; k < n; ++k) {
        const int lat = m_tailLat + k * u;
        const int w = tailW - 2 * k * u;
        outer << tailRect(k * u, u, lat, w);
        if (w > 2 * u)
            inner << tailRect(k * u, u, lat + u, w - 2 * u);
    }
    // 몸통 테두리의 꼬리 붙는 자리를 비워 안쪽과 이어 준다
    inner << tailRect(-u, u, m_tailLat + u, tailW - 2 * u);
}

// 고양이 중심선을 가리키도록 꼬리 방향과 창 위치를 정한다 (monitor 밖으로 나가지 않게 clamp)
void SpeechBubble::place()
{
    const int u = unit();
    const int T = tailLength();
    const int gap = Config::kBubbleGapSpritePx * m_scale;
    const int bw = m_bodySize.width();
    const int bh = m_bodySize.height();

    const int cx = m_catRect.x() + m_catRect.width() / 2;
    const int cy = m_catRect.y() + m_catRect.height() / 2;

    TailSide side = TailSide::Bottom;
    QPoint pos;   // 창 왼쪽 위 (desktop)
    switch (m_gravity) {
    case CatGravity::Down: {
        // 고양이 위 (꼬리 아래). 위쪽 공간이 모자라면 고양이 아래로 뒤집는다
        const int h = bh + T;
        side = TailSide::Bottom;
        int y = m_catRect.top() - gap - h;
        if (m_monitor.isValid() && y < m_monitor.top()) {
            side = TailSide::Top;
            y = m_catRect.bottom() + 1 + gap;
        }
        pos = QPoint(cx - bw / 2, y);
        break;
    }
    case CatGravity::Left:
        // 벽이 고양이 왼쪽 → 말풍선은 오른쪽, 꼬리는 말풍선 왼쪽
        // 벽 반대쪽 공간이 모자라도 뒤집지 않고 clamp 만 한다 (고양이를 일부 가릴 수 있음, 사용자 결정)
        side = TailSide::Left;
        pos = QPoint(m_catRect.right() + 1 + gap, cy - bh / 2);
        break;
    case CatGravity::Right:
        // 벽이 고양이 오른쪽 → 말풍선은 왼쪽, 꼬리는 말풍선 오른쪽
        side = TailSide::Right;
        pos = QPoint(m_catRect.left() - gap - (bw + T), cy - bh / 2);
        break;
    }
    const bool vertical = (side == TailSide::Bottom || side == TailSide::Top);
    const QSize win = vertical ? QSize(bw, bh + T) : QSize(bw + T, bh);

    if (m_monitor.isValid()) {
        pos.setX(std::max(m_monitor.left(), std::min(pos.x(), m_monitor.right() + 1 - win.width())));
        pos.setY(std::max(m_monitor.top(), std::min(pos.y(), m_monitor.bottom() + 1 - win.height())));
    }

    // 꼬리 가로 위치: 고양이 중심선 (칸 단위로 맞춤). 몸통이 clamp 로 밀려도 고양이를 향한다
    const int n = T / u;
    const int centerLat = (vertical ? cx - pos.x() : cy - pos.y()) - n * u;
    applyShape(side, (centerLat / u) * u);

    move(pos);
#ifdef Q_OS_WIN
    // 다른 DPI 모니터를 지나며 Windows / Qt 가 위치·크기를 바꿨을 수 있어 물리 px 그대로 다시 지정 (CatOverlay::place 와 같은 이유)
    if (isVisible()) {
        HWND hwnd = toHwnd(winId());
        RECT real{};
        if (IsWindow(hwnd) && GetWindowRect(hwnd, &real)
            && QRect(real.left, real.top, real.right - real.left, real.bottom - real.top) != QRect(pos, win))
            SetWindowPos(hwnd, nullptr, pos.x(), pos.y(), win.width(), win.height(), SWP_NOACTIVATE | SWP_NOZORDER);
    }
#endif
}

SpeechBubble::Region SpeechBubble::regionAt(const QPoint &pos) const
{
    if (!m_shape.contains(pos))
        return Region::None;
    if (closeRect().contains(pos))
        return Region::Close;
    if (skipRect().contains(pos))
        return Region::Skip;
    return Region::Body;   // 꼬리 포함
}

void SpeechBubble::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), Qt::transparent);   // 알파 0 영역을 확실히 비움
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setRenderHint(QPainter::Antialiasing, false);

    QVector<QRect> outer, inner;
    buildShape(outer, inner);
    for (const QRect &r : outer)
        p.fillRect(r, kBorderColor);
    for (const QRect &r : inner)
        p.fillRect(r, kFillColor);

    const QPoint o = m_bodyOrigin;
    p.setPen(kTextColor);
    p.setFont(makeFont(Config::kBubbleFontPx));
    p.drawText(m_textLocal.translated(o), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, m_text);

    p.setFont(makeFont(Config::kBubbleSmallFontPx));
    p.setPen(m_hoverRegion == Region::Close ? kCloseHoverColor : kMutedColor);
    p.drawText(closeRect(), Qt::AlignCenter | Qt::TextSingleLine, QStringLiteral("×"));

    p.setFont(makeFont(Config::kBubbleSmallFontPx, m_hoverRegion == Region::Skip));
    p.setPen(kMutedColor);
    p.drawText(skipRect(), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
               QString::fromUtf8(Config::kBubbleSkipText));
}

void SpeechBubble::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    m_pressRegion = regionAt(event->position().toPoint());
}

void SpeechBubble::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const Region pressed = m_pressRegion;
    m_pressRegion = Region::None;
    // 누른 곳과 뗀 곳이 같은 영역일 때만 클릭
    if (pressed == Region::None || pressed != regionAt(event->position().toPoint()))
        return;
    hideBubble();
    switch (pressed) {
    case Region::Close: emit laterClicked(); break;
    case Region::Skip:  emit skipClicked(); break;
    default:            emit openClicked(); break;
    }
}

void SpeechBubble::mouseMoveEvent(QMouseEvent *event)
{
    const Region r = regionAt(event->position().toPoint());
    if (r != m_hoverRegion) {
        m_hoverRegion = r;
        update();   // 링크 밑줄 / × 색
    }
}

void SpeechBubble::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    m_hover = true;
    pauseTimer();
}

void SpeechBubble::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    m_hover = false;
    if (m_hoverRegion != Region::None) {
        m_hoverRegion = Region::None;
        update();
    }
    resumeTimer();
}

void SpeechBubble::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    applyNativeStyle(this);
}

bool SpeechBubble::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    if (DpiSizeLock::handle(eventType, message, result, size()))
        return true;
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG") {
        const MSG *msg = static_cast<const MSG *>(message);
        if (msg->message == WM_MOUSEACTIVATE) {
            *result = MA_NOACTIVATE;   // 클릭해도 활성화하지 않는다 (클릭 자체는 받는다)
            return true;
        }
    }
#endif
    return QWidget::nativeEvent(eventType, message, result);
}
