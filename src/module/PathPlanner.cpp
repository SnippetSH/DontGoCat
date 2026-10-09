#include "PathPlanner.hpp"

#include "Config.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>

namespace {

constexpr double kMsPerSec = 1000.0;
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr int kViaWalk = -1;   // dijkstra via[]: 같은 세그먼트 걷기
constexpr int kViaJump = -2;   // 점프 (그 외 값은 명시 간선 인덱스)

struct Node
{
    int seg = 0;
    int along = 0;
    QPoint pos;            // 앵커 데스크탑 좌표
};

struct Edge
{
    int to = 0;
    double cost = 0.0;
    RouteStep::Kind kind = RouteStep::Kind::Corner;
    bool towardPositive = false;
    int next = -1;         // 같은 출발 노드의 다음 간선
};

// 스냅샷의 세그먼트로 만드는 면 그래프. 사용 순서: 생성 → addNode(시작/목표) → finalize → dijkstra
class Graph
{
public:
    Graph(const DesktopSnapshot &snap, int scale)
        : m_snap(snap), m_scale(scale)
    {
        m_step = std::max(1, Config::kPlannerSampleSpritePx * scale);
        const int n = int(snap.segments.size());
        m_segBase.resize(n);
        m_segRegular.resize(n);
        m_segCount.resize(n);
        m_segNodes.resize(n);
        for (int s = 0; s < n; ++s) {
            const SurfaceSegment &seg = snap.segments[s];
            // 샘플: a, a+step, … (≤ b-1) 그리고 마지막 b-1
            const int regular = (seg.length() - 1) / m_step + 1;
            const bool needLast = seg.a + (regular - 1) * m_step != seg.b - 1;
            m_segBase[s] = int(m_nodes.size());
            m_segRegular[s] = regular;
            m_segCount[s] = regular + (needLast ? 1 : 0);
            m_segNodes[s].reserve(m_segCount[s] + 4);
            for (int k = 0; k < m_segCount[s]; ++k) {
                pushNode(s, sampleAlong(s, k));
            }
        }
    }

    int segmentCount() const { return int(m_snap.segments.size()); }
    const SurfaceSegment &segment(int s) const { return m_snap.segments[s]; }
    const Node &node(int i) const { return m_nodes[i]; }
    int nodeCount() const { return int(m_nodes.size()); }
    const std::vector<int> &nodesOf(int s) const { return m_segNodes[s]; }

    // SurfacePoint → (세그먼트 인덱스, along). 못 찾으면 -1
    int locate(const SurfacePoint &p, int *alongOut) const
    {
        const std::optional<QRect> rect = m_snap.ownerRect(p.surface);
        if (!rect)
            return -1;
        const int along = (p.surface.kind == SurfaceKind::Floor ? rect->left() : rect->top()) + p.offset;
        const SurfaceSegment *seg = m_snap.findSegment(p.surface, along);
        if (!seg)
            return -1;
        *alongOut = along;
        return int(seg - m_snap.segments.data());
    }

    int sampleAlong(int s, int k) const
    {
        const SurfaceSegment &seg = m_snap.segments[s];
        return k < m_segRegular[s] ? seg.a + k * m_step : seg.b - 1;
    }

    // 세그먼트 s 에 along 위치 노드를 만든다 (샘플이거나 이미 있으면 그걸 돌려줌)
    int addNode(int s, int along)
    {
        const SurfaceSegment &seg = m_snap.segments[s];
        along = std::clamp(along, seg.a, seg.b - 1);
        if (along == seg.b - 1)
            return m_segBase[s] + m_segCount[s] - 1;
        if ((along - seg.a) % m_step == 0 && (along - seg.a) / m_step < m_segRegular[s])
            return m_segBase[s] + (along - seg.a) / m_step;

        const qint64 key = (qint64(s) << 32) | quint32(along);
        auto it = m_extra.find(key);
        if (it != m_extra.end())
            return it->second;
        const int id = pushNode(s, along);
        m_extra.emplace(key, id);
        return id;
    }

