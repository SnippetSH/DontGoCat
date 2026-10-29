#pragma once

#include <QByteArray>
#include <QPoint>
#include <QtGlobal>
#include <qwindowdefs.h>   // WId

#include <functional>

// macOS 네이티브 보조 (MacNative.mm, Obj-C++). APPLE 빌드에서만 컴파일·링크된다.
// 헤더는 AppKit 을 노출하지 않으므로 일반 .cpp 에서 #ifdef Q_OS_MACOS 안에서 include 한다.
// 좌표는 모두 Qt / CG 전역 좌표 (좌상단 원점, 포인트 단위).
namespace MacNative {

// ── 창 (WId = NSView*) ─────────────────────────────
// 고양이 / 말풍선 창 (MAC-Conception Phase 1):
//   level = NSPopUpMenuWindowLevel + 1 (Dock·자체 팝업보다 위)
//   collectionBehavior = CanJoinAllSpaces | Stationary | IgnoresCycle (FullScreenAuxiliary 없음 → 전체화면 Space 에 안 나타남)
//   hidesOnDeactivate = NO, hasShadow = NO
// 네이티브 창이 아직 없으면 아무것도 하지 않는다 (showEvent 에서 다시 부른다).
void configureOverlayWindow(WId id);

// 종료 팝업 창: level = NSPopUpMenuWindowLevel (Dock 위, 고양이 창 바로 아래 → 고양이가 버튼을 덮을 수 있다).
// Qt::WindowStaysOnTopHint 만으로는 floating level 이라 바닥(화면 하단)에 붙으면 Dock 뒤에 가려진다
void configurePopupWindow(WId id);

// 마우스 이벤트 통과 (CatOverlay::setClickThrough). NO 여도 알파 0 픽셀의 통과 여부는 실측 필요
void setIgnoresMouseEvents(WId id, bool ignore);

// 최상위 다시 올리기 (활성화하지 않음). Windows 의 SetWindowPos(HWND_TOPMOST) 대응
void orderFront(WId id);

// ── 커서 / 입력 상태 (MouseWatcher) ─────────────────
QPoint cursorPos();
bool leftButtonDown();
bool anyButtonDown();   // 좌/우/가운데
bool shiftDown();
bool warpCursor(QPoint pos);   // 커서를 옮긴다 (손쉬운 사용 권한 불필요)

// ── 소리 (CatVoice) ───────────────────────────────
// 메모리 wav 를 비동기 재생. 재생 중이면 앞 소리를 끊는다
void playWav(const QByteArray &wav);
void stopSound();

// ── 앱 다시 실행 (reopen) ───────────────────────────
// 실행 중에 Finder / Spotlight / Launchpad 로 앱을 다시 열면 새 프로세스 대신 reopen Apple 이벤트(kAEReopenApplication)가 온다.
// 그 처리기를 handler 로 바꾼다. NSApplication 이 시작하며 기본 처리기를 설치하므로 이벤트 루프가 돈 뒤에 부른다
void setReopenHandler(std::function<void()> handler);

} // namespace MacNative
