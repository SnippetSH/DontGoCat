#pragma once

#include "Surface.hpp"

#include <QRect>

#include <vector>

// 플랫폼 무관 면 계산 (README 5.2). DesktopScanner(Win32 / macOS)가 창 목록만 모아 넘긴다.
// 엣지 좌표 규약 (RECT 와 동일한 반개구간):
//   창 영역 = x ∈ [left, right), y ∈ [top, bottom).  ownerRect = QRect(left, top, right-left, bottom-top)
//   지붕        : Floor,         line = top,   구간 x ∈ [left, right)   (고양이 몸: y ∈ [top-catH, top))
//   왼쪽 측면   : WallLeftSide,  line = left,  구간 y ∈ [top, bottom)   (고양이 몸: x ∈ [left-catH, left))
//   오른쪽 측면 : WallRightSide, line = right, 구간 y ∈ [top, bottom)   (고양이 몸: x ∈ [right, right+catH))
//   즉 오른쪽 측면 line 은 창 바깥 첫 열이다. 가림 판정은 창 "자신의" 픽셀 행/열
//   (지붕=top 행, 왼쪽=left 열, 오른쪽=right-1 열)이 앞 창에 덮였는지로 한다.
//   (QRect::right()/bottom() 은 inclusive 이므로 여기서는 쓰지 않고 x()+width() 를 쓴다.)
namespace SurfaceBuilder {

struct WindowRect
{
    WindowHandle handle = 0;
    QRect rect;   // 창 사각형 (Win32: DWM 확장 프레임, macOS: kCGWindowBounds)
};

// snap.monitors (floorY 확정 후) 마다 바닥 세그먼트를 추가한다.
// 가상 데스크탑 바깥쪽 끝에서는 몸 절반이 화면 밖으로 나가지 않게 안쪽으로 줄인다 (다른 모니터와 이어진 끝은 그대로)
void addMonitorFloors(DesktopSnapshot &snap, int scale);

// 창마다 지붕/좌측면/우측면 엣지 → occluders(모든 창보다 앞: 작업표시줄, 메뉴 막대 등)와 앞쪽 창에 가린 구간 제거
// → 몸이 같은 모니터 안에 들어가지 않는 구간 / 너무 짧은 구간 제거 후 snap.segments 에 추가한다.
// windows 는 z-order 앞 → 뒤.
void addWindowEdges(DesktopSnapshot &snap, const std::vector<WindowRect> &windows,
                    const std::vector<QRect> &occluders, int scale);

} // namespace SurfaceBuilder
