#include "DesktopScanner.hpp"

#include "CatSprite.hpp"
#include "Config.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>     // SHQueryUserNotificationState

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

// 엣지 좌표 규약 (RECT 와 동일한 반개구간):
//   창 영역 = x ∈ [left, right), y ∈ [top, bottom).  ownerRect = QRect(left, top, right-left, bottom-top)
//   지붕        : Floor,         line = top,   구간 x ∈ [left, right)   (고양이 몸: y ∈ [top-catH, top))
//   왼쪽 측면   : WallLeftSide,  line = left,  구간 y ∈ [top, bottom)   (고양이 몸: x ∈ [left-catH, left))
//   오른쪽 측면 : WallRightSide, line = right, 구간 y ∈ [top, bottom)   (고양이 몸: x ∈ [right, right+catH))
//   즉 오른쪽 측면 line 은 창 바깥 첫 열이다. 가림 판정은 창 "자신의" 픽셀 행/열
//   (지붕=top 행, 왼쪽=left 열, 오른쪽=right-1 열)이 앞 창에 덮였는지로 한다.
//   (QRect::right()/bottom() 은 inclusive 이므로 여기서는 쓰지 않고 x()+width() 를 쓴다.)

namespace {

struct Span
{
    int a = 0;
    int b = 0;   // [a, b)
};
using Spans = std::vector<Span>;

struct WinInfo
{
    HWND hwnd = nullptr;
    int l = 0, t = 0, r = 0, b = 0;   // RECT 규약 (r, b 는 배타)
};

struct TaskbarWnd
{
    HWND hwnd = nullptr;
    RECT rc{};
    bool primary = false;
};

QRect toQRect(const RECT &rc)
{
    return QRect(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
}

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
void occlude(Spans &spans, int ol, int ot, int orr, int ob, SurfaceKind kind, int line)
{
    if (kind == SurfaceKind::Floor) {
        if (ot <= line && line < ob)
            subtractSpan(spans, ol, orr);
    } else {
        const int col = kind == SurfaceKind::WallRightSide ? line - 1 : line;
        if (ol <= col && col < orr)
            subtractSpan(spans, ot, ob);
    }
}

std::wstring classNameOf(HWND hwnd)
{
    wchar_t buf[256] = {};
    const int n = GetClassNameW(hwnd, buf, 256);
    return std::wstring(buf, n > 0 ? n : 0);
}

bool isShellClass(const std::wstring &cls)
{
    return cls == L"Progman" || cls == L"WorkerW" || cls == L"Shell_TrayWnd"
        || cls == L"Shell_SecondaryTrayWnd";
}

bool frameBounds(HWND hwnd, RECT &rc)
{
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rc, sizeof(rc))))
        return true;
    return GetWindowRect(hwnd, &rc) != 0;
}

struct MonitorEnum
{
    std::vector<HMONITOR> handles;
    std::vector<MonitorInfo> infos;
};

BOOL CALLBACK monitorProc(HMONITOR hmon, HDC, LPRECT, LPARAM lp)
{
    auto *e = reinterpret_cast<MonitorEnum *>(lp);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hmon, &mi))
        return TRUE;
    MonitorInfo info;
    info.bounds = toQRect(mi.rcMonitor);
    info.workArea = toQRect(mi.rcWork);
    info.floorY = mi.rcWork.bottom;   // 기본 바닥 = 작업 영역 하단 (배타 경계 = 바닥 선)
    e->handles.push_back(hmon);
    e->infos.push_back(info);
    return TRUE;
}

struct WindowEnum
{
    std::vector<WinInfo> wins;
    std::vector<TaskbarWnd> secondaryTaskbars;
    DWORD ownPid = 0;
};