    // 가까운 샘플 k 주변 후보(최대 3개 + 끝점)를 out 에 담는다
    int nearestSamples(int s, int along, int out[4]) const
    {
        const SurfaceSegment &seg = m_snap.segments[s];
        const int idx = std::clamp((along - seg.a + m_step / 2) / m_step, 0, m_segRegular[s] - 1);
        int n = 0;
        for (int k = idx - 1; k <= idx + 1; ++k) {
            if (k >= 0 && k < m_segCount[s])
                out[n++] = m_segBase[s] + k;
        }
        const int last = m_segBase[s] + m_segCount[s] - 1;
        if (idx >= m_segRegular[s] - 2 && std::find(out, out + n, last) == out + n)
            out[n++] = last;
        return n;
    }

    void finalize()
    {
        m_head.assign(m_nodes.size(), -1);
        addCornerEdges();
        addWalkOffEdges();
        prepareJumps();
        m_head.resize(m_nodes.size(), -1);
    }

    // src 에서 출발하는 Dijkstra. dst >= 0 이면 도착 시 중단
    void dijkstra(int src, int dst, bool run, std::vector<double> &dist, std::vector<int> &prev, std::vector<int> &via) const
    {
        const int n = nodeCount();
        dist.assign(n, kInf);
        prev.assign(n, -1);
        via.assign(n, kViaWalk);

        const double floorSpeed = Locomotion::pxPerSecond(run ? CatAnim::Run : CatAnim::Walk, m_scale);
        const double wallSpeed = Locomotion::pxPerSecond(CatAnim::Climb, m_scale);

        using Item = std::pair<double, int>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
        dist[src] = 0.0;
        pq.emplace(0.0, src);
        while (!pq.empty()) {
            const auto [d, u] = pq.top();
            pq.pop();
            if (d > dist[u])
                continue;
            if (u == dst)
                break;

            // 명시 간선 (모서리/점프/낙하)
            for (int e = m_head[u]; e >= 0; e = m_edges[e].next) {
                const Edge &ed = m_edges[e];
                const double nd = d + ed.cost;
                if (nd < dist[ed.to]) {
                    dist[ed.to] = nd;
                    prev[ed.to] = u;
                    via[ed.to] = e;
                    pq.emplace(nd, ed.to);
                }
            }
            // 점프 (즉석 생성)
            forEachJump(u, [&](int to, double cost) {
                const double nd = d + cost;
                if (nd < dist[to]) {
                    dist[to] = nd;
                    prev[to] = u;
                    via[to] = kViaJump;
                    pq.emplace(nd, to);
                }
            });
            // 암묵 간선: 같은 세그먼트 안에서 걷기/오르기
            const Node &un = m_nodes[u];
            const double speed = m_snap.segments[un.seg].id.kind == SurfaceKind::Floor ? floorSpeed : wallSpeed;
            for (int v : m_segNodes[un.seg]) {
                if (v == u)
                    continue;
                const double nd = d + std::abs(m_nodes[v].along - un.along) / speed;
                if (nd < dist[v]) {
                    dist[v] = nd;
                    prev[v] = u;
                    via[v] = kViaWalk;
                    pq.emplace(nd, v);
                }
            }
        }
    }

    const Edge &edge(int e) const { return m_edges[e]; }

private:
    int pushNode(int s, int along)
    {
        Node nd;
        nd.seg = s;
        nd.along = along;
        nd.pos = m_snap.segments[s].pointAt(along);
        m_nodes.push_back(nd);
        m_segNodes[s].push_back(int(m_nodes.size()) - 1);
        return int(m_nodes.size()) - 1;
    }

    void addEdge(int from, int to, double cost, RouteStep::Kind kind, bool positive = false)
    {
        if (from >= int(m_head.size()))
            m_head.resize(m_nodes.size() + 16, -1);
        Edge e;
        e.to = to;
        e.cost = cost;
        e.kind = kind;
        e.towardPositive = positive;
        e.next = m_head[from];
        m_head[from] = int(m_edges.size());
        m_edges.push_back(e);
    }

