#include "Surface.hpp"

#include <algorithm>
#include <limits>

CatGravity gravityFor(SurfaceKind kind)
{
    switch (kind) {
    case SurfaceKind::WallLeftSide:  return CatGravity::Right;
    case SurfaceKind::WallRightSide: return CatGravity::Left;
    default:                         return CatGravity::Down;
    }
}

QPoint SurfaceSegment::pointAt(int along) const
{
    return id.kind == SurfaceKind::Floor ? QPoint(along, line) : QPoint(line, along);
}

const SurfaceSegment *DesktopSnapshot::findSegment(const SurfaceId &id, int along) const
{
    for (const SurfaceSegment &seg : segments) {
        if (seg.id == id && seg.contains(along))
            return &seg;
    }
    return nullptr;
}

std::vector<const SurfaceSegment *> DesktopSnapshot::segmentsOf(const SurfaceId &id) const
{
    std::vector<const SurfaceSegment *> result;
    for (const SurfaceSegment &seg : segments) {
        if (seg.id == id)
            result.push_back(&seg);
    }
    return result;
}

std::optional<QRect> DesktopSnapshot::ownerRect(const SurfaceId &id) const
{
    // 같은 id 의 세그먼트는 모두 같은 ownerRect 를 가진다 (가림으로 쪼개진 경우 포함)
    for (const SurfaceSegment &seg : segments) {
        if (seg.id == id)
            return seg.ownerRect;
    }
    return std::nullopt;
}

std::optional<QPoint> DesktopSnapshot::resolve(const SurfacePoint &p) const
{
    const std::optional<QRect> rect = ownerRect(p.surface);
    if (!rect)
        return std::nullopt;

    // offset 은 소유 사각형 원점 기준: Floor → x, Wall → y
    const int origin = p.surface.kind == SurfaceKind::Floor ? rect->left() : rect->top();
    const int along = origin + p.offset;

    const SurfaceSegment *seg = findSegment(p.surface, along);
    if (!seg)
        return std::nullopt;
    return seg->pointAt(along);
}

const SurfaceSegment *DesktopSnapshot::floorBelow(QPoint p) const
{
    const SurfaceSegment *best = nullptr;
    for (const SurfaceSegment &seg : segments) {
        if (seg.id.kind != SurfaceKind::Floor)
            continue;
        if (seg.line < p.y() || !seg.contains(p.x()))
            continue;
        if (!best || seg.line < best->line)
            best = &seg;
    }
    return best;
}

int DesktopSnapshot::monitorAt(QPoint p) const
{
    if (monitors.empty())
        return 0;

    int nearest = 0;
    qint64 nearestDist = std::numeric_limits<qint64>::max();
    for (int i = 0; i < static_cast<int>(monitors.size()); ++i) {
        const QRect &r = monitors[i].bounds;
        if (r.contains(p))
            return i;

        // 사각형까지의 거리² (바깥이면 가장 가까운 모니터)
        const qint64 dx = qMax(qMax(r.left() - p.x(), p.x() - r.right()), 0);
        const qint64 dy = qMax(qMax(r.top() - p.y(), p.y() - r.bottom()), 0);
        const qint64 dist = dx * dx + dy * dy;
        if (dist < nearestDist) {
            nearestDist = dist;
            nearest = i;
        }
    }
    return nearest;
}

bool DesktopSnapshot::isFullscreenAt(QPoint p) const
{
    if (monitors.empty())
        return false;
    return monitors[monitorAt(p)].fullscreen;
}
