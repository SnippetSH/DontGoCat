#pragma once

#include "QuitGuard.hpp"
#include "Surface.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QRect>
#include <QString>
#include <QSystemTrayIcon>

class QuitPopup;

// 트레이 아이콘 + QuitPopup + QuitGuard 를 묶어 종료 방해 흐름을 관리 (README 5.6).
//   1. 종료 버튼 hover 진입 (방해 중 아님) → attempt() → 방해면 blockRequested
//   2. 방해 중 고양이 몸 클릭 (onCatClicked) → attempt() 재판정 → 유지면 blockReact, 실패면 blockReleased
//   3. 종료 버튼 클릭 (고양이가 덮지 않은 보이는 부분) → 방해 여부와 무관하게 quitRequested (재판정 없음)
//   4. Shift+고양이 클릭 → 즉시 quitRequested
//   5. 팝업 닫힘 → blockReleased (방해 중이었다면)
class TrayController : public QObject
{
    Q_OBJECT

public:
    // rng: QuitGuard 난수 (테스트 주입용, 비면 QRandomGenerator::global())
    explicit TrayController(QObject *parent = nullptr, QuitGuard::Rng rng = {});
    ~TrayController() override;

    void setScale(int scale);
    void updateSnapshot(const DesktopSnapshot &snapshot);   // 바닥 y, 트레이 사각형 대체값
    void setAutoStartKey(const QString &runKey) { m_autoStartKey = runKey; }   // 자동 시작 레지스트리 키 (테스트용 대체, 비면 실제 Run 키)
    void setSoundEnabled(bool enabled) { m_soundEnabled = enabled; }   // 팝업 소리 체크박스에 표시할 현재 설정

    QRect trayRect() const;          // 트레이 아이콘 geometry, 비었으면 snapshot.trayRect
    int trayFloorY() const;          // 트레이가 있는 모니터의 바닥 y
    bool isPopupOpen() const;
    bool isBlocking() const { return m_blocking; }
    QRect quitButtonRect() const;    // 데스크탑 좌표
    QRect popupRect() const;         // 팝업이 열려 있을 때의 전체 사각형, 닫혀 있으면 null

public slots:
    void togglePopup();                                   // 트레이 아이콘 클릭과 동일 (테스트에서 직접 호출)
    void releaseBlock();                                  // 고양이가 방해를 포기함(도달 불가 등) → blockReleased
    void onCursor(QPoint pos);                            // 트레이 접근 판정
    void onCatClicked(Qt::KeyboardModifiers modifiers);   // 고양이 몸 클릭 (방해 중이면 재시도)

signals:
    void trayApproach(bool near);
    void blockRequested(QRect quitButtonDesktop);
    void blockReact();               // 방해 유지 판정 → 고양이 반응 (paw_swipe)
    void blockReleased();            // 방해 실패 / 팝업 닫힘 → 고양이 비켜 앉음
    void scaleChanged(int scale);
    void soundChanged(bool enabled);   // 사용자가 팝업에서 소리를 켜거나 껐다
    void quitRequested();

private:
    void onQuitHovered();
    void onQuitClicked();

    QSystemTrayIcon m_tray;
    QuitPopup *m_popup = nullptr;    // 소유 (최상위 위젯이므로 소멸자에서 delete)
    QuitGuard m_guard;
    QElapsedTimer m_clock;
    DesktopSnapshot m_snapshot;
    QElapsedTimer m_leftSince;
    QString m_autoStartKey;
    int m_scale = 3;
    bool m_blocking = false;
    bool m_soundEnabled = true;
    bool m_near = false;
    qint64 m_popupClosedMs = -1;     // 팝업이 마지막으로 닫힌 시각 (m_clock 기준)
    mutable QRect m_trayRectCache;
    mutable qint64 m_trayRectCacheMs = -1;
};