BOOL CALLBACK windowProc(HWND hwnd, LPARAM lp)
{
    auto *e = reinterpret_cast<WindowEnum *>(lp);

    if (!IsWindowVisible(hwnd) || IsIconic(hwnd))
        return TRUE;

    const std::wstring cls = classNameOf(hwnd);
    if (cls == L"Shell_SecondaryTrayWnd") {
        TaskbarWnd tb;
        tb.hwnd = hwnd;
        if (GetWindowRect(hwnd, &tb.rc))
            e->secondaryTaskbars.push_back(tb);
        return TRUE;
    }
    if (isShellClass(cls))
        return TRUE;

    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW)
        return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == e->ownPid)
        return TRUE;

    // 완전 투명 레이어드 창 제외 (알파 0)
    if (ex & WS_EX_LAYERED) {
        BYTE alpha = 255;
        DWORD flags = 0;
        if (GetLayeredWindowAttributes(hwnd, nullptr, &alpha, &flags) && (flags & LWA_ALPHA) && alpha == 0)
            return TRUE;
    }

    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0)
        return TRUE;

    RECT rc{};
    if (!frameBounds(hwnd, rc))
        return TRUE;
    if (rc.right - rc.left < Config::kMinWindowW || rc.bottom - rc.top < Config::kMinWindowH)
        return TRUE;

    WinInfo w;
    w.hwnd = hwnd;
    w.l = rc.left;
    w.t = rc.top;
    w.r = rc.right;
    w.b = rc.bottom;
    e->wins.push_back(w);
    return TRUE;
}