    // 벽 ↔ 바닥 모서리 전환 (양방향)
    void addCornerEdges()
    {
        const int n = segmentCount();
        const double mantleSec = animInfo(CatAnim::Mantle).totalMs() / kMsPerSec;
        for (int w = 0; w < n; ++w) {
            const SurfaceSegment &wall = m_snap.segments[w];
            if (wall.id.kind == SurfaceKind::Floor)
                continue;
            for (int f = 0; f < n; ++f) {
                const SurfaceSegment &floor = m_snap.segments[f];
                if (floor.id.kind != SurfaceKind::Floor)
                    continue;
                // 빠른 거름: 벽의 위/아래끝이 바닥선 근처여야 함
                if (std::abs(floor.line - wall.b) > Config::kCornerTolerancePx
                    && std::abs(floor.line - wall.a) > Config::kCornerTolerancePx)
                    continue;
                const std::optional<CornerLink> link = Locomotion::cornerLink(wall, floor, m_scale);
                if (!link)
                    continue;

                const int wallNode = addNode(w, link->wall.offset + Locomotion::originOf(wall));
                const int floorNode = addNode(f, link->floor.offset + Locomotion::originOf(floor));
                const double sec = link->kind == CornerLink::Kind::WallTop ? mantleSec : Config::kCornerSitSec;
                addEdge(wallNode, floorNode, sec, RouteStep::Kind::Corner);
                addEdge(floorNode, wallNode, sec, RouteStep::Kind::Corner);
            }
        }
    }

    // 바닥(지붕) 끝에서 걸어 나가 아래 첫 바닥으로 낙하
    void addWalkOffEdges()
    {
        const int n = segmentCount();
        for (int s = 0; s < n; ++s) {
            const SurfaceSegment &seg = m_snap.segments[s];
            if (seg.id.kind != SurfaceKind::Floor)
                continue;
            for (int dir = 0; dir < 2; ++dir) {
                const bool positive = dir == 1;
                const int xOut = positive ? seg.b : seg.a - 1;   // 끝을 벗어난 첫 열
                const SurfaceSegment *below = m_snap.floorBelow(QPoint(xOut, seg.line + 1));
                if (!below)
                    continue;
                const int bs = int(below - m_snap.segments.data());
                const int from = m_segBase[s] + (positive ? m_segCount[s] - 1 : 0);
                const int to = addNode(bs, xOut);
                const double sec = Locomotion::fallSeconds(below->line - seg.line, m_scale) + Config::kFallPenaltySec;
                addEdge(from, to, sec, RouteStep::Kind::WalkOff, positive);
            }
        }
    }

    // 점프 간선: 바닥→바닥, 바닥→벽, 벽→바닥. 위로 kJumpMaxUpSpritePx, 수평 kJumpMaxDxSpritePx 이내 (아래는 무제한)
    // 간선 수가 많으므로 미리 만들지 않고 Dijkstra 가 노드를 꺼낼 때 forEachJump 로 즉석에서 만든다.
    // 여기서는 세그먼트 쌍 단위로 후보 목록만 준비한다.
    void prepareJumps()
    {
        const int maxUp = Config::kJumpMaxUpSpritePx * m_scale;
        const int maxDx = Config::kJumpMaxDxSpritePx * m_scale;
        const int nSeg = segmentCount();

        // 세그먼트 쌍 단위 사전 거름 (구간 전체의 범위로 보수적으로): 노드마다 모든 세그먼트를 훑지 않도록 후보 목록을 만든다
        auto gap = [](int lo1, int hi1, int lo2, int hi2) { return std::max({0, lo2 - hi1, lo1 - hi2}); };
        m_dstOf.assign(nSeg, {});
        for (int s = 0; s < nSeg; ++s) {
            const SurfaceSegment &a = m_snap.segments[s];
            const bool aFloor = a.id.kind == SurfaceKind::Floor;
            for (int d = 0; d < nSeg; ++d) {
                const SurfaceSegment &b = m_snap.segments[d];
                const bool bFloor = b.id.kind == SurfaceKind::Floor;
                if (d == s || (!aFloor && !bFloor))
                    continue;
                if (b.id.owner != 0 && b.id.owner == a.id.owner)
                    continue;   // 같은 창의 지붕 ↔ 벽은 모서리 전환으로만 오간다 (점프로 모서리를 건너뛰지 않음)
                bool ok;
                if (aFloor && bFloor)
                    ok = a.line - b.line <= maxUp && gap(a.a, a.b - 1, b.a, b.b - 1) <= maxDx;
                else if (aFloor)
                    ok = gap(a.a, a.b - 1, b.line, b.line) <= maxDx && b.b - 1 >= a.line - maxUp;
                else
                    ok = gap(a.line, a.line, b.a, b.b - 1) <= maxDx && a.a - b.line <= maxUp;
                if (ok)
                    m_dstOf[s].push_back(d);
            }
        }
    }

