#include "QuitPopup.hpp"

#include "Config.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QEvent>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

// 팝업 크기/여백은 물리 px (QT_ENABLE_HIGHDPI_SCALING=0), 글꼴은 pt 로 지정
constexpr int kPadPx = 10;               // 좌우/상단 여백
constexpr int kGapPx = 8;                // 행 사이 간격
constexpr int kIconPx = 24;              // 상단 얼굴 아이콘 (tray_face_24.png 를 그대로 사용)
constexpr int kScaleButtonH = 24;
constexpr int kScaleButtonW = 36;
constexpr int kScaleButtonGap = 4;
constexpr int kBorderPx = 1;
constexpr int kFontPt = 9;
constexpr int kQuitFontPx = 12;            // 종료 버튼 글자 크기 (버튼 높이 16px 에 맞춤)

const char *kStyleSheet =
    "QLabel#title { color: #311410; font-weight: bold; }"
    "QPushButton#scale { background: #FDF5E3; color: #311410; border: 1px solid #311410;"
    "                    border-radius: 0; padding: 0; }"
    "QPushButton#scale:hover { background: #FDE3B4; }"
    "QPushButton#scale:checked { background: #F9BD61; font-weight: bold; }"
    "QCheckBox#autostart { color: #311410; spacing: 6px; }"
    "QPushButton#quit { background: #D9534F; color: #FFFFFF; border: none; border-radius: 0;"
    "                   padding: 0; font-weight: bold; }"
    "QPushButton#quit:hover { background: #E26A5A; }"
    "QPushButton#quit:pressed { background: #B94540; }";

} // namespace

QuitPopup::QuitPopup(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setFocusPolicy(Qt::StrongFocus);   // Esc 를 받으려면 팝업이 포커스를 가져야 한다 (버튼은 NoFocus)
    setStyleSheet(QString::fromUtf8(kStyleSheet));

    QFont font;
    font.setFamilies({QStringLiteral("Segoe UI"), QStringLiteral("Malgun Gothic")});
    font.setPointSize(kFontPt);
    setFont(font);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(kPadPx + kBorderPx, kPadPx, kPadPx + kBorderPx, 0);   // 아래 여백 0: 버튼이 바닥에 닿는다
    root->setSpacing(kGapPx);

    // 상단: 얼굴 아이콘 + "DontGoCat"
    auto *header = new QHBoxLayout;
    header->setSpacing(kGapPx);
    auto *icon = new QLabel(this);
    icon->setPixmap(faceIcon().pixmap(kIconPx, kIconPx));
    icon->setFixedSize(kIconPx, kIconPx);
    auto *title = new QLabel(QStringLiteral("DontGoCat"), this);
    title->setObjectName(QStringLiteral("title"));
    header->addWidget(icon);
    header->addWidget(title);
    header->addStretch();
    root->addLayout(header);

    // 중단: 배율 1x~4x (배타 선택)
    auto *scales = new QHBoxLayout;
    scales->setSpacing(kScaleButtonGap);
    m_scaleGroup = new QButtonGroup(this);
    m_scaleGroup->setExclusive(true);
    for (int s = Config::kMinScale; s <= Config::kMaxScale; ++s) {
        auto *button = new QPushButton(QStringLiteral("%1x").arg(s), this);
        button->setObjectName(QStringLiteral("scale"));
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedSize(kScaleButtonW, kScaleButtonH);
        m_scaleGroup->addButton(button, s);
        scales->addWidget(button);
    }
    root->addLayout(scales);
    connect(m_scaleGroup, &QButtonGroup::idClicked, this, [this](int s) {
        setScale(s);
        emit scaleSelected(s);
    });

    // 자동 시작 체크박스 (표시 전에 TrayController 가 레지스트리 상태로 맞춘다)
    m_autoStart = new QCheckBox(QString::fromUtf8("윈도우 시작 시 실행"), this);
    m_autoStart->setObjectName(QStringLiteral("autostart"));
    m_autoStart->setFocusPolicy(Qt::NoFocus);
    root->addWidget(m_autoStart);
    connect(m_autoStart, &QCheckBox::clicked, this, &QuitPopup::autoStartToggled);

    // 하단: 종료 버튼 (Config 고정 크기, 배율과 무관)
    m_quitButton = new QPushButton(QStringLiteral("종료"), this);
    m_quitButton->setObjectName(QStringLiteral("quit"));
    m_quitButton->setFocusPolicy(Qt::NoFocus);
    m_quitButton->setFixedSize(Config::kQuitButtonW, Config::kQuitButtonH);
    QFont quitFont = m_quitButton->font();
    quitFont.setPixelSize(kQuitFontPx);
    m_quitButton->setFont(quitFont);
    m_quitButton->installEventFilter(this);
    root->addWidget(m_quitButton, 0, Qt::AlignHCenter);
    connect(m_quitButton, &QPushButton::clicked, this, &QuitPopup::quitClicked);

    // 앱 전체가 비활성화되면 닫는다 (고양이 오버레이는 활성화되지 않으므로 클릭해도 닫히지 않는다)
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive)
            closeIfDeactivated();
    });

    setScale(m_scale);
}

