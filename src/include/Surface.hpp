#pragma once

#include "CatSprite.hpp"

#include <QPoint>
#include <QRect>
#include <QtGlobal>

#include <optional>
#include <vector>

// 고양이가 붙을 수 있는 면 모델 (README 5.2)
// 모든 좌표는 가상 데스크탑 물리 px.

using WindowHandle = quintptr;   // HWND 값 (헤더에 windows.h 를 노출하지 않기 위함)

enum class SurfaceKind {
    Floor,           // 수평선 y, 구간 [a, b) 는 x. 고양이는 선 위에 선다
    WallLeftSide,    // 창의 왼쪽 측면. 수직선 x, 구간 [a, b) 는 y. 고양이는 선 왼쪽 (gravity Right)
    WallRightSide,   // 창의 오른쪽 측면. 고양이는 선 오른쪽 (gravity Left)
};

CatGravity gravityFor(SurfaceKind kind);

struct SurfaceId
{
    WindowHandle owner = 0;   // 0 = 모니터 바닥 (작업표시줄 윗면 또는 작업 영역 하단)
    int monitor = -1;         // owner == 0 일 때 모니터 인덱스
    SurfaceKind kind = SurfaceKind::Floor;

    bool operator==(const SurfaceId &) const = default;
};

// 가림 처리 후 실제로 보이는 엣지 구간 하나. 한 엣지가 여러 세그먼트로 쪼개질 수 있다.
struct SurfaceSegment
{
    SurfaceId id;
    int line = 0;        // Floor: y, Wall: x
    int a = 0, b = 0;    // [a, b) — Floor: x 구간, Wall: y 구간
    QRect ownerRect;     // 소유 창 사각형 (모니터 바닥이면 모니터 사각형). offset 기준 원점

    int length() const { return b - a; }
    bool contains(int along) const { return along >= a && along < b; }
    // along 위치의 앵커 데스크탑 좌표 (Floor: (along, line), Wall: (line, along))
    QPoint pointAt(int along) const;
};

// 면 위의 한 점. offset 은 소유 사각형 원점 기준 상대 좌표
// (Floor: along - ownerRect.left(), Wall: along - ownerRect.top()).
// 상대 좌표이므로 창이 움직이면 그 위의 고양이도 따라 움직인다.
struct SurfacePoint
{
    SurfaceId surface;
    int offset = 0;
};

struct MonitorInfo
{
    QRect bounds;
    QRect workArea;
    int floorY = 0;               // 이 모니터의 바닥 y (작업표시줄 윗면 또는 작업 영역 하단)
    bool hasTaskbarFloor = false; // 하단 작업표시줄이 바닥이면 true
    bool fullscreen = false;      // 전체화면 앱이 떠 있음 → 이 모니터의 고양이는 숨김
};

struct DesktopSnapshot
{
    std::vector<MonitorInfo> monitors;
    std::vector<SurfaceSegment> segments;
    QRect virtualBounds;          // 모니터 합집합 경계
    QRect trayRect;               // TrayNotifyWnd 사각형 (트레이 아이콘 위치 대체용)
    qint64 timestampMs = 0;

    // id 의 세그먼트 중 along 을 포함하는 것. 없으면 nullptr
    const SurfaceSegment *findSegment(const SurfaceId &id, int along) const;
    std::vector<const SurfaceSegment *> segmentsOf(const SurfaceId &id) const;

    // id 의 현재 소유 사각형 (창이 사라졌으면 nullopt)
    std::optional<QRect> ownerRect(const SurfaceId &id) const;

    // 면 위 점 → 데스크탑 앵커 좌표. 면이 없거나 offset 이 세그먼트 밖이면 nullopt
    std::optional<QPoint> resolve(const SurfacePoint &p) const;

    // 데스크탑 좌표 p 에서 수직으로 떨어졌을 때 처음 만나는 바닥 (line >= p.y, x 포함)
    const SurfaceSegment *floorBelow(QPoint p) const;

    int monitorAt(QPoint p) const;   // 없으면 가장 가까운 모니터
    bool isFullscreenAt(QPoint p) const;
};
