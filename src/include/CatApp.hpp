#pragma once

#include "CatBrain.hpp"
#include "CatOverlay.hpp"
#include "CatSprite.hpp"
#include "CatStats.hpp"
#include "BubbleUpdateNotifier.hpp"
#include "CatVoice.hpp"
#include "Config.hpp"
#include "DesktopScanner.hpp"
#include "Locomotion.hpp"
#include "MouseWatcher.hpp"
#include "PathPlanner.hpp"
#include "SpeechBubble.hpp"
#include "TrayController.hpp"
#include "UpdateChecker.hpp"
#include "UpdateNotifier.hpp"

#include <QAbstractNativeEventFilter>
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QTimer>

#include <memory>
#include <optional>

// 윈도우 메시지에 대한 반응 (README 5.14). 판정만 순수 함수로 분리해 테스트한다.
// 대상: WM_CLOSE / WM_QUERYENDSESSION / WM_ENDSESSION (Windows 가 아니어도 같은 값으로 판정)
enum class NativeMsgAction
{
    None,          // 건드리지 않는다 (기본 처리)
    QuitSwallow,   // 메시지를 삼키고 quit()
    HideSwallow,   // WM_SYSCOMMAND+SC_CLOSE (Alt+F4 / 시스템 메뉴 닫기): 삼키고 그 창만 hide() (종료 방해 우회 방지)
    SaveOnly,      // saveStats() 후 기본 처리 유지 (종료를 막지 않는다)
    SaveAndQuit,   // saveStats() 후 quit() (기본 처리도 유지)
};
NativeMsgAction nativeMsgAction(unsigned message, quint64 wParam);

// 모든 모듈 생성/배선, 메인 tick, 배율 설정 (README 5.10).
// 배율 우선순위: 명령줄 --scale N > QSettings(Config::kSettingsScaleKey) > Config::kDefaultScale
class CatApp : public QObject
{
    Q_OBJECT

public:
    explicit CatApp(std::optional<int> scaleOverride, QObject *parent = nullptr);
    ~CatApp() override;

    void start();   // 스캔 1회 → 주 모니터 바닥 중앙 위에서 spawn → 오버레이 표시 → 타이머 시작
    void setScale(int scale);

    // 하네스/점검용 접근자
    CatBrain &brain() { return m_brain; }
    Locomotion &body() { return m_body; }
    TrayController &tray() { return m_tray; }
    CatVoice &voice() { return m_voice; }
    CatStats &stats() { return m_stats; }

    void saveStats();   // 누적 통계를 QSettings 에 기록 (주기 타이머 + aboutToQuit)

private:
    void tick();
    void followWindow();   // Config::kFollowMs 주기: 붙어 있는 창의 실시간 위치를 몸에 반영
    void onSnapshot(const DesktopSnapshot &snapshot);
    void render();
    void onCatPressed(Qt::KeyboardModifiers modifiers);   // Shift 잡기 / 그 밖은 TrayController (README 5.13)
    void updateGrab();                                    // tick: 잡힌 동안 커서 추적, 버튼 뗌 안전장치
    void updateClickThrough();                            // tick: brainWants && !shiftHover && !grabbing (README 3.4)
    QPoint spawnPoint(const DesktopSnapshot &snapshot) const;
    QPoint trayFloorPoint() const;
    void onUpdateAvailable(const QString &version);       // 새 릴리스 알림 → 선택 처리 (README 5.15)
    BubblePlacement bubblePlacement() const;              // 지금 프레임의 고양이 이미지 사각형 / 중력 / 모니터
    void updateStats(qint64 dtMs);                        // tick: 실행/활동 시간 + 이동 거리 (README 5.14)
    double pxToMeters(double px, QPoint at) const;        // at 이 있는 모니터의 물리 DPI 로 환산 (모르면 kFallbackDpi)

    class NativeFilter;   // WM_CLOSE / 세션 종료 (README 5.14)
    std::unique_ptr<NativeFilter> m_nativeFilter;

    DesktopScanner m_scanner;
    MouseWatcher m_mouse;
    TrayController m_tray;
    CatOverlay m_overlay;
    Locomotion m_body;
    PathPlanner m_planner;
    CatBrain m_brain;
    CatVoice m_voice;
    CatStats m_stats;
    SpeechBubble m_bubble;
    UpdateChecker m_updates;
    std::unique_ptr<UpdateNotifier> m_updateNotifier;   // 표시 방식 교체 지점 (지금은 고양이 말풍선)

    QTimer m_tickTimer;
    QTimer m_statsSaveTimer;           // Config::kStatsSaveIntervalMs
    double m_lastTravelledPx = 0.0;    // Locomotion::travelledPx() 마지막 값 (차이만 거리로 더한다)
    QTimer m_followTimer;
    QElapsedTimer m_clock;
    qint64 m_lastTickMs = 0;
    int m_scale = Config::kDefaultScale;
    bool m_visible = true;
    bool m_brainClickThrough = true;   // CatBrain::wantClickThrough 마지막 값
    bool m_grabbing = false;

    // 렌더 캐시: (anim, frame, facing, gravity) 가 바뀔 때만 다시 그린다 (pose 는 anim/frame 의 키포즈라 함께 결정됨)
    QImage m_image;
    CatAnim m_lastAnim = CatAnim::Idle;
    int m_lastFrame = -1;
    CatFacing m_lastFacing = CatFacing::Left;
    CatGravity m_lastGravity = CatGravity::Down;
    QPoint m_lastAnchor;
    bool m_haveImage = false;
};
