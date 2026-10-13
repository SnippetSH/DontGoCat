#pragma once

#include <QByteArray>
#include <QSize>

// 물리 px 고정 크기 창(QT_ENABLE_HIGHDPI_SCALING=0)이 DPI 가 다른 모니터로 넘어갈 때
// Windows / Qt 가 창 크기를 DPI 비율만큼 바꾸지 못하게 막는다.
// (예: 125% 모니터를 다녀온 고양이 창이 100% 모니터에서 96 → 77 로 줄어 오른쪽이 잘리던 문제)
//
// 사용: 고정 크기 QWidget 의 nativeEvent 에서 먼저 호출하고, true 면 그대로 true 를 돌려준다.
//   bool X::nativeEvent(const QByteArray &t, void *m, qintptr *r)
//   {
//       if (DpiSizeLock::handle(t, m, r, size()))
//           return true;
//       return QWidget::nativeEvent(t, m, r);
//   }
namespace DpiSizeLock {

// WM_GETDPISCALEDSIZE 이면 새 DPI 에서의 창 크기를 fixedSize 로 채우고 *result = TRUE, true 를 돌려준다.
// 그 밖의 메시지는 건드리지 않고 false. (테두리 없는 창 전용: 창 크기 = 클라이언트 크기)
bool handle(const QByteArray &eventType, void *message, qintptr *result, QSize fixedSize);

} // namespace DpiSizeLock
