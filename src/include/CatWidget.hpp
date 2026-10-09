#pragma once

#include "CatAnim.hpp"

#include <QTimer>
#include <QWidget>

#include <optional>

// 고양이 애니메이션을 정수 배율 nearest-neighbor로 재생하는 위젯.
// Pixel 모드 규칙: 키포즈만 표시, 월드 위치도 정수 스프라이트 px 단위로만 이동.
class CatWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CatWidget(QWidget *parent = nullptr);

    CatAnim anim() const { return m_anim; }
    int frame() const { return m_frame; }
    bool isPlaying() const { return m_playing; }
    float eyeOpen() const { return m_eyeOpen; }
    CatFacing facing() const { return m_facing; }
    CatGravity gravity() const { return m_gravity; }
    int pixelScale() const { return m_scale; }

    QSize sizeHint() const override;

public slots:
    void setAnim(CatAnim anim);
    void play();
    void pause();
    void togglePlay();
    void stepForward();
    void stepBackward();
    void setMoving(bool on);
    void setEyeOpen(float value);   // 눈 뜸 상한 (애니메이션의 깜빡임/수면과 min)
    void setFacing(CatFacing facing);
    void setGravity(CatGravity gravity);   // 붙은 면 (바닥/오른쪽 벽/왼쪽 벽) 방향으로 회전해서 표시
    void setPixelScale(int scale);

signals:
    void frameChanged(int frame);
    void playingChanged(bool playing);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void advance(int dir);
    void scheduleNext();
    CatPose currentPose() const;

    CatAnim m_anim = CatAnim::Idle;
    int m_frame = 0;
    bool m_playing = false;
    bool m_moving = true;
    int m_worldX = 0;               // 스프라이트 px
    std::optional<CatPose> m_blendFrom;   // 애니메이션 전환 첫 프레임에만 사용

    float m_eyeOpen = 1.0f;
    CatFacing m_facing = CatFacing::Left;
    CatGravity m_gravity = CatGravity::Down;
    int m_scale = 10;
    CatPalette m_palette;
    QTimer m_timer;
};