void QuitPopup::setAutoStartChecked(bool checked)
{
    m_autoStart->setChecked(checked);   // clicked 는 사용자 조작에만 발생하므로 시그널이 나가지 않는다
}

bool QuitPopup::autoStartChecked() const
{
    return m_autoStart->isChecked();
}

QIcon QuitPopup::faceIcon()
{
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64})
        icon.addFile(QStringLiteral(":/icons/tray_face_%1.png").arg(size), QSize(size, size));
    return icon;
}

void QuitPopup::setScale(int scale)
{
    m_scale = qBound(Config::kMinScale, scale, Config::kMaxScale);
    if (auto *b = m_scaleGroup->button(m_scale))
        b->setChecked(true);
    relayout();
}

void QuitPopup::relayout()
{
    layout()->invalidate();
    layout()->activate();
    setFixedSize(layout()->sizeHint());

    if (m_hasPlacement) {
        // 하단 = 바닥 y, 가로는 앵커(트레이 아이콘) 중심, 모니터 안으로 clamp
        int x = m_anchorRect.center().x() - width() / 2;
        x = qMin(x, m_monitor.right() + 1 - width());
        x = qMax(x, m_monitor.left());
        move(x, m_floorY - height());
    }
}

void QuitPopup::showAt(const QRect &anchorRect, int floorY, const QRect &monitor)
{
    m_anchorRect = anchorRect;
    m_floorY = floorY;
    m_monitor = monitor;
    m_hasPlacement = true;
    m_wasActive = false;
    relayout();
    show();
    raise();
    activateWindow();
    setFocus();
}

QRect QuitPopup::quitButtonDesktopRect() const
{
    return QRect(m_quitButton->mapToGlobal(QPoint(0, 0)), m_quitButton->size());
}

void QuitPopup::closeIfDeactivated()
{
    if (isVisible() && m_wasActive)
        hide();
}

bool QuitPopup::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_quitButton && event->type() == QEvent::Enter)
        emit quitHovered();
    return QWidget::eventFilter(watched, event);
}

void QuitPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QWidget::keyPressEvent(event);
}

void QuitPopup::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::ActivationChange) {
        if (isActiveWindow())
            m_wasActive = true;
        else
            closeIfDeactivated();
    }
}

void QuitPopup::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_wasActive = false;
    emit closed();
}

void QuitPopup::paintEvent(QPaintEvent *)
{
    // 납작한 크림색 면 + 위/좌/우 외곽선 (아래는 바닥에 닿으므로 선 없음)
    QPainter p(this);
    p.fillRect(rect(), QColor(0xFF, 0xF8, 0xEC));
    p.fillRect(QRect(0, 0, width(), kBorderPx), QColor(0x31, 0x14, 0x10));
    p.fillRect(QRect(0, 0, kBorderPx, height()), QColor(0x31, 0x14, 0x10));
    p.fillRect(QRect(width() - kBorderPx, 0, kBorderPx, height()), QColor(0x31, 0x14, 0x10));
}
