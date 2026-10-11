#include "QuitPopup.hpp"

#include "CatStats.hpp"
#include "Config.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <iterator>

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
constexpr int kVolumeStep = 5;             // 음량 슬라이더 휠 / 키 단위 (%)
constexpr int kVolumeCommitMs = 150;       // 음량 변경이 이 시간 멈추면 확정 (끄는 중에는 놓을 때까지 미룸)
constexpr int kQuitFontPx = 12;            // 종료 버튼 글자 크기 (버튼 높이 16px 에 맞춤)
constexpr int kToggleH = 14;               // 통계 토글 버튼 높이
constexpr int kStatColGapPx = 16;          // 통계 이름 열과 값 열 사이 간격
constexpr int kStatRowGapPx = 2;

const char *kStyleSheet =
    "QLabel#title { color: #311410; font-weight: bold; }"
    "QPushButton#scale { background: #FDF5E3; color: #311410; border: 1px solid #311410;"
    "                    border-radius: 0; padding: 0; }"
    "QPushButton#scale:hover { background: #FDE3B4; }"
    "QPushButton#scale:checked { background: #F9BD61; font-weight: bold; }"
    "QCheckBox#autostart, QCheckBox#sound { color: #311410; spacing: 6px; }"
    "QLabel#volumeLabel { color: #311410; }"
    "QLabel#volumeLabel:disabled { color: #A89A90; }"
    "QPushButton#quit { background: #D9534F; color: #FFFFFF; border: none; border-radius: 0;"
    "                   padding: 0; font-weight: bold; }"
    "QPushButton#quit:hover { background: #E26A5A; }"
    "QPushButton#quit:pressed { background: #B94540; }"
    "QPushButton#statsToggle { background: transparent; color: #311410; border: none; padding: 0;"
    "                          font-size: 9px; }"
    "QPushButton#statsToggle:hover { background: #FDE3B4; }"
    "QWidget#statsPanel QLabel { color: #311410; }"
    "QFrame#statsSep { background: rgba(49, 20, 16, 77); border: none; }";

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
    root->setContentsMargins(kPadPx + kBorderPx, kBorderPx, kPadPx + kBorderPx, 0);   // 위는 외곽선만, 아래 여백 0: 버튼이 바닥에 닿는다
    root->setSpacing(kGapPx);
    root->setSizeConstraint(QLayout::SetNoConstraint);   // 크기는 relayout() 이 정한다 (값이 길어져도 레이아웃이 최소 크기로 창을 밀지 않게)

    // 맨 위: 통계 토글 (▲ 접힘 / ▼ 펼침) + 통계 패널 (접힘 동안 숨김)
    m_statsToggle = new QPushButton(QString::fromUtf8("▲"), this);
    m_statsToggle->setObjectName(QStringLiteral("statsToggle"));
    m_statsToggle->setFocusPolicy(Qt::NoFocus);
    m_statsToggle->setFlat(true);
    m_statsToggle->setFixedHeight(kToggleH);
    root->addWidget(m_statsToggle);
    connect(m_statsToggle, &QPushButton::clicked, this, [this]() { setStatsExpanded(!statsExpanded()); });

    m_statsPanel = buildStatsPanel();
    m_statsPanel->hide();
    root->addWidget(m_statsPanel);

    m_statsRefresh = new QTimer(this);
    m_statsRefresh->setInterval(Config::kStatsRefreshMs);
    connect(m_statsRefresh, &QTimer::timeout, this, &QuitPopup::refreshStats);

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

    // 소리 체크박스 (표시 전에 TrayController 가 현재 설정으로 맞춘다)
    m_sound = new QCheckBox(QString::fromUtf8("고양이 소리"), this);
    m_sound->setObjectName(QStringLiteral("sound"));
    m_sound->setFocusPolicy(Qt::NoFocus);
    m_sound->setChecked(true);
    root->addWidget(m_sound);
    connect(m_sound, &QCheckBox::clicked, this, &QuitPopup::soundToggled);

    // 음량 슬라이더 (0~100%). 소리가 꺼져 있으면 비활성화.
    // 트랙 클릭은 핸들을 바로 옮긴 뒤(valueChanged) 놓기(sliderReleased)가 또 오므로, 변경이 잠시 멈춘 뒤
    // 마지막 확정값과 다를 때만 한 번 알린다 (미리듣기가 겹쳐 재생되지 않게)
    auto *volumeRow = new QHBoxLayout;
    volumeRow->setSpacing(kGapPx);
    auto *volumeLabel = new QLabel(QString::fromUtf8("음량"), this);
    volumeLabel->setObjectName(QStringLiteral("volumeLabel"));
    m_volume = new QSlider(Qt::Horizontal, this);
    m_volume->setObjectName(QStringLiteral("volume"));
    m_volume->setFocusPolicy(Qt::NoFocus);
    m_volume->setRange(0, 100);
    m_volume->setSingleStep(kVolumeStep);
    m_volume->setPageStep(kVolumeStep * 2);
    m_volume->setValue(Config::kMeowVolume);
    m_committedVolume = Config::kMeowVolume;
    volumeRow->addWidget(volumeLabel);
    volumeRow->addWidget(m_volume, 1);
    root->addLayout(volumeRow);
    m_volumeCommit = new QTimer(this);
    m_volumeCommit->setSingleShot(true);
    m_volumeCommit->setInterval(kVolumeCommitMs);
    connect(m_volumeCommit, &QTimer::timeout, this, [this]() {
        if (m_volume->isSliderDown())
            return;   // 아직 끄는 중: 놓을 때 다시 예약된다
        const int v = m_volume->value();
        if (v == m_committedVolume)
            return;
        m_committedVolume = v;
        emit volumeSelected(v);
    });
    connect(m_volume, &QSlider::valueChanged, m_volumeCommit, qOverload<>(&QTimer::start));
    connect(m_volume, &QSlider::sliderReleased, m_volumeCommit, qOverload<>(&QTimer::start));
    connect(m_sound, &QCheckBox::toggled, this, [this, volumeLabel](bool on) {
        m_volume->setEnabled(on);
        volumeLabel->setEnabled(on);
    });

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