int monitorIndexOf(const std::vector<HMONITOR> &handles, HMONITOR h)
{
    for (int i = 0; i < static_cast<int>(handles.size()); ++i) {
        if (handles[i] == h)
            return i;
    }
    return -1;
}

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
    MonitorEnum menum;
    EnumDisplayMonitors(nullptr, nullptr, monitorProc, reinterpret_cast<LPARAM>(&menum));
    snap.monitors = menum.infos;
    for (const MonitorInfo &m : snap.monitors)
        snap.virtualBounds = snap.virtualBounds.united(m.bounds);

    std::vector<QRect> monitorRects;
    for (const MonitorInfo &m : snap.monitors)
        monitorRects.push_back(m.bounds);

    // ── 창 열거 (z-order 앞 → 뒤) + 보조 작업표시줄 수집 ──
    WindowEnum wenum;
    wenum.ownPid = GetCurrentProcessId();
    EnumWindows(windowProc, reinterpret_cast<LPARAM>(&wenum));

    // ── 작업표시줄 → 모니터 바닥 ─────────────────────
    const HWND primaryTray = FindWindowW(L"Shell_TrayWnd", nullptr);

    APPBARDATA abdState{};
    abdState.cbSize = sizeof(abdState);
    const bool autoHide = (SHAppBarMessage(ABM_GETSTATE, &abdState) & ABS_AUTOHIDE) != 0;

    std::vector<TaskbarWnd> taskbars = wenum.secondaryTaskbars;
    if (primaryTray && IsWindowVisible(primaryTray)) {
        TaskbarWnd tb;
        tb.hwnd = primaryTray;
        tb.primary = true;
        if (GetWindowRect(primaryTray, &tb.rc))
            taskbars.push_back(tb);

        // 트레이 알림 영역 사각형
        if (const HWND notify = FindWindowExW(primaryTray, nullptr, L"TrayNotifyWnd", nullptr)) {
            RECT rc{};
            if (GetWindowRect(notify, &rc))
                snap.trayRect = toQRect(rc);
        }
    }

    if (!autoHide) {
        for (const TaskbarWnd &tb : taskbars) {
            const int mi = monitorIndexOf(menum.handles, MonitorFromWindow(tb.hwnd, MONITOR_DEFAULTTONEAREST));
            if (mi < 0)
                continue;
            MonitorInfo &mon = snap.monitors[mi];
            const int monBottom = mon.bounds.y() + mon.bounds.height();

            const int w = tb.rc.right - tb.rc.left;
            const int h = tb.rc.bottom - tb.rc.top;
            const bool bottomDocked = tb.rc.bottom >= monBottom
                && tb.rc.top > mon.bounds.y() + mon.bounds.height() / 2
                && w >= h && w >= mon.bounds.width() / 2;
            if (!bottomDocked)
                continue;

            if (tb.primary) {
                // 기본 작업표시줄은 ABM_GETTASKBARPOS 로 도킹 방향을 한 번 더 확인
                APPBARDATA pos{};
                pos.cbSize = sizeof(pos);
                pos.hWnd = tb.hwnd;
                if (SHAppBarMessage(ABM_GETTASKBARPOS, &pos) && pos.uEdge != ABE_BOTTOM)
                    continue;
            }

            mon.floorY = tb.rc.top;
            mon.hasTaskbarFloor = true;
        }
    }

    // 모니터 바닥 세그먼트
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

    // ── 창 엣지 ──────────────────────────────────────
    const int catH = CatSprite::Height * m_scale;                    // 면에 붙었을 때 몸 두께
    const int minLen = Config::kMinSegmentSpritePx * m_scale;

    // 하단 작업표시줄 영역은 모든 창보다 앞(topmost)이므로 가림으로 취급한다.
    // TODO(decision): 창이 작업표시줄 뒤로 걸친 경우 벽/지붕을 작업표시줄 윗면에서 자른다
    //                 (스펙에는 없음. 고양이가 안 보이는 곳으로 가지 않게 하기 위한 보수적 선택).
    std::vector<QRect> taskbarAreas;
    for (const MonitorInfo &m : snap.monitors) {
        if (m.hasTaskbarFloor) {
            taskbarAreas.push_back(QRect(m.bounds.x(), m.floorY, m.bounds.width(),
                                         m.bounds.y() + m.bounds.height() - m.floorY));
        }
    }

    const std::vector<WinInfo> &wins = wenum.wins;
    for (size_t i = 0; i < wins.size(); ++i) {
        const WinInfo &w = wins[i];
        const QRect ownerRect(w.l, w.t, w.r - w.l, w.b - w.t);

        struct Edge { SurfaceKind kind; int line; int a; int b; };
        const Edge edges[3] = {
            {SurfaceKind::Floor,         w.t, w.l, w.r},
            {SurfaceKind::WallLeftSide,  w.l, w.t, w.b},
            {SurfaceKind::WallRightSide, w.r, w.t, w.b},
        };

        for (const Edge &edge : edges) {
            Spans spans{{edge.a, edge.b}};

            for (const QRect &tb : taskbarAreas) {
                occlude(spans, tb.x(), tb.y(), tb.x() + tb.width(), tb.y() + tb.height(),
                        edge.kind, edge.line);
            }
            for (size_t j = 0; j < i && !spans.empty(); ++j) {
                const WinInfo &o = wins[j];
                occlude(spans, o.l, o.t, o.r, o.b, edge.kind, edge.line);
            }
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
                seg.id.owner = reinterpret_cast<WindowHandle>(w.hwnd);
                seg.id.monitor = -1;
                seg.id.kind = edge.kind;
                seg.line = edge.line;
                seg.a = sp.a;
                seg.b = sp.b;
                seg.ownerRect = ownerRect;
                snap.segments.push_back(seg);
            }
        }
    }

    // ── 전체화면 감지 ────────────────────────────────
    const HWND fg = GetForegroundWindow();
    if (fg && !isShellClass(classNameOf(fg))) {
        RECT frc{};
        // 최대화/모니터 크기의 "일반 캡션 창"은 전체화면이 아니다 (작업표시줄 자동 숨김 시 최대화 창이 모니터를 덮는다).
        // 캡션 없는(borderless) 창만 사각형 덮임으로 전체화면 판정한다.
        const LONG_PTR fgStyle = GetWindowLongPtrW(fg, GWL_STYLE);
        const bool normalCaptioned = (fgStyle & WS_CAPTION) == WS_CAPTION;
        if (!normalCaptioned && frameBounds(fg, frc)) {
            for (MonitorInfo &m : snap.monitors) {
                const QRect &b = m.bounds;
                if (frc.left <= b.x() && frc.top <= b.y()
                    && frc.right >= b.x() + b.width() && frc.bottom >= b.y() + b.height())
                    m.fullscreen = true;
            }
        }

        QUERY_USER_NOTIFICATION_STATE qs = QUNS_ACCEPTS_NOTIFICATIONS;
        if (SUCCEEDED(SHQueryUserNotificationState(&qs))
            && (qs == QUNS_BUSY || qs == QUNS_RUNNING_D3D_FULL_SCREEN || qs == QUNS_PRESENTATION_MODE)) {
            const int mi = monitorIndexOf(menum.handles, MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST));
            if (mi >= 0)
                snap.monitors[mi].fullscreen = true;
        }
    }

    m_snapshot = std::move(snap);
    emit scanned(m_snapshot);
    return m_snapshot;
}
