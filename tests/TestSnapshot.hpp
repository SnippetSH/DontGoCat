#pragma once

// 합성 DesktopSnapshot 헬퍼 (실제 데스크탑을 스캔하지 않는다). 1920x1080 모니터, 작업표시줄 윗면 y=1040.

#include "Surface.hpp"

#include <QRect>

inline constexpr int kFloorY = 1040;

// 창 하나를 지붕(Floor) + 좌/우 벽 세그먼트로 추가
inline void addWindow(DesktopSnapshot &s, quintptr h, QRect r)
{
    auto make = [&](SurfaceKind k, int line, int a, int b) {
        SurfaceSegment seg;
        seg.id.owner = h;
        seg.id.kind = k;
        seg.line = line;
        seg.a = a;
        seg.b = b;
        seg.ownerRect = r;
        s.segments.push_back(seg);
    };
    make(SurfaceKind::Floor, r.top(), r.left(), r.left() + r.width());
    make(SurfaceKind::WallLeftSide, r.left(), r.top(), r.top() + r.height());
    make(SurfaceKind::WallRightSide, r.left() + r.width(), r.top(), r.top() + r.height());
}

// 모니터 1개 + 작업표시줄 바닥. floorInset: 바닥 양 끝에서 안쪽으로 줄이는 폭 (0 이면 모니터 전폭)
inline DesktopSnapshot baseSnap(int floorInset = 0)
{
    DesktopSnapshot s;
    MonitorInfo m;
    m.bounds = QRect(0, 0, 1920, 1080);
    m.workArea = QRect(0, 0, 1920, kFloorY);
    m.floorY = kFloorY;
    m.hasTaskbarFloor = true;
    s.monitors.push_back(m);
    s.virtualBounds = m.bounds;
    SurfaceSegment f;
    f.id.owner = 0;
    f.id.monitor = 0;
    f.id.kind = SurfaceKind::Floor;
    f.line = kFloorY;
    f.a = floorInset;
    f.b = 1920 - floorInset;
    f.ownerRect = m.bounds;
    s.segments.push_back(f);
    return s;
}
