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
// - 바탕화면 위젯 (macOS 14+): 알림 센터(com.apple.notificationcenterui) 소유, 바탕화면 레벨(layer < 0) 창.
//   모든 일반 창보다 뒤이므로 창 목록 맨 뒤에 붙여 앞 창 가림을 그대로 받는다. 프로세스 이름은 언어마다 달라 번들 ID 로 판별
// - WindowHandle = kCGWindowNumber
// - 전체화면 (Phase 4): 맨 앞 일반 창(layer 0)이 모니터를 완전히 덮으면 그 모니터의 고양이를 숨긴다 (보더리스 전체화면 게임 등).
//   확대(zoom) 창은 메뉴 막대를 덮지 않아 해당하지 않는다. 기본 전체화면 Space 의 창도 화면을 덮으므로 같이 숨는다

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

bool isWidgetOwner(pid_t pid)
{
    NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
    return [app.bundleIdentifier isEqualToString:@"com.apple.notificationcenterui"];
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
    std::vector<SurfaceBuilder::WindowRect> widgets;   // 바탕화면 위젯 (windows 뒤에 붙인다)
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
            if (layer > 0)
                continue;   // Dock (화면 전체 크기, layer 20) / 메뉴 막대 / 알림 배너 등
            const QRect rect = boundsOf(info);
            if (rect.isEmpty())
                continue;
            const auto number = [info[(__bridge NSString *)kCGWindowNumber] unsignedIntValue];
            if (layer < 0) {
                if (isWidgetOwner(pid) && rect.width() >= Config::kMinWindowW && rect.height() >= Config::kMinWindowH)
                    widgets.push_back({static_cast<WindowHandle>(number), rect});
                continue;
            }

            // Stage Manager 띠 썸네일 (layer 0). 같은 자리 뒤에 원래 앱 창이 썸네일 크기로 있어 앞쪽 가림으로도 빠진다
            // 띠가 숨었을 때는 화면 밖(음수 x)에 있고, 화면 밖 먼 곳(1e9)에 1x1 창도 둔다 → 모니터와 겹치는 것만 합친다
            if (stage && [info[(__bridge NSString *)kCGWindowOwnerName] isEqualToString:@"WindowManager"]) {
                for (size_t mi = 0; mi < snap.monitors.size(); ++mi) {
                    if (snap.monitors[mi].bounds.intersects(rect)) {
                        stageRects[mi] = stageRects[mi].united(rect);
                        break;
                    }
                }
                continue;
            }
            if (rect.width() < Config::kMinWindowW || rect.height() < Config::kMinWindowH)
                continue;
            windows.push_back({static_cast<WindowHandle>(number), rect});
        }
    }

    // 전체화면 판정은 일반 창만 본다 (위젯은 화면을 덮지 않는다)
    const std::optional<QRect> frontRect = windows.empty() ? std::nullopt : std::optional<QRect>(windows.front().rect);
    windows.insert(windows.end(), widgets.begin(), widgets.end());

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

    // ── 전체화면 감지 ────────────────────────────────
    if (frontRect)
        SurfaceBuilder::markCoveredMonitors(snap, *frontRect);

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
