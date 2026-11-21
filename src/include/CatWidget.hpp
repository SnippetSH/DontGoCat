#pragma once

#include "CatAnim.hpp"

#include <QHash>
#include <QImage>
#include <QTimer>
#include <QWidget>

#include <optional>

// 미리보기할 동물. 고양이는 파츠 조합(CatSprite), 강아지는 시트 이미지
// (<sheets>/<white_dog|brown_dog>/small/<동작>_small.png, 1칸=1px, 프레임 폭 40)를 잘라서 재생한다.
enum class PreviewAnimal { Cat, WhiteDog, BrownDog };

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
    void setAnimal(PreviewAnimal animal);
    void setSheetDir(const QString &dir);   // 강아지 시트 폴더 (sheets)

public:
    // 강아지 시트 규격
    static constexpr int DogWidth = 40;        // 프레임 폭 (hang 은 40x40, 나머지 40x32)
    static constexpr int DogGroundRow = 28;    // 서 있는 발 밑 외곽선(27 줄) 바로 아래 = 지면선

signals:
    void frameChanged(int frame);
    void playingChanged(bool playing);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void advance(int dir);
    void scheduleNext();
    CatPose currentPose() const;
    QImage currentSprite();                 // 회전·반전까지 적용한 현재 프레임
    QImage dogFrame(QString *error);        // 강아지 시트에서 현재 프레임만 잘라냄 (회전 전)
    int groundRow() const;                  // 기본 프레임에서 지면선 줄 (고양이 Height-1)

    PreviewAnimal m_animal = PreviewAnimal::Cat;
    QString m_sheetDir;
    QHash<QString, QImage> m_sheetCache;    // 파일 경로 -> 시트

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