void QuitPopup::setSoundChecked(bool checked)
{
    m_sound->setChecked(checked);
}

bool QuitPopup::soundChecked() const
{
    return m_sound->isChecked();
}

void QuitPopup::setVolume(int percent)
{
    const QSignalBlocker block(m_volume);
    m_volume->setValue(percent);
    m_committedVolume = m_volume->value();
    m_volumeCommit->stop();
}

int QuitPopup::volume() const
{
    return m_volume->value();
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

void QuitPopup::relayout(bool keepWidth)
{
    // 폭 = max(접힌 내용 폭, 통계 패널 폭) 을 접힘/펼침과 무관하게 쓴다 → 폭, x, 종료 버튼 사각형이 토글로 바뀌지 않는다
    const bool expanded = m_statsPanel->isVisibleTo(this);
    m_statsPanel->hide();
    layout()->invalidate();
    layout()->activate();
    const QSize collapsed = layout()->sizeHint();
    int w = qMax(collapsed.width(), m_statsPanel->sizeHint().width() + 2 * (kPadPx + kBorderPx));
    int h = collapsed.height();
    if (expanded) {
        m_statsPanel->show();
        layout()->invalidate();
        layout()->activate();
        h = layout()->sizeHint().height();
    }
    if (keepWidth && isVisible())
        w = width();   // 표시 중에는 폭을 바꾸지 않는다 (값이 길어져도 잘릴 뿐, 다음 showAt 에서 다시 정함)
    setFixedSize(w, h);

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
    setStatsExpanded(false);   // 열릴 때마다 접힌 상태로 시작
    refreshStats();            // 접힌 폭에도 패널 폭이 반영되도록 현재 값으로 맞춘 뒤 배치
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
    m_statsRefresh->stop();
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

// ── 통계 영역 (README 5.14) ──────────────────────────────

namespace {

struct StatRow
{
    const char *key;     // objectName 접미사
    const char *name;    // 왼쪽 이름
};

const StatRow kTotalRows[] = {
    {"runMs", "실행 시간"},          {"roamMs", "혼자 돌아다닌 시간"}, {"sleepMs", "잠잔 시간"},
    {"playMs", "마우스와 논 시간"},  {"nudges", "툭툭 친 횟수"},       {"quit", "종료 방해"},
    {"grabs", "잡힌 횟수"},          {"distanceM", "이동 거리"},       {"jumpFall", "점프 / 낙하"},
};
const StatRow kSessionRows[] = {
    {"runMs", "실행 시간"}, {"sleepMs", "잠잔 시간"}, {"nudges", "툭툭 친 횟수"},
    {"quitBlocks", "종료 방해"}, {"grabs", "잡힌 횟수"}, {"distanceM", "이동 거리"},
};

} // namespace

QWidget *QuitPopup::buildStatsPanel()
{
    auto *panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("statsPanel"));
    auto *grid = new QGridLayout(panel);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(kStatColGapPx);
    grid->setVerticalSpacing(kStatRowGapPx);
    grid->setColumnStretch(0, 1);

    int row = 0;
    auto addSection = [&](const QString &object, const QString &text) {
        auto *title = new QLabel(text, panel);
        title->setObjectName(object);
        QFont f = title->font();
        f.setBold(true);
        title->setFont(f);
        grid->addWidget(title, row++, 0, 1, 2);
    };
    auto addRows = [&](const char *prefix, const StatRow *rows, int n) {
        for (int i = 0; i < n; ++i) {
            auto *name = new QLabel(QString::fromUtf8(rows[i].name), panel);
            auto *value = new QLabel(QStringLiteral("-"), panel);
            value->setObjectName(QStringLiteral("%1%2").arg(QLatin1String(prefix), QLatin1String(rows[i].key)));
            value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            grid->addWidget(name, row, 0);
            grid->addWidget(value, row, 1);
            ++row;
        }
    };
    auto addSeparator = [&]() {
        auto *line = new QFrame(panel);
        line->setObjectName(QStringLiteral("statsSep"));
        line->setFixedHeight(1);
        grid->addWidget(line, row++, 0, 1, 2);
    };

    addSection(QStringLiteral("stat_since"), QString::fromUtf8("누적"));
    addRows("stat_total_", kTotalRows, int(std::size(kTotalRows)));
    addSeparator();
    addSection(QStringLiteral("stat_sessionTitle"), QString::fromUtf8("이번 실행"));
    addRows("stat_session_", kSessionRows, int(std::size(kSessionRows)));
    addSeparator();
    return panel;
}

void QuitPopup::setStatsSource(const CatStats *stats)
{
    m_stats = stats;
    refreshStats();
}

void QuitPopup::setStatsExpanded(bool expanded)
{
    if (expanded == statsExpanded())
        return;
    m_statsToggle->setText(QString::fromUtf8(expanded ? "▼" : "▲"));
    if (expanded) {
        m_statsPanel->show();
        refreshStats();   // 펼치는 즉시 현재 값
        if (isVisible())
            m_statsRefresh->start();
    } else {
        m_statsPanel->hide();
        m_statsRefresh->stop();
    }
    relayout(true);
}

bool QuitPopup::statsExpanded() const
{
    return m_statsPanel && m_statsPanel->isVisibleTo(this);
}

void QuitPopup::refreshStats()
{
    if (!m_statsPanel)
        return;
    auto set = [this](const QString &object, const QString &text) {
        if (auto *l = m_statsPanel->findChild<QLabel *>(object)) {
            if (l->text() != text)
                l->setText(text);
        }
    };
    const QString dash = QStringLiteral("-");
    if (!m_stats) {
        set(QStringLiteral("stat_since"), QString::fromUtf8("누적"));
        for (const StatRow &r : kTotalRows)
            set(QStringLiteral("stat_total_") + QLatin1String(r.key), dash);
        for (const StatRow &r : kSessionRows)
            set(QStringLiteral("stat_session_") + QLatin1String(r.key), dash);
        return;
    }

    const CatStatsValues &t = m_stats->total();
    const CatStatsValues &s = m_stats->session();
    using S = CatStats;
    const QString sinceText = m_stats->since().isValid()
        ? QString::fromUtf8("누적 (%1 부터)").arg(m_stats->since().toString(QStringLiteral("yyyy-MM-dd")))
        : QString::fromUtf8("누적");
    set(QStringLiteral("stat_since"), sinceText);
    set(QStringLiteral("stat_total_runMs"), S::formatDuration(t.runMs));
    set(QStringLiteral("stat_total_roamMs"), S::formatDuration(t.roamMs));
    set(QStringLiteral("stat_total_sleepMs"), S::formatDuration(t.sleepMs));
    set(QStringLiteral("stat_total_playMs"), S::formatDuration(t.playMs));
    set(QStringLiteral("stat_total_nudges"), S::formatCount(t.nudges));
    set(QStringLiteral("stat_total_quit"),
        QString::fromUtf8("%1 / %2 (%3)")
            .arg(S::formatCount(t.quitBlocks), S::formatCount(t.quitAttempts), S::formatRate(t.quitBlocks, t.quitAttempts)));
    set(QStringLiteral("stat_total_grabs"), S::formatCount(t.grabs));
    set(QStringLiteral("stat_total_distanceM"), S::formatDistance(t.distanceM));
    set(QStringLiteral("stat_total_jumpFall"), QString::fromUtf8("%1 / %2").arg(S::formatCount(t.jumps), S::formatCount(t.falls)));
    set(QStringLiteral("stat_session_runMs"), S::formatDuration(s.runMs));
    set(QStringLiteral("stat_session_sleepMs"), S::formatDuration(s.sleepMs));
    set(QStringLiteral("stat_session_nudges"), S::formatCount(s.nudges));
    set(QStringLiteral("stat_session_quitBlocks"), S::formatCount(s.quitBlocks));
    set(QStringLiteral("stat_session_grabs"), S::formatCount(s.grabs));
    set(QStringLiteral("stat_session_distanceM"), S::formatDistance(s.distanceM));
}
