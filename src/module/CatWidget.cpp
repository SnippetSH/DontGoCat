#include "CatWidget.hpp"

#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QTransform>
#include <algorithm>

CatWidget::CatWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        advance(+1);
        if (m_playing)
            scheduleNext();
    });
}

QSize CatWidget::sizeHint() const
{
    // 벽(회전)에서는 스프라이트가 Height x Width 로 서므로 세로를 Width 만큼 잡는다
    const int w = std::max(int(CatSprite::Width), DogWidth);   // 강아지 프레임이 더 크다
    return QSize(w * 3, w) * m_scale + QSize(32, 32);
}

void CatWidget::setAnim(CatAnim anim)
{
    if (anim == m_anim)
        return;
    m_blendFrom = currentPose();
    m_anim = anim;
    m_frame = 0;
    emit frameChanged(m_frame);
    if (m_playing)
        scheduleNext();
    update();
}

void CatWidget::play()
{
    const CatAnimInfo &info = animInfo(m_anim);
    if (!info.loop && m_frame == info.frameCount() - 1)
        m_frame = 0;   // 끝난 1회성 애니메이션은 처음부터
    m_playing = true;
    emit playingChanged(true);
    scheduleNext();
    update();
}

void CatWidget::pause()
{
    m_timer.stop();
    if (!m_playing)
        return;
    m_playing = false;
    emit playingChanged(false);
}

void CatWidget::togglePlay()
{
    m_playing ? pause() : play();
}

void CatWidget::stepForward()
{
    pause();
    advance(+1);
}

void CatWidget::stepBackward()
{
    pause();
    advance(-1);
}

void CatWidget::setMoving(bool on)
{
    m_moving = on;
    if (!on)
        m_worldX = 0;
    update();
}

void CatWidget::setEyeOpen(float value)
{
    value = std::clamp(value, 0.0f, 1.0f);
    if (value == m_eyeOpen)
        return;
    m_eyeOpen = value;
    update();
}

void CatWidget::setFacing(CatFacing facing)
{
    if (facing == m_facing)
        return;
    m_facing = facing;
    update();
}

void CatWidget::setGravity(CatGravity gravity)
{
    if (gravity == m_gravity)
        return;
    m_gravity = gravity;
    m_worldX = 0;
    update();
}

void CatWidget::setPixelScale(int scale)
{
    scale = std::max(1, scale);
    if (scale == m_scale)
        return;
    m_scale = scale;
    updateGeometry();
    update();
}

void CatWidget::setAnimal(PreviewAnimal animal)
{
    if (animal == m_animal)
        return;
    m_animal = animal;
    m_blendFrom.reset();
    m_worldX = 0;
    updateGeometry();
    update();
}

void CatWidget::setSheetDir(const QString &dir)
{
    m_sheetDir = dir;
    m_sheetCache.clear();
    update();
}

void CatWidget::scheduleNext()
{
    m_timer.start(animInfo(m_anim).frameMs[m_frame]);
}

void CatWidget::advance(int dir)
{
    const CatAnimInfo &info = animInfo(m_anim);
    const int n = info.frameCount();
    int next = m_frame + dir;

    if (!info.loop && (next < 0 || next >= n)) {   // 1회성은 마지막 프레임에서 멈춤
        pause();
        return;
    }
    next = (next + n) % n;

    // 디딤발 고정: 프레임마다 정확히 speed px씩 진행 방향으로
    if (m_moving && info.speedPxPerFrame) {
        const int forward = m_facing == CatFacing::Left ? -1 : 1;
        m_worldX += dir * forward * info.speedPxPerFrame;

        // 벽에서는 세로로 움직이므로 위젯 높이를 기준으로 한다
        const int extent = m_gravity == CatGravity::Down ? width() : height();
        const int half = (extent / m_scale + CatSprite::Width) / 2;
        if (m_worldX < -half) m_worldX += 2 * half;
        if (m_worldX >  half) m_worldX -= 2 * half;
    }

    m_frame = next;
    m_blendFrom.reset();
    emit frameChanged(m_frame);
    update();
}

CatPose CatWidget::currentPose() const
{
    CatPose pose = keyPose(m_anim, m_frame);
    if (m_blendFrom)
        pose = CatPose::lerp(*m_blendFrom, pose, 0.5f);
    pose.eyeOpen = std::min(pose.eyeOpen, m_eyeOpen);
    return pose;
}

