#include "MacNative.hpp"

#include <QGuiApplication>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

// ARC 로 컴파일된다 (CMakeLists.txt: -fobjc-arc)

namespace {

NSWindow *windowOf(WId id)
{
    // offscreen 플랫폼(테스트)의 WId 는 NSView 가 아니다
    if (!id || QGuiApplication::platformName() != QLatin1String("cocoa"))
        return nil;
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(id);
    return view.window;
}

NSSound *g_sound = nil;   // 재생 중인 소리 (재생이 끝날 때까지 살아 있어야 한다)

} // namespace

namespace MacNative {

void configureOverlayWindow(WId id)
{
    NSWindow *w = windowOf(id);
    if (!w)
        return;
    w.level = NSPopUpMenuWindowLevel + 1;
    w.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces
        | NSWindowCollectionBehaviorStationary
        | NSWindowCollectionBehaviorIgnoresCycle;
    w.hidesOnDeactivate = NO;   // Qt::Tool(NSPanel) 기본은 YES → 앱이 비활성이면 사라진다
    w.hasShadow = NO;
}

void setIgnoresMouseEvents(WId id, bool ignore)
{
    if (NSWindow *w = windowOf(id))
        w.ignoresMouseEvents = ignore ? YES : NO;
}

void orderFront(WId id)
{
    NSWindow *w = windowOf(id);
    if (w && w.visible)
        [w orderFrontRegardless];   // 앱을 활성화하지 않고 같은 level 안에서 맨 앞으로
}

QPoint cursorPos()
{
    CGEventRef ev = CGEventCreate(nullptr);
    if (!ev)
        return {};
    const CGPoint p = CGEventGetLocation(ev);   // 좌상단 원점 전역 좌표 (포인트)
    CFRelease(ev);
    return QPoint(qRound(p.x), qRound(p.y));
}

bool leftButtonDown()
{
    return CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonLeft);
}

bool anyButtonDown()
{
    return CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonLeft)
        || CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonRight)
        || CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonCenter);
}

bool shiftDown()
{
    return (CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState) & kCGEventFlagMaskShift) != 0;
}

bool warpCursor(QPoint pos)
{
    if (CGWarpMouseCursorPosition(CGPointMake(pos.x(), pos.y())) != kCGErrorSuccess)
        return false;
    // 워프 직후 물리 마우스 입력이 잠깐 멈추는 것을 막는다
    CGAssociateMouseAndMouseCursorPosition(true);
    return true;
}

void playWav(const QByteArray &wav)
{
    stopSound();
    if (wav.isEmpty())
        return;
    NSData *data = [NSData dataWithBytes:wav.constData() length:static_cast<NSUInteger>(wav.size())];
    g_sound = [[NSSound alloc] initWithData:data];
    [g_sound play];
}

void stopSound()
{
    if (g_sound) {
        [g_sound stop];
        g_sound = nil;
    }
}

} // namespace MacNative
