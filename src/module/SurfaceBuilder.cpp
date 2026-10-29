#include "SurfaceBuilder.hpp"

#include "CatSprite.hpp"
#include "Config.hpp"

#include <algorithm>

namespace {

struct Span
{
    int a = 0;
    int b = 0;   // [a, b)
};
using Spans = std::vector<Span>;

// s 에서 [a, b) 를 뺀다
void subtractSpan(Spans &s, int a, int b)
{
    Spans out;
    out.reserve(s.size() + 1);
    for (const Span &sp : s) {
        if (b <= sp.a || a >= sp.b) {
            out.push_back(sp);
            continue;
        }
        if (sp.a < a)
            out.push_back({sp.a, a});
        if (sp.b > b)
            out.push_back({b, sp.b});
    }
    s = std::move(out);
}

// 두 구간 집합의 교집합 (각각 서로 겹치지 않는다고 가정)
Spans intersectSpans(const Spans &x, const Spans &y)
{
    Spans out;
    for (const Span &sx : x) {
        for (const Span &sy : y) {
            const int a = std::max(sx.a, sy.a);
            const int b = std::min(sx.b, sy.b);
            if (a < b)
                out.push_back({a, b});
        }
    }
    std::sort(out.begin(), out.end(), [](const Span &p, const Span &q) { return p.a < q.a; });
    return out;
}

// 몸 두께 범위 [thickLo, thickHi) 가 "창 쪽 픽셀(sidePix)을 포함하는 같은 모니터" 안에 완전히 들어가는 along 구간들.
// 모니터 이음매에서 몸이 옆 모니터(다른 앱 위)에 그려져 공중에 떠 보이는 것을 막는다.
// alongIsX: 지붕(along=x, 두께=y) / 벽(along=y, 두께=x)
// sidePix : 창 자신의 엣지 픽셀 좌표 (두께 축 기준: 지붕=top, 왼쪽=left, 오른쪽=right-1)
Spans allowedSpans(const std::vector<QRect> &mons, bool alongIsX, int thickLo, int thickHi, int sidePix)
{
    auto alongLo = [&](const QRect &m) { return alongIsX ? m.x() : m.y(); };
    auto alongHi = [&](const QRect &m) { return alongIsX ? m.x() + m.width() : m.y() + m.height(); };
    auto thLo = [&](const QRect &m) { return alongIsX ? m.y() : m.x(); };
    auto thHi = [&](const QRect &m) { return alongIsX ? m.y() + m.height() : m.x() + m.width(); };

    Spans cand;
    for (const QRect &m : mons) {
        if (sidePix < thLo(m) || sidePix >= thHi(m))
            continue;   // 창 쪽 픽셀이 이 모니터에 없다
        if (thickLo < thLo(m) || thickHi > thHi(m))
            continue;   // 몸이 이 모니터를 벗어난다
        cand.push_back({alongLo(m), alongHi(m)});
    }
    std::sort(cand.begin(), cand.end(), [](const Span &x, const Span &y) { return x.a < y.a; });

    // 맞닿은/겹친 구간은 합친다
    Spans out;
    for (const Span &sp : cand) {
        if (!out.empty() && sp.a <= out.back().b)
            out.back().b = std::max(out.back().b, sp.b);
        else
            out.push_back(sp);
    }
    return out;
}

// 사각형 o 가 (kind, line) 엣지의 픽셀 행/열을 덮으면 그 구간을 spans 에서 뺀다
void occlude(Spans &spans, const QRect &o, SurfaceKind kind, int line)
{
    const int ol = o.x(), ot = o.y(), orr = o.x() + o.width(), ob = o.y() + o.height();
    if (kind == SurfaceKind::Floor) {
        if (ot <= line && line < ob)
            subtractSpan(spans, ol, orr);
    } else {
        const int col = kind == SurfaceKind::WallRightSide ? line - 1 : line;
        if (ol <= col && col < orr)
            subtractSpan(spans, ot, ob);
    }
}

} // namespace

namespace SurfaceBuilder {

void addMonitorFloors(DesktopSnapshot &snap, int scale)
{
    for (int i = 0; i < static_cast<int>(snap.monitors.size()); ++i) {
        const MonitorInfo &m = snap.monitors[i];
        SurfaceSegment seg;
        seg.id.owner = 0;
        seg.id.monitor = i;
        seg.id.kind = SurfaceKind::Floor;
        seg.line = m.floorY;
        seg.a = m.bounds.x();
        seg.b = m.bounds.x() + m.bounds.width();
        const int halfBody = Config::kBodyHalfWidthSpritePx * scale;
        const auto joined = [&](int x) {
            for (const MonitorInfo &o : snap.monitors) {
                if (o.bounds.contains(x, m.floorY - 1))
                    return true;
            }
            return false;
        };
        if (!joined(seg.a - 1))
            seg.a += halfBody;
        if (!joined(seg.b))
            seg.b -= halfBody;
        seg.ownerRect = m.bounds;
        snap.segments.push_back(seg);
    }
}

void addWindowEdges(DesktopSnapshot &snap, const std::vector<WindowRect> &windows,
                    const std::vector<QRect> &occluders, int scale)
{
    const int catH = CatSprite::Height * scale;   // 면에 붙었을 때 몸 두께
    const int minLen = Config::kMinSegmentSpritePx * scale;

    std::vector<QRect> monitorRects;
    for (const MonitorInfo &m : snap.monitors)
        monitorRects.push_back(m.bounds);

    for (size_t i = 0; i < windows.size(); ++i) {
        const QRect &r = windows[i].rect;
        const int l = r.x(), t = r.y(), rr = r.x() + r.width(), b = r.y() + r.height();

        struct Edge { SurfaceKind kind; int line; int a; int b; };
        const Edge edges[3] = {
            {SurfaceKind::Floor,         t,  l, rr},
            {SurfaceKind::WallLeftSide,  l,  t, b},
            {SurfaceKind::WallRightSide, rr, t, b},
        };

        for (const Edge &edge : edges) {
            Spans spans{{edge.a, edge.b}};

            for (const QRect &o : occluders)
                occlude(spans, o, edge.kind, edge.line);
            for (size_t j = 0; j < i && !spans.empty(); ++j)
                occlude(spans, windows[j].rect, edge.kind, edge.line);
            if (spans.empty())
                continue;

            // 화면 밖/이음매: 몸이 "창 쪽 픽셀이 속한 같은 모니터" 안에 완전히 들어가는 구간만
            //   (지붕=top 행, 왼쪽=left 열, 오른쪽=right-1 열)
            const bool isRoof = edge.kind == SurfaceKind::Floor;
            const int thickLo = edge.kind == SurfaceKind::WallRightSide ? edge.line : edge.line - catH;
            const int thickHi = thickLo + catH;
            const int sidePix = edge.kind == SurfaceKind::WallRightSide ? edge.line - 1 : edge.line;
            spans = intersectSpans(spans, allowedSpans(monitorRects, isRoof, thickLo, thickHi, sidePix));

            for (const Span &sp : spans) {
                if (sp.b - sp.a < minLen)
                    continue;
                SurfaceSegment seg;
                seg.id.owner = windows[i].handle;
                seg.id.monitor = -1;
                seg.id.kind = edge.kind;
                seg.line = edge.line;
                seg.a = sp.a;
                seg.b = sp.b;
                seg.ownerRect = r;
                snap.segments.push_back(seg);
            }
        }
    }
}

} // namespace SurfaceBuilder