QImage CatWidget::dogFrame(QString *error)
{
    const QString dog = m_animal == PreviewAnimal::WhiteDog ? "white_dog" : "brown_dog";
    const QString path = QDir(m_sheetDir).filePath(QString("%1/small/%2_small.png").arg(dog, animInfo(m_anim).name));
    const QFileInfo fi(path);
    if (!fi.exists()) {
        *error = "시트 없음: " + QDir::toNativeSeparators(path);
        return {};
    }
    // 수정 시각을 키에 넣어 시트를 고치면 다시 읽는다
    const QString key = path + '|' + QString::number(fi.lastModified().toMSecsSinceEpoch());
    auto it = m_sheetCache.find(key);
    if (it == m_sheetCache.end())
        it = m_sheetCache.insert(key, QImage(path).convertToFormat(QImage::Format_ARGB32));
    const QImage &sheet = *it;
    const int n = animInfo(m_anim).frameCount();
    if (sheet.width() != DogWidth * n) {
        *error = QString("시트 폭 %1 != %2 x %3 프레임: %4")
                     .arg(sheet.width()).arg(DogWidth).arg(n).arg(QDir::toNativeSeparators(path));
        return {};
    }
    return sheet.copy(m_frame * DogWidth, 0, DogWidth, sheet.height());
}

int CatWidget::groundRow() const
{
    return m_animal == PreviewAnimal::Cat ? CatSprite::Height - 1 : DogGroundRow;
}

QImage CatWidget::currentSprite()
{
    if (m_animal == PreviewAnimal::Cat)
        return CatSprite::renderOriented(currentPose(), m_gravity, m_facing, m_palette);

    QString error;
    QImage img = dogFrame(&error);
    if (img.isNull())
        return img;
    if (m_facing == CatFacing::Right)
        img = img.flipped(Qt::Horizontal);
    // 매달림은 고양이와 같이 바닥 방향으로만 쓴다
    if (m_gravity == CatGravity::Down || m_anim == CatAnim::Hang)
        return img;
    QTransform t;
    t.rotate(m_gravity == CatGravity::Left ? 90 : -90);   // CatSprite::renderOriented 와 같은 방향 (Left = 시계)
    return img.transformed(t);
}

void CatWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0xCA, 0xD3, 0xDA));

    const QImage sprite = currentSprite();
    if (sprite.isNull()) {
        QString error;
        dogFrame(&error);
        painter.setPen(Qt::black);
        painter.drawText(rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap, error);
        return;
    }
    const QSize target = sprite.size() * m_scale;

    // 모든 좌표를 m_scale의 정수배로만 움직인다
    const int cx = (width() / m_scale / 2) * m_scale;
    const int top = ((height() - target.height()) / 2 / m_scale) * m_scale;
    // 진행 방향: 기본(Down)에서 왼쪽 보기는 왼쪽, 시계 방향(Left)에서는 위, 반시계 방향(Right)에서는 아래
    QPoint pos(cx - target.width() / 2, top);
    switch (m_gravity) {
    case CatGravity::Down:  pos.rx() += m_worldX * m_scale; break;
    case CatGravity::Left:  pos.ry() += m_worldX * m_scale; break;
    case CatGravity::Right: pos.ry() -= m_worldX * m_scale; break;
    }
    const QRect dst(pos, target);

    // 지면(벽) 눈금: 월드에 고정. 디딤발이 눈금 위에서 미끄러지지 않는지 확인용.
    // 접촉선 = 기본 프레임 마지막 줄(Height-1) 띠. 회전하면 Left는 왼쪽 끝 열, Right는 오른쪽 끝 열
    const QColor line(0xB4, 0xBE, 0xC8), tick(0x9A, 0xA6, 0xB2);
    const int tw = std::max(1, m_scale / 2);
    if (m_gravity == CatGravity::Down) {
        const int groundY = top + groundRow() * m_scale;
        painter.fillRect(QRect(0, groundY, width(), m_scale / 4 + 1), line);
        for (int x = cx % (4 * m_scale); x < width(); x += 4 * m_scale)
            painter.fillRect(QRect(x, groundY, tw, m_scale / 2), tick);
    } else {
        const int left = cx - target.width() / 2;   // 벽에서는 가로로 움직이지 않는다
        // 회전 전 프레임 높이 h 에서 지면선 줄 g 가 Left(시계)는 h-1-g 열, Right(반시계)는 g 열로 간다
        const int h = m_gravity == CatGravity::Left || m_gravity == CatGravity::Right ? sprite.width() : sprite.height();
        const int x0 = m_gravity == CatGravity::Left ? left + (h - 1 - groundRow()) * m_scale
                                                     : left + groundRow() * m_scale;
        painter.fillRect(QRect(x0, 0, m_scale / 4 + 1, height()), line);
        for (int y = top % (4 * m_scale); y < height(); y += 4 * m_scale)
            painter.fillRect(QRect(x0, y, m_scale / 2, tw), tick);
    }

    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(dst, sprite);
}
