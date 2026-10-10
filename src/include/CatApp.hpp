#pragma once

#include "CatBrain.hpp"
#include "CatOverlay.hpp"
#include "CatSprite.hpp"
#include "Config.hpp"
#include "DesktopScanner.hpp"
#include "Locomotion.hpp"
#include "MouseWatcher.hpp"
#include "PathPlanner.hpp"
#include "TrayController.hpp"

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QTimer>

#include <optional>

// 모든 모듈 생성/배선, 메인 tick, 배율 설정 (README 5.10).
// 배율 우선순위: 명령줄 --scale N > QSettings(Config::kSettingsScaleKey) > Config::kDefaultScale
class CatApp : public QObject
{
    Q_OBJECT

public:
    explicit CatApp(std::optional<int> scaleOverride, QObject *parent = nullptr);

    void start();   // 스캔 1회 → 주 모니터 바닥 중앙 위에서 spawn → 오버레이 표시 → 타이머 시작
    void setScale(int scale);

    // 하네스/점검용 접근자
    CatBrain &brain() { return m_brain; }
    Locomotion &body() { return m_body; }
    TrayController &tray() { return m_tray; }

private:
    void tick();
    void followWindow();   // Config::kFollowMs 주기: 붙어 있는 창의 실시간 위치를 몸에 반영
    void onSnapshot(const DesktopSnapshot &snapshot);
    void render();
    QPoint spawnPoint(const DesktopSnapshot &snapshot) const;
    QPoint trayFloorPoint() const;

    DesktopScanner m_scanner;
    MouseWatcher m_mouse;
    TrayController m_tray;
    CatOverlay m_overlay;
    Locomotion m_body;
    PathPlanner m_planner;
    CatBrain m_brain;

    QTimer m_tickTimer;
    QTimer m_followTimer;
    QElapsedTimer m_clock;
    qint64 m_lastTickMs = 0;
    int m_scale = Config::kDefaultScale;
    bool m_visible = true;

    // 렌더 캐시: (anim, frame, facing, gravity) 가 바뀔 때만 다시 그린다 (pose 는 anim/frame 의 키포즈라 함께 결정됨)
    QImage m_image;
    CatAnim m_lastAnim = CatAnim::Idle;
    int m_lastFrame = -1;
    CatFacing m_lastFacing = CatFacing::Left;
    CatGravity m_lastGravity = CatGravity::Down;
    QPoint m_lastAnchor;
    bool m_haveImage = false;
};
