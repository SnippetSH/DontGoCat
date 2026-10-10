#pragma once

#include "Surface.hpp"

#include <QObject>
#include <QTimer>

// Win32 로 모니터 / 작업표시줄 / 최상위 창을 주기적으로 스캔해 DesktopSnapshot 을 만든다 (README 5.2).
// - EnumWindows z-order(앞→뒤) 순회, 창 필터, DWM 확장 프레임 사각형
// - 창마다 지붕/좌측면/우측면 엣지 → 앞쪽 창에 가린 구간 제거 → 화면 밖 / 너무 짧은 구간 제거
// - 모니터마다 바닥(작업표시줄 윗면 또는 작업 영역 하단), 전체화면 감지, 트레이 사각형
class DesktopScanner : public QObject
{
    Q_OBJECT

public:
    explicit DesktopScanner(QObject *parent = nullptr);

    // 고양이 크기(최소 세그먼트 길이, 화면 밖 판정)에 쓰이는 배율
    void setScale(int scale);

    void start();   // Config::kScanIntervalMs 주기, 시작 즉시 1회 스캔
    void stop();

    const DesktopSnapshot &snapshot() const { return m_snapshot; }
    const DesktopSnapshot &scanNow();

    // 창 하나의 현재 DWM 확장 프레임 사각형 (전체 스캔 없이). 창이 없거나 숨김/최소화면 nullopt
    static std::optional<QRect> liveWindowRect(WindowHandle hwnd);

signals:
    void scanned(const DesktopSnapshot &snapshot);

private:
    DesktopSnapshot m_snapshot;
    QTimer m_timer;
    int m_scale = 3;
};
