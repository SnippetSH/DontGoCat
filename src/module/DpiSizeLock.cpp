#include <QtGlobal>   // Q_OS_WIN 정의용 (windows.h 보다 먼저)

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "DpiSizeLock.hpp"

#ifndef WM_GETDPISCALEDSIZE
#define WM_GETDPISCALEDSIZE 0x02E4
#endif

namespace DpiSizeLock {

bool handle(const QByteArray &eventType, void *message, qintptr *result, QSize fixedSize)
{
#ifndef Q_OS_WIN
    // macOS 등: 포인트 좌표라 DPI 로 창 크기가 바뀌지 않는다
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
    Q_UNUSED(fixedSize)
    return false;
#else
    if (eventType != "windows_generic_MSG" || !message || fixedSize.isEmpty())
        return false;
    const MSG *msg = static_cast<const MSG *>(message);
    if (msg->message != WM_GETDPISCALEDSIZE)
        return false;
    // lParam: 새 DPI 에서 쓸 창 크기 (in/out). TRUE 를 돌려주면 WM_DPICHANGED 의 제안 사각형이 이 크기를 쓴다
    auto *size = reinterpret_cast<SIZE *>(msg->lParam);
    size->cx = fixedSize.width();
    size->cy = fixedSize.height();
    if (result)
        *result = TRUE;
    return true;
#endif
}

} // namespace DpiSizeLock
