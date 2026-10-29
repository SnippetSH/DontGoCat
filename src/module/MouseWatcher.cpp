#include "MouseWatcher.hpp"

#include "Config.hpp"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include "MacNative.hpp"
#endif

namespace {

QPoint cursorPos()
{
#ifdef Q_OS_WIN
    POINT pt{};
    GetCursorPos(&pt);
    return QPoint(pt.x, pt.y);
#elif defined(Q_OS_MACOS)
    return MacNative::cursorPos();
#endif
}

} // namespace

MouseWatcher::MouseWatcher(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(Config::kMousePollMs);
    connect(&m_timer, &QTimer::timeout, this, &MouseWatcher::poll);
}

void MouseWatcher::start()
{
    m_pos = cursorPos();
    m_expectedAfterNudge.reset();
    m_idleSignaled = false;
    m_sinceMove.start();
    m_timer.start();
}

void MouseWatcher::stop()
{
    m_timer.stop();
}

qint64 MouseWatcher::idleMs() const
{
    return m_sinceMove.isValid() ? m_sinceMove.elapsed() : 0;
}

bool MouseWatcher::anyButtonDown() const
{
#ifdef Q_OS_WIN
    // 최상위 비트 = 현재 눌림. 좌우 교체 설정과 무관하게 세 버튼을 모두 본다
    return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0
        || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0
        || (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
#elif defined(Q_OS_MACOS)
    return MacNative::anyButtonDown();
#endif
}

bool MouseWatcher::leftButtonDown() const
{
#ifdef Q_OS_WIN
    return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
#elif defined(Q_OS_MACOS)
    return MacNative::leftButtonDown();
#endif
}

bool MouseWatcher::shiftDown() const
{
#ifdef Q_OS_WIN
    return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
#elif defined(Q_OS_MACOS)
    return MacNative::shiftDown();
#endif
}

bool MouseWatcher::nudge(int dx, int dy)
{
    if (m_nudgeHook)
        return m_nudgeHook(dx, dy);

    if (anyButtonDown())
        return false;

    const QPoint before = cursorPos();
#ifdef Q_OS_WIN
    if (!SetCursorPos(before.x() + dx, before.y() + dy))
        return false;
#elif defined(Q_OS_MACOS)
    if (!MacNative::warpCursor(before + QPoint(dx, dy)))
        return false;
#endif

    // 화면 가장자리에서 잘릴 수 있으므로 실제 도착 위치를 기대값으로 기록한다
    const QPoint after = cursorPos();
    if (after != m_pos)
        m_expectedAfterNudge = after;
    return true;
}

void MouseWatcher::poll()
{
    const QPoint now = cursorPos();
    if (now != m_pos) {
        m_pos = now;
        if (m_expectedAfterNudge && *m_expectedAfterNudge == now) {
            // 우리가 옮긴 것 → 사용자 이동으로 치지 않는다
            m_expectedAfterNudge.reset();
        } else {
            m_expectedAfterNudge.reset();
            m_sinceMove.restart();
            m_idleSignaled = false;
            emit userMoved(now);
        }
    }

    if (!m_idleSignaled && idleMs() >= Config::kMouseIdleMs) {
        m_idleSignaled = true;
        emit becameIdle(m_pos);
    }
}
