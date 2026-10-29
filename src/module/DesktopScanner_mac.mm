#include "DesktopScanner.hpp"

#include "CatSprite.hpp"
#include "Config.hpp"

#include <QGuiApplication>
#include <QScreen>

#include <chrono>

// macOS 판 DesktopScanner (MAC-Conception Phase 1 최소 상태).
// - 모니터: QScreen. 좌표는 Qt / CG 공통의 좌상단 원점 포인트 좌표
// - 바닥: DockProbe 기초 — geometry() 와 availableGeometry() 의 하단 차이 = 하단 Dock 두께. 없으면 화면 하단
// TODO(Phase 2): WindowScanner (CGWindowListCopyWindowInfo) 로 창 지붕 / 벽, StageProbe, liveWindowRect
// TODO(Phase 4): 맨 앞 창이 화면을 덮으면 fullscreen

namespace {

qint64 monotonicMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace

DesktopScanner::DesktopScanner(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(Config::kScanIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, [this] { scanNow(); });
}

void DesktopScanner::setScale(int scale)
{
    m_scale = scale;
}

void DesktopScanner::start()
{
    scanNow();
    m_timer.start();
}

void DesktopScanner::stop()
{
    m_timer.stop();
}

const DesktopSnapshot &DesktopScanner::scanNow()
{
    DesktopSnapshot snap;
    snap.timestampMs = monotonicMs();

    // ── 모니터 ───────────────────────────────────────
    for (const QScreen *screen : QGuiApplication::screens()) {
        MonitorInfo info;
        info.bounds = screen->geometry();
        info.workArea = screen->availableGeometry();   // 메뉴 막대 / Dock 제외
        const int monBottom = info.bounds.y() + info.bounds.height();
        const int workBottom = info.workArea.y() + info.workArea.height();
        info.floorY = workBottom;                      // 하단 Dock 이 있으면 Dock 윗면, 없으면 화면 하단
        info.hasTaskbarFloor = workBottom < monBottom; // 하단 Dock 이 바닥
        snap.monitors.push_back(info);
        snap.virtualBounds = snap.virtualBounds.united(info.bounds);
    }

    // 모니터 바닥 세그먼트 (DesktopScanner.cpp 와 같은 규칙. Phase 2 에서 공통 코드로 뺀다)
    for (int i = 0; i < static_cast<int>(snap.monitors.size()); ++i) {
        const MonitorInfo &m = snap.monitors[i];
        SurfaceSegment seg;
        seg.id.owner = 0;
        seg.id.monitor = i;
        seg.id.kind = SurfaceKind::Floor;
        seg.line = m.floorY;
        seg.a = m.bounds.x();
        seg.b = m.bounds.x() + m.bounds.width();
        // 가상 데스크탑 바깥쪽 끝에서는 몸 절반이 화면 밖으로 나가지 않게 안쪽으로 줄인다 (다른 모니터와 이어진 끝은 그대로)
        const int halfBody = Config::kBodyHalfWidthSpritePx * m_scale;
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

    m_snapshot = std::move(snap);
    emit scanned(m_snapshot);
    return m_snapshot;
}

std::optional<QRect> DesktopScanner::liveWindowRect(WindowHandle)
{
    return std::nullopt;   // TODO(Phase 2): kCGWindowNumber 로 창 하나의 bounds
}
