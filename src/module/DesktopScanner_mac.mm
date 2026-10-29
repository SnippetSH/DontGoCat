#include "DesktopScanner.hpp"

#include "Config.hpp"
#include "SurfaceBuilder.hpp"

#include <QGuiApplication>
#include <QScreen>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include <unistd.h>

#include <chrono>
#include <optional>
#include <vector>

// macOS 판 DesktopScanner (MAC-Conception Phase 2).
// - 모니터: QScreen. 좌표는 Qt / CG 공통의 좌상단 원점 포인트 좌표 (변환 없음)
// - 바닥: 항상 화면 하단. availableGeometry 는 Dock 띠 두께만 주고 아이콘 막대의 실제 폭은 알 수 없어서
//   (Dock 창도 화면 전체 크기) Dock 윗면을 화면 전폭 바닥으로 쓰면 Dock 이 없는 좌우에서 공중에 뜬다.
//   고양이는 Dock 아이콘 위를 겹쳐 지나가고, 평소 클릭 통과라 Dock 클릭은 막지 않는다
// - 창 (WindowScanner): CGWindowListCopyWindowInfo(OnScreenOnly | ExcludeDesktopElements). 배열 순서 = 앞 → 뒤.
//   layer 0 만, 자기 PID / alpha < 0.1 / 너무 작은 창 제외. 창 제목은 읽지 않는다 (화면 기록 권한 불필요)
// - 가림: 메뉴 막대와 Stage Manager 띠 (StageProbe) 는 모든 창보다 앞. Dock 띠는 바닥과 같은 이유로 가림에서도 뺀다
// - WindowHandle = kCGWindowNumber
// TODO(Phase 4): 맨 앞 창이 화면을 덮으면 fullscreen

namespace {

constexpr double kMinAlpha = 0.1;

qint64 monotonicMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

QRect boundsOf(NSDictionary *info)
{
    CGRect r = CGRectZero;
    id dict = info[(__bridge NSString *)kCGWindowBounds];
    if (!dict || !CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)dict, &r))
        return {};
    return QRect(qRound(r.origin.x), qRound(r.origin.y), qRound(r.size.width), qRound(r.size.height));
}

// Stage Manager 켜짐 여부 (com.apple.WindowManager GloballyEnabled). 시스템 설정에서 바뀌면 다음 스캔에 반영
bool stageManagerEnabled()
{
    const CFStringRef app = CFSTR("com.apple.WindowManager");
    CFPreferencesAppSynchronize(app);
    Boolean valid = false;
    const Boolean on = CFPreferencesGetAppBooleanValue(CFSTR("GloballyEnabled"), app, &valid);
    return valid && on;
}

// 메뉴 막대 띠: 모니터 위쪽에서 availableGeometry 위까지 (Dock 은 위쪽에 올 수 없다)
std::optional<QRect> menuBarStrip(const MonitorInfo &m)
{
    const QRect &b = m.bounds;
    const QRect &w = m.workArea;
    if (w.y() <= b.y())
        return std::nullopt;
    return QRect(b.x(), b.y(), b.width(), w.y() - b.y());
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
        info.floorY = info.bounds.y() + info.bounds.height();   // Dock 유무와 상관없이 화면 하단 (위 설명)
        info.hasTaskbarFloor = false;
        snap.monitors.push_back(info);
        snap.virtualBounds = snap.virtualBounds.united(info.bounds);
    }

    SurfaceBuilder::addMonitorFloors(snap, m_scale);

    // ── WindowScanner + StageProbe ───────────────────
    std::vector<SurfaceBuilder::WindowRect> windows;
    std::vector<QRect> stageRects(snap.monitors.size());   // 모니터별 WindowManager 창 합집합
    const bool stage = stageManagerEnabled();
    const pid_t ownPid = getpid();

    @autoreleasepool {
        NSArray *list = CFBridgingRelease(CGWindowListCopyWindowInfo(
            kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID));
        for (NSDictionary *info in list) {
            const int layer = [info[(__bridge NSString *)kCGWindowLayer] intValue];
            const pid_t pid = [info[(__bridge NSString *)kCGWindowOwnerPID] intValue];
            const double alpha = [info[(__bridge NSString *)kCGWindowAlpha] doubleValue];
            if (pid == ownPid || alpha < kMinAlpha)
                continue;
            const QRect rect = boundsOf(info);
            if (rect.isEmpty())
                continue;

            if (stage && [info[(__bridge NSString *)kCGWindowOwnerName] isEqualToString:@"WindowManager"]) {
                const int mi = snap.monitorAt(rect.center());
                if (mi >= 0)
                    stageRects[mi] = stageRects[mi].united(rect);
                continue;
            }
            if (layer != 0)
                continue;
            if (rect.width() < Config::kMinWindowW || rect.height() < Config::kMinWindowH)
                continue;
            const auto number = [info[(__bridge NSString *)kCGWindowNumber] unsignedIntValue];
            windows.push_back({static_cast<WindowHandle>(number), rect});
        }
    }

    // ── 창 엣지 ──────────────────────────────────────
    // 메뉴 막대 · Stage Manager 띠는 모든 창보다 앞이므로 가림으로 취급한다 (그 뒤의 지붕/벽에 올라가지 않음)
    std::vector<QRect> occluders;
    for (const MonitorInfo &m : snap.monitors) {
        if (const std::optional<QRect> bar = menuBarStrip(m))
            occluders.push_back(*bar);
    }
    for (const QRect &r : stageRects) {
        if (!r.isEmpty())
            occluders.push_back(r);
    }
    SurfaceBuilder::addWindowEdges(snap, windows, occluders, m_scale);

    m_snapshot = std::move(snap);
    emit scanned(m_snapshot);
    return m_snapshot;
}

std::optional<QRect> DesktopScanner::liveWindowRect(WindowHandle handle)
{
    if (handle == 0)
        return std::nullopt;
    @autoreleasepool {
        const void *value = reinterpret_cast<const void *>(static_cast<uintptr_t>(handle));
        CFArrayRef ids = CFArrayCreate(kCFAllocatorDefault, &value, 1, nullptr);
        NSArray *list = CFBridgingRelease(CGWindowListCreateDescriptionFromArray(ids));
        CFRelease(ids);
        NSDictionary *info = list.firstObject;
        // 닫힘 / 최소화 / 다른 Space 로 이동 → 화면에 없음 → nullopt (고양이 낙하)
        if (!info || ![info[(__bridge NSString *)kCGWindowIsOnscreen] boolValue])
            return std::nullopt;
        const QRect rect = boundsOf(info);
        if (rect.isEmpty())
            return std::nullopt;
        return rect;
    }
}
