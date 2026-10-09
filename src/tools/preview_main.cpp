#include "CatWidget.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <QTransform>

#include <cstdio>

namespace {

int exportOriented(const QString &dir);

// ccat --export <dir>: 애니메이션별 프레임 시트 PNG와 메타데이터, 지글거림 검사 결과 출력
int exportSheets(const QString &dir)
{
    QDir().mkpath(dir);
    constexpr int scale = 8;
    int failures = 0;
    for (int a = 0; a < int(CatAnim::Count); ++a) {
        const CatAnim anim = CatAnim(a);
        const CatAnimInfo &info = animInfo(anim);
        const int n = info.frameCount();

        QImage sheet(CatSprite::Width * scale * n, CatSprite::Height * scale, QImage::Format_ARGB32);
        sheet.fill(QColor(0xCA, 0xD3, 0xDA));
        QPainter p(&sheet);
        int jitter = 0;
        for (int f = 0; f < n; ++f) {
            const QImage key = CatSprite::render(keyPose(anim, f));
            p.drawImage(QRect(f * CatSprite::Width * scale, 0,
                              CatSprite::Width * scale, CatSprite::Height * scale), key);
            // poseFor를 프레임 시작 시점에서 샘플링하면 키포즈와 픽셀 단위로 같아야 한다
            if (CatSprite::render(poseFor(anim, frameStartT(anim, f))) != key)
                ++jitter;
        }
        p.end();
        sheet.save(QDir(dir).filePath(QString("%1.png").arg(info.name)));

        const QRect un = animOpaqueBounds(anim, false), in = animOpaqueBounds(anim, true);
        std::printf("%-10s frames=%d total=%dms loop=%d speed=%dpx/frame cycle=%dpx jitter=%d"
                    " union=%d,%d %dx%d inter=%d,%d %dx%d\n",
                    info.name, n, info.totalMs(), int(info.loop),
                    info.speedPxPerFrame, info.cycleDistancePx, jitter,
                    un.x(), un.y(), un.width(), un.height(), in.x(), in.y(), in.width(), in.height());
        failures += jitter;
    }
    failures += exportOriented(dir);
    return failures ? 1 : 0;
}

// renderOriented 검사: Left/Right 로 돌린 결과가 기본 프레임을 정확히 90° 돌린 것과 같은지(무손실),
// 앵커가 약속한 변 위에 있는지 확인하고, 벽타기/넘어가기 프레임을 회전시킨 시트(<dir>/oriented.png)를 낸다.
int exportOriented(const QString &dir)
{
    int failures = 0;
    for (CatFacing facing : {CatFacing::Left, CatFacing::Right})
        for (int a = 0; a < int(CatAnim::Count); ++a)
            for (int f = 0; f < animInfo(CatAnim(a)).frameCount(); ++f) {
                const CatPose pose = keyPose(CatAnim(a), f);
                const QImage base = CatSprite::render(pose, facing);
                QTransform cw, ccw;
                cw.rotate(90);
                ccw.rotate(-90);
                if (CatSprite::renderOriented(pose, CatGravity::Left, facing) != base.transformed(cw)
                    || CatSprite::renderOriented(pose, CatGravity::Right, facing) != base.transformed(ccw)
                    || CatSprite::renderOriented(pose, CatGravity::Down, facing) != base)
                    ++failures;
            }
    // 앵커 = 지면선 (Height-1) 중앙. 회전 후에는 같은 변환으로 옮겨진 점 (Left: (1, 16), Right: (23, 16))
    const int ground = CatSprite::Height - 1;
    const bool anchorOk = CatSprite::anchorIn(CatGravity::Down) == QPoint(CatSprite::Width / 2, ground)
        && CatSprite::anchorIn(CatGravity::Left) == QPoint(CatSprite::Height - ground, CatSprite::Width / 2)
        && CatSprite::anchorIn(CatGravity::Right) == QPoint(ground, CatSprite::Width / 2);
    failures += anchorOk ? 0 : 1;

    // 시트: 위 줄 Left(벽타기 8프레임, 넘어가기 3프레임), 아래 줄 Right
    constexpr int scale = 6, cell = CatSprite::Width * scale;
    const int n = animInfo(CatAnim::Climb).frameCount() + animInfo(CatAnim::Mantle).frameCount();
    QImage sheet(cell * n, cell * 2, QImage::Format_ARGB32);
    sheet.fill(QColor(0xCA, 0xD3, 0xDA));
    QPainter p(&sheet);
    for (int r = 0; r < 2; ++r) {
        const CatGravity g = r == 0 ? CatGravity::Left : CatGravity::Right;
        int col = 0;
        for (CatAnim anim : {CatAnim::Climb, CatAnim::Mantle})
            for (int f = 0; f < animInfo(anim).frameCount(); ++f, ++col) {
                // 앵커(회전된 스프라이트 안의 접촉 중앙)가 셀의 같은 변 가운데에 오도록 놓는다
                const QImage img = CatSprite::renderOriented(keyPose(anim, f), g, CatFacing::Left);
                const QPoint at(col * cell + (g == CatGravity::Left ? 0 : cell - img.width() * scale),
                                r * cell + (cell - img.height() * scale) / 2);
                p.drawImage(QRect(at, img.size() * scale), img);
                const int wall = g == CatGravity::Left ? col * cell : (col + 1) * cell - 2;
                p.fillRect(QRect(wall, r * cell, 2, cell), QColor(0x6A, 0x74, 0x80));
            }
    }
    p.end();
    sheet.save(QDir(dir).filePath("oriented.png"));
    std::printf("oriented   lossless/anchor check %s\n", failures ? "FAILED" : "ok");
    return failures;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const QStringList args = app.arguments();
    if (const qsizetype i = args.indexOf("--export"); i >= 0 && i + 1 < args.size())
        return exportSheets(args[i + 1]);

    QWidget window;
    window.setWindowTitle("CCat");

    auto *cat = new CatWidget;

    // 애니메이션 컨트롤
    auto *animBox = new QComboBox;
    for (int i = 0; i < int(CatAnim::Count); ++i)
        animBox->addItem(animInfo(CatAnim(i)).name);

    auto *prevBtn = new QPushButton("◀");
    auto *playBtn = new QPushButton("재생");
    auto *nextBtn = new QPushButton("▶");
    prevBtn->setToolTip("이전 프레임");
    nextBtn->setToolTip("다음 프레임");

    auto *frameLabel = new QLabel;
    auto *metaLabel = new QLabel;

    const auto refreshLabels = [=] {
        const CatAnimInfo &info = animInfo(cat->anim());
        const int f = cat->frame();
        frameLabel->setText(QString("프레임 %1/%2 · %3ms")
                                .arg(f + 1).arg(info.frameCount()).arg(info.frameMs[f]));
        metaLabel->setText(info.cycleDistancePx
            ? QString("1주기 이동 %1px (%2px/프레임)")
                  .arg(info.cycleDistancePx).arg(info.speedPxPerFrame)
            : QString("제자리"));
    };

    QObject::connect(animBox, &QComboBox::currentIndexChanged, cat, [=](int i) {
        cat->setAnim(CatAnim(i));
        refreshLabels();
    });
    QObject::connect(playBtn, &QPushButton::clicked, cat, &CatWidget::togglePlay);
    QObject::connect(prevBtn, &QPushButton::clicked, cat, &CatWidget::stepBackward);
    QObject::connect(nextBtn, &QPushButton::clicked, cat, &CatWidget::stepForward);
    QObject::connect(cat, &CatWidget::frameChanged, frameLabel, refreshLabels);
    QObject::connect(cat, &CatWidget::playingChanged, playBtn, [=](bool on) {
        playBtn->setText(on ? "정지" : "재생");
    });

    auto *animRow = new QHBoxLayout;
    animRow->addWidget(animBox);
    animRow->addWidget(prevBtn);
    animRow->addWidget(playBtn);
    animRow->addWidget(nextBtn);
    animRow->addWidget(frameLabel);
    animRow->addStretch(1);
    animRow->addWidget(metaLabel);

    // 표시 옵션
    auto *eyeSlider = new QSlider(Qt::Horizontal);
    eyeSlider->setRange(0, 100);
    eyeSlider->setValue(100);
    auto *eyeLabel = new QLabel("eyeOpen 1.00");

    auto *flip = new QCheckBox("오른쪽 보기");
    auto *gravityBox = new QComboBox;
    gravityBox->addItem("바닥 (Down)");
    gravityBox->addItem("오른쪽 벽 (Left, 시계 90°)");
    gravityBox->addItem("왼쪽 벽 (Right, 반시계 90°)");
    gravityBox->setToolTip("붙은 면. 벽타기(climb) 회전 확인용");
    auto *move = new QCheckBox("이동");
    move->setChecked(true);

    auto *scale = new QSpinBox;
    scale->setRange(1, 32);
    scale->setValue(cat->pixelScale());
    scale->setSuffix("x");

    QObject::connect(eyeSlider, &QSlider::valueChanged, cat, [=](int v) {
        cat->setEyeOpen(v / 100.0f);
        eyeLabel->setText(QString("eyeOpen %1").arg(v / 100.0, 0, 'f', 2));
    });
    QObject::connect(flip, &QCheckBox::toggled, cat, [=](bool on) {
        cat->setFacing(on ? CatFacing::Right : CatFacing::Left);
    });
    QObject::connect(gravityBox, &QComboBox::currentIndexChanged, cat, [=](int i) {
        constexpr CatGravity gravities[] = {CatGravity::Down, CatGravity::Left, CatGravity::Right};
        cat->setGravity(gravities[i]);
    });
    QObject::connect(move, &QCheckBox::toggled, cat, &CatWidget::setMoving);
    QObject::connect(scale, &QSpinBox::valueChanged, cat, &CatWidget::setPixelScale);

    auto *optionRow = new QHBoxLayout;
    optionRow->addWidget(eyeLabel);
    optionRow->addWidget(eyeSlider, 1);
    optionRow->addWidget(flip);
    optionRow->addWidget(gravityBox);
    optionRow->addWidget(move);
    optionRow->addWidget(new QLabel("배율"));
    optionRow->addWidget(scale);

    auto *layout = new QVBoxLayout(&window);
    layout->addWidget(cat, 1);
    layout->addLayout(animRow);
    layout->addLayout(optionRow);

    refreshLabels();
    window.show();
    return app.exec();
}
