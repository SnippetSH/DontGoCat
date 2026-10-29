// SurfaceBuilder (플랫폼 공통 면 계산): 모니터 바닥, 창 엣지, 앞 창 / occluder 가림, 모니터 경계, 짧은 구간 제거.
// 합성 모니터 / 창 사각형만 쓰며 실제 데스크탑은 스캔하지 않는다. scale 1 (몸 두께 24, 최소 길이 24, 몸 반폭 12)

#include "TestCheck.hpp"

#include "CatSprite.hpp"
#include "Config.hpp"
#include "SurfaceBuilder.hpp"

#include <cstdio>
#include <vector>

namespace {

constexpr int kScale = 1;
constexpr int kCatH = CatSprite::Height * kScale;
constexpr int kMinLen = Config::kMinSegmentSpritePx * kScale;
constexpr int kHalfBody = Config::kBodyHalfWidthSpritePx * kScale;

DesktopSnapshot monitors(std::vector<QRect> rects)
{
    DesktopSnapshot s;
    for (const QRect &r : rects) {
        MonitorInfo m;
        m.bounds = r;
        m.workArea = r;
        m.floorY = r.y() + r.height();
        s.monitors.push_back(m);
        s.virtualBounds = s.virtualBounds.united(r);
    }
    return s;
}

std::vector<const SurfaceSegment *> find(const DesktopSnapshot &s, WindowHandle h, SurfaceKind kind)
{
    std::vector<const SurfaceSegment *> out;
    for (const SurfaceSegment &seg : s.segments) {
        if (seg.id.owner == h && seg.id.kind == kind)
            out.push_back(&seg);
    }
    return out;
}

} // namespace

static void testMonitorFloors()
{
    std::printf("[1] monitor floors\n");
    DesktopSnapshot s = monitors({QRect(0, 0, 1920, 1080)});
    SurfaceBuilder::addMonitorFloors(s, kScale);
    CHECK(s.segments.size() == 1, "one floor (%zu)", s.segments.size());
    const SurfaceSegment &f = s.segments[0];
    CHECK(f.id.owner == 0 && f.id.monitor == 0 && f.line == 1080, "floor id / line (%d)", f.line);
    CHECK(f.a == kHalfBody && f.b == 1920 - kHalfBody, "outer ends inset by half body [%d,%d)", f.a, f.b);

    // 나란한 두 모니터: 맞닿은 끝은 줄이지 않는다
    DesktopSnapshot d = monitors({QRect(0, 0, 1920, 1080), QRect(1920, 0, 1920, 1080)});
    SurfaceBuilder::addMonitorFloors(d, kScale);
    CHECK(d.segments.size() == 2, "two floors");
    CHECK(d.segments[0].a == kHalfBody && d.segments[0].b == 1920, "left monitor joined end [%d,%d)", d.segments[0].a,
          d.segments[0].b);
    CHECK(d.segments[1].a == 1920 && d.segments[1].b == 3840 - kHalfBody, "right monitor joined end [%d,%d)",
          d.segments[1].a, d.segments[1].b);
}

static void testSingleWindow()
{
    std::printf("[2] single window\n");
    DesktopSnapshot s = monitors({QRect(0, 0, 1920, 1080)});
    SurfaceBuilder::addWindowEdges(s, {{7, QRect(100, 200, 400, 300)}}, {}, kScale);
    const auto roof = find(s, 7, SurfaceKind::Floor);
    const auto left = find(s, 7, SurfaceKind::WallLeftSide);
    const auto right = find(s, 7, SurfaceKind::WallRightSide);
    CHECK(roof.size() == 1 && roof[0]->line == 200 && roof[0]->a == 100 && roof[0]->b == 500, "roof");
    CHECK(left.size() == 1 && left[0]->line == 100 && left[0]->a == 200 && left[0]->b == 500, "left wall");
    CHECK(right.size() == 1 && right[0]->line == 500 && right[0]->a == 200 && right[0]->b == 500, "right wall (line = right)");
    CHECK(roof.size() == 1 && roof[0]->ownerRect == QRect(100, 200, 400, 300) && roof[0]->id.monitor == -1, "owner rect");
}