    // 노드 u 에서 갈 수 있는 점프 (도착 노드, 비용) 를 visit(to, cost) 로 넘긴다
    template <class Fn>
    void forEachJump(int u, Fn &&visit) const
    {
        const int maxUp = Config::kJumpMaxUpSpritePx * m_scale;
        const int maxDx = Config::kJumpMaxDxSpritePx * m_scale;
        {
            const Node &src = m_nodes[u];

            for (const int d : m_dstOf[src.seg]) {
                const SurfaceSegment &ds = m_snap.segments[d];
                const bool dstFloor = ds.id.kind == SurfaceKind::Floor;

                int cand[8];
                int nc = 0;
                if (dstFloor) {
                    // 위로 올라야 하는 높이와 수평 거리(구간까지의 최소 거리)로 거른다
                    if (src.pos.y() - ds.line > maxUp)
                        continue;
                    const int near = std::clamp(src.pos.x(), ds.a, ds.b - 1);
                    if (std::abs(near - src.pos.x()) > maxDx)
                        continue;
                    int tmp[4];
                    const int m = nearestSamples(d, near, tmp);
                    int best = -1;
                    for (int i = 0; i < m; ++i) {
                        const Node &t = m_nodes[tmp[i]];
                        if (std::abs(t.pos.x() - src.pos.x()) > maxDx)
                            continue;
                        if (best < 0 || std::abs(t.pos.x() - src.pos.x()) < std::abs(m_nodes[best].pos.x() - src.pos.x()))
                            best = tmp[i];
                    }
                    if (best >= 0)
                        cand[nc++] = best;
                } else {
                    // 벽: 수평 거리 = 벽 선까지, 높이는 올라갈 수 있는 한계 안의 구간
                    if (std::abs(ds.line - src.pos.x()) > maxDx)
                        continue;
                    const int lowestAllowed = src.pos.y() - maxUp;   // 이보다 위(작은 y)는 못 올라감
                    if (ds.b - 1 < lowestAllowed)
                        continue;
                    // 후보 둘: 내 높이에 가장 가까운 곳 / 올라갈 수 있는 가장 높은 곳
                    const int nearY = std::clamp(src.pos.y(), ds.a, ds.b - 1);
                    const int topY = std::max(ds.a, lowestAllowed);
                    int best = -1, highest = -1;
                    int tmp[4];
                    for (const int target : {nearY, topY}) {
                        const int m = nearestSamples(d, target, tmp);
                        for (int i = 0; i < m; ++i) {
                            const Node &t = m_nodes[tmp[i]];
                            if (t.pos.y() < lowestAllowed)
                                continue;
                            if (best < 0 || std::abs(t.pos.y() - src.pos.y()) < std::abs(m_nodes[best].pos.y() - src.pos.y()))
                                best = tmp[i];
                            if (highest < 0 || t.pos.y() < m_nodes[highest].pos.y())
                                highest = tmp[i];
                        }
                    }
                    if (best >= 0)
                        cand[nc++] = best;
                    if (highest >= 0 && highest != best)
                        cand[nc++] = highest;
                }

                for (int i = 0; i < nc; ++i) {
                    const QPoint to = m_nodes[cand[i]].pos;
                    const JumpArc arc = Locomotion::planJump(QPointF(src.pos), QPointF(to), m_scale);
                    visit(cand[i], Config::kJumpPenaltySec + arc.flightSec);
                }
            }
        }
    }

