#include "DesktopScanner.hpp"

#include "Config.hpp"
#include "SurfaceBuilder.hpp"

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

// 엣지 좌표 규약과 가림 / 모니터 경계 / 길이 필터는 SurfaceBuilder (플랫폼 공통). 여기서는 Win32 로 창 목록만 모은다.

namespace {

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
    SurfaceBuilder::addMonitorFloors(snap, m_scale);

    // ── 창 엣지 ──────────────────────────────────────
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

    std::vector<SurfaceBuilder::WindowRect> windows;
    windows.reserve(wenum.wins.size());
    for (const WinInfo &w : wenum.wins)
        windows.push_back({reinterpret_cast<WindowHandle>(w.hwnd), QRect(w.l, w.t, w.r - w.l, w.b - w.t)});
    SurfaceBuilder::addWindowEdges(snap, windows, taskbarAreas, m_scale);

    // ── 전체화면 감지 ────────────────────────────────
    const HWND fg = GetForegroundWindow();
    // 트레이 오버플로·빠른 설정 등 셸 플라이아웃은 WS_EX_TOOLWINDOW 창이다.
    // (XamlExplorerHostIslandWindow 는 캡션 없이 모니터 전체를 덮어 사각형 판정에 걸린다.)
    const bool fgToolWindow = fg && (GetWindowLongPtrW(fg, GWL_EXSTYLE) & WS_EX_TOOLWINDOW);
    if (fg && !fgToolWindow && !isShellClass(classNameOf(fg))) {
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

        // QUNS_BUSY 는 쓰지 않는다: 다른 모니터에 최대화 창이 있거나 셸 플라이아웃만 열려도 BUSY 가 나와
        // 평범한 전경 창의 모니터까지 숨겨진다. borderless 전체화면은 위 사각형 판정이 잡는다.
        QUERY_USER_NOTIFICATION_STATE qs = QUNS_ACCEPTS_NOTIFICATIONS;
        if (SUCCEEDED(SHQueryUserNotificationState(&qs))
            && (qs == QUNS_RUNNING_D3D_FULL_SCREEN || qs == QUNS_PRESENTATION_MODE)) {
            const int mi = monitorIndexOf(menum.handles, MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST));
            if (mi >= 0)
                snap.monitors[mi].fullscreen = true;
        }
    }

    m_snapshot = std::move(snap);
    emit scanned(m_snapshot);
    return m_snapshot;
}

std::optional<QRect> DesktopScanner::liveWindowRect(WindowHandle handle)
{
    const HWND hwnd = reinterpret_cast<HWND>(handle);
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd))
        return std::nullopt;
    RECT rc{};
    if (!frameBounds(hwnd, rc))
        return std::nullopt;
    return toQRect(rc);
}