static void testFrontWindowOcclusion()
{
    std::printf("[3] front window occlusion\n");
    DesktopSnapshot s = monitors({QRect(0, 0, 1920, 1080)});
    // 앞 창(1)이 뒤 창(2) 지붕 가운데와 왼쪽 벽 아래쪽을 덮는다
    const std::vector<SurfaceBuilder::WindowRect> wins = {
        {1, QRect(250, 150, 200, 400)},   // 앞
        {2, QRect(100, 200, 600, 300)},   // 뒤
    };
    SurfaceBuilder::addWindowEdges(s, wins, {}, kScale);
    const auto roof = find(s, 2, SurfaceKind::Floor);
    CHECK(roof.size() == 2, "back roof split in two (%zu)", roof.size());
    if (roof.size() == 2) {
        CHECK(roof[0]->a == 100 && roof[0]->b == 250, "left piece [%d,%d)", roof[0]->a, roof[0]->b);
        CHECK(roof[1]->a == 450 && roof[1]->b == 700, "right piece [%d,%d)", roof[1]->a, roof[1]->b);
    }
    CHECK(find(s, 1, SurfaceKind::Floor).size() == 1, "front roof untouched");

    // 뒤 창 앞에 남는 틈이 최소 길이보다 짧으면 버린다
    DesktopSnapshot g = monitors({QRect(0, 0, 1920, 1080)});
    const std::vector<SurfaceBuilder::WindowRect> gap = {
        {1, QRect(100 + kMinLen - 1, 150, 600, 400)},
        {2, QRect(100, 200, 600, 300)},
    };
    SurfaceBuilder::addWindowEdges(g, gap, {}, kScale);
    CHECK(find(g, 2, SurfaceKind::Floor).empty(), "short leftover dropped");
}

static void testOccluders()
{
    std::printf("[4] occluders (menu bar / dock / stage strip)\n");
    DesktopSnapshot s = monitors({QRect(0, 0, 1920, 1080)});
    const QRect menuBar(0, 0, 1920, 25);
    const QRect dock(0, 1000, 1920, 80);
    // 지붕이 메뉴 막대 안 → 지붕 없음, 벽은 메뉴 막대 아래부터 Dock 위까지
    SurfaceBuilder::addWindowEdges(s, {{3, QRect(300, 10, 500, 1070)}}, {menuBar, dock}, kScale);
    CHECK(find(s, 3, SurfaceKind::Floor).empty(), "roof under menu bar removed");
    const auto left = find(s, 3, SurfaceKind::WallLeftSide);
    CHECK(left.size() == 1 && left[0]->a == 25 && left[0]->b == 1000, "left wall clipped to [25,1000)");

    // Stage Manager 띠가 창 왼쪽 벽을 덮으면 그 벽은 없다
    DesktopSnapshot t = monitors({QRect(0, 0, 1920, 1080)});
    SurfaceBuilder::addWindowEdges(t, {{4, QRect(150, 300, 600, 400)}}, {QRect(0, 100, 200, 800)}, kScale);
    CHECK(find(t, 4, SurfaceKind::WallLeftSide).empty(), "wall behind stage strip removed");
    const auto roof = find(t, 4, SurfaceKind::Floor);
    CHECK(roof.size() == 1 && roof[0]->a == 200 && roof[0]->b == 750, "roof starts after strip [%d,%d)",
          roof.empty() ? 0 : roof[0]->a, roof.empty() ? 0 : roof[0]->b);
}

static void testMonitorBounds()
{
    std::printf("[5] monitor bounds\n");
    // 지붕 위 몸(두께 24)이 모니터 위로 나가면 지붕 없음, 벽 몸이 화면 왼쪽 밖이면 벽 없음
    DesktopSnapshot s = monitors({QRect(0, 0, 1920, 1080)});
    SurfaceBuilder::addWindowEdges(s, {{5, QRect(kCatH - 1, kCatH - 1, 400, 300)}}, {}, kScale);
    CHECK(find(s, 5, SurfaceKind::Floor).empty(), "roof too close to top");
    CHECK(find(s, 5, SurfaceKind::WallLeftSide).empty(), "left wall too close to screen edge");
    CHECK(find(s, 5, SurfaceKind::WallRightSide).size() == 1, "right wall kept");

    // 화면 밖으로 걸친 창의 지붕은 모니터 안 구간만
    DesktopSnapshot o = monitors({QRect(0, 0, 1920, 1080)});
    SurfaceBuilder::addWindowEdges(o, {{6, QRect(1700, 300, 600, 300)}}, {}, kScale);
    const auto roof = find(o, 6, SurfaceKind::Floor);
    CHECK(roof.size() == 1 && roof[0]->a == 1700 && roof[0]->b == 1920, "roof clipped to monitor");
}

int main()
{
    testMonitorFloors();
    testSingleWindow();
    testFrontWindowOcclusion();
    testOccluders();
    testMonitorBounds();
    return testing::testResult("test_surface");
}