    const DesktopSnapshot &m_snap;
    int m_scale;
    int m_step = 1;
    std::vector<int> m_segBase, m_segRegular, m_segCount;
    std::vector<std::vector<int>> m_segNodes;
    std::vector<Node> m_nodes;
    std::vector<Edge> m_edges;
    std::vector<int> m_head;
    std::vector<std::vector<int>> m_dstOf;   // 세그먼트별 점프 후보 도착 세그먼트
    std::unordered_map<qint64, int> m_extra;
};

SurfacePoint pointOfNode(const Graph &g, int node)
{
    return Locomotion::pointOn(g.segment(g.node(node).seg), g.node(node).along);
}

} // namespace

void PathPlanner::setScale(int scale)
{
    m_scale = scale;
}

std::optional<Route> PathPlanner::plan(const DesktopSnapshot &snapshot,
                                       const SurfacePoint &from,
                                       const SurfacePoint &to,
                                       bool preferRun) const
{
    Graph g(snapshot, m_scale);
    int fromAlong = 0, toAlong = 0;
    const int fromSeg = g.locate(from, &fromAlong);
    const int toSeg = g.locate(to, &toAlong);
    if (fromSeg < 0 || toSeg < 0)
        return std::nullopt;

    const int src = g.addNode(fromSeg, fromAlong);
    const int dst = g.addNode(toSeg, toAlong);
    if (src == dst)
        return Route{};
    g.finalize();

    std::vector<double> dist;
    std::vector<int> prev, via;
    g.dijkstra(src, dst, preferRun, dist, prev, via);
    if (dist[dst] == kInf)
        return std::nullopt;

    // 경로 복원 (뒤 → 앞)
    std::vector<int> path;
    for (int v = dst; v >= 0; v = prev[v])
        path.push_back(v);
    std::reverse(path.begin(), path.end());

    // 같은 세그먼트 걷기가 이어지면 하나의 Move 로 합친다
    Route route;
    const int m = int(path.size());
    for (int i = 1; i < m;) {
        const int v = path[i];
        if (via[v] == kViaWalk) {
            int j = i;
            while (j + 1 < m && via[path[j + 1]] == kViaWalk)
                ++j;
            RouteStep step;
            step.kind = RouteStep::Kind::Move;
            step.target = pointOfNode(g, path[j]);
            step.run = preferRun && g.segment(g.node(path[j]).seg).id.kind == SurfaceKind::Floor;
            route.push_back(step);
            i = j + 1;
        } else {
            RouteStep step;
            step.target = pointOfNode(g, v);
            if (via[v] == kViaJump) {
                step.kind = RouteStep::Kind::Jump;
            } else {
                const Edge &e = g.edge(via[v]);
                step.kind = e.kind;
                step.towardPositive = e.towardPositive;
            }
            route.push_back(step);
            ++i;
        }
    }
    return route;
}

namespace {

// from 에서 도달 가능한 세그먼트를 구한다 (reached[세그먼트 인덱스]). 시작 세그먼트를 못 찾으면 false
bool reachableSegments(const DesktopSnapshot &snapshot, const SurfacePoint &from, int scale, std::vector<char> &reached)
{
    Graph g(snapshot, scale);
    int along = 0;
    const int seg = g.locate(from, &along);
    if (seg < 0)
        return false;
    const int src = g.addNode(seg, along);
    g.finalize();

    std::vector<double> dist;
    std::vector<int> prev, via;
    g.dijkstra(src, -1, false, dist, prev, via);

    reached.assign(g.segmentCount(), 0);
    for (int s = 0; s < g.segmentCount(); ++s) {
        // 한 세그먼트 안은 걸어서 모두 이어지므로 노드 하나라도 닿으면 세그먼트 전체 도달 가능
        for (int v : g.nodesOf(s)) {
            if (dist[v] != kInf) {
                reached[s] = 1;
                break;
            }
        }
    }
    return true;
}

} // namespace

std::optional<SurfacePoint> PathPlanner::nearestReachable(const DesktopSnapshot &snapshot,
                                                          const SurfacePoint &from,
                                                          QPoint desktopPoint) const
{
    std::vector<char> reached;
    if (!reachableSegments(snapshot, from, m_scale, reached))
        return std::nullopt;

    // 점이 바닥 위(바로 아래 첫 바닥)에 있고 그 바닥에 갈 수 있으면 그 바닥 우선
    // (너무 높이 떠 있는 점이면 바닥 우선을 적용하지 않고 순수 거리로)
    if (const SurfaceSegment *below = snapshot.floorBelow(desktopPoint)) {
        if (reached[below - snapshot.segments.data()]
            && below->line - desktopPoint.y() <= Config::kNearestFloorPreferMaxSpritePx * m_scale)
            return Locomotion::pointOn(*below, desktopPoint.x());
    }

    const SurfaceSegment *best = nullptr;
    int bestAlong = 0;
    qint64 bestDist = std::numeric_limits<qint64>::max();
    for (size_t s = 0; s < snapshot.segments.size(); ++s) {
        if (!reached[s])
            continue;
        const SurfaceSegment &seg = snapshot.segments[s];
        const bool floor = seg.id.kind == SurfaceKind::Floor;
        const int pAlong = floor ? desktopPoint.x() : desktopPoint.y();
        const int pLine = floor ? desktopPoint.y() : desktopPoint.x();
        const int along = std::clamp(pAlong, seg.a, seg.b - 1);
        const qint64 da = along - pAlong;
        const qint64 dl = seg.line - pLine;
        const qint64 d = da * da + dl * dl;
        if (d < bestDist) {
            bestDist = d;
            best = &seg;
            bestAlong = along;
        }
    }
    if (!best)
        return std::nullopt;
    return Locomotion::pointOn(*best, bestAlong);
}

std::optional<SurfacePoint> PathPlanner::randomReachable(const DesktopSnapshot &snapshot,
                                                         const SurfacePoint &from,
                                                         QRandomGenerator &rng) const
{
    std::vector<char> reached;
    if (!reachableSegments(snapshot, from, m_scale, reached))
        return std::nullopt;

    qint64 total = 0;
    for (size_t s = 0; s < snapshot.segments.size(); ++s) {
        if (reached[s])
            total += snapshot.segments[s].length();
    }
    if (total <= 0)
        return std::nullopt;

    qint64 pick = qint64(rng.bounded(quint32(std::min<qint64>(total, std::numeric_limits<qint32>::max()))));
    for (size_t s = 0; s < snapshot.segments.size(); ++s) {
        if (!reached[s])
            continue;
        const SurfaceSegment &seg = snapshot.segments[s];
        if (pick < seg.length())
            return Locomotion::pointOn(seg, seg.a + int(pick));
        pick -= seg.length();
    }
    return std::nullopt;
}

void applyRouteStep(Locomotion &loco, const RouteStep &step, double runFrameScale)
{
    switch (step.kind) {
    case RouteStep::Kind::Move:    loco.moveAlong(step.target.offset, step.run, runFrameScale); break;
    case RouteStep::Kind::Corner:  loco.corner(step.target); break;
    case RouteStep::Kind::Jump:    loco.jumpTo(step.target); break;
    case RouteStep::Kind::WalkOff: loco.walkOffEdge(step.towardPositive); break;
    }
}
