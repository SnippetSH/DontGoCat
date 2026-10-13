// DPI 가 다른 모니터 사이를 넘어갈 때 고정 크기 창(CatOverlay, QuitPopup)이 DPI 비율로 커지거나 줄지 않는지 테스트.
// 앱은 QT_ENABLE_HIGHDPI_SCALING=0 으로 물리 px 만 쓴다. 125% 모니터를 다녀온 고양이 창이 96 → 77 로 줄어
// 스프라이트 오른쪽이 잘리던 문제의 회귀 방지.
// 실제 Windows 플랫폼(offscreen 아님)으로 네이티브 창(HWND)에 진짜 WM_GETDPISCALEDSIZE 를 보낸다.
// 창은 winId() 로 네이티브만 만들고 show() 하지 않으므로 화면에 아무것도 뜨지 않는다 (GetWindowRect 는 숨긴 창에도 동작).
#include "TestCheck.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "CatOverlay.hpp"
#include "CatSprite.hpp"
#include "Config.hpp"
#include "DpiSizeLock.hpp"
#include "QuitPopup.hpp"

#include <QApplication>
#include <QByteArray>
#include <QImage>

#include <cstdio>

#ifndef WM_GETDPISCALEDSIZE
#define WM_GETDPISCALEDSIZE 0x02E4
#endif

static HWND toHwnd(WId id)
{
    return reinterpret_cast<HWND>(id);
}

// 숨긴 네이티브 창의 실제 사각형 (물리 px)
static QRect nativeRect(HWND hwnd)
{
    RECT r{};
    GetWindowRect(hwnd, &r);
    return QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
}

// 창에 WM_GETDPISCALEDSIZE 를 보내고 (반환값, 채워진 크기) 를 돌려준다. 크기는 일부러 틀린 값으로 미리 채운다.
struct DpiReply
{
    LRESULT ret;
    SIZE size;
};

static DpiReply sendGetDpiScaledSize(HWND hwnd, WPARAM dpi, SIZE initial = SIZE{77, 77})
{
    DpiReply reply{0, initial};
    reply.ret = SendMessageW(hwnd, WM_GETDPISCALEDSIZE, dpi, reinterpret_cast<LPARAM>(&reply.size));
    return reply;
}

// ---------------------------------------------------------------------------------------------
static void testHandleDirect()
{
    std::printf("[1] DpiSizeLock::handle direct\n");
    const QByteArray generic("windows_generic_MSG");
    const QSize fixed(96, 96);

    // WM_GETDPISCALEDSIZE → 크기를 채우고 TRUE
    {
        SIZE sz{77, 77};
        MSG msg{};
        msg.message = WM_GETDPISCALEDSIZE;
        msg.wParam = 120;
        msg.lParam = reinterpret_cast<LPARAM>(&sz);
        qintptr result = 0;
        const bool handled = DpiSizeLock::handle(generic, &msg, &result, fixed);
        CHECK(handled, "handled WM_GETDPISCALEDSIZE");
        CHECK(result == TRUE, "result TRUE (%lld)", static_cast<long long>(result));
        CHECK(sz.cx == 96 && sz.cy == 96, "size written %ldx%ld", sz.cx, sz.cy);
    }

    // 직사각형 크기도 그대로 쓴다 (가로 세로 구분)
    {
        SIZE sz{1, 2};
        MSG msg{};
        msg.message = WM_GETDPISCALEDSIZE;
        msg.wParam = 144;
        msg.lParam = reinterpret_cast<LPARAM>(&sz);
        qintptr result = 0;
        const bool handled = DpiSizeLock::handle(generic, &msg, &result, QSize(200, 120));
        CHECK(handled && result == TRUE, "handled non-square");
        CHECK(sz.cx == 200 && sz.cy == 120, "non-square size %ldx%ld", sz.cx, sz.cy);
    }

    // 다른 메시지 → 건드리지 않음
    {
        SIZE sz{77, 77};
        MSG msg{};
        msg.message = WM_MOVE;
        msg.wParam = 120;
        msg.lParam = reinterpret_cast<LPARAM>(&sz);
        qintptr result = 12345;
        const bool handled = DpiSizeLock::handle(generic, &msg, &result, fixed);
        CHECK(!handled, "WM_MOVE not handled");
        CHECK(sz.cx == 77 && sz.cy == 77, "WM_MOVE leaves size untouched %ldx%ld", sz.cx, sz.cy);
        CHECK(result == 12345, "WM_MOVE leaves result untouched (%lld)", static_cast<long long>(result));
    }

    // 이벤트 종류가 다르면 무시
    {
        SIZE sz{77, 77};
        MSG msg{};
        msg.message = WM_GETDPISCALEDSIZE;
        msg.wParam = 120;
        msg.lParam = reinterpret_cast<LPARAM>(&sz);
        qintptr result = 0;
        const bool handled = DpiSizeLock::handle(QByteArray("xcb_generic_event_t"), &msg, &result, fixed);
        CHECK(!handled, "other eventType not handled");
        CHECK(sz.cx == 77 && sz.cy == 77, "other eventType leaves size untouched %ldx%ld", sz.cx, sz.cy);
        CHECK(result == 0, "other eventType leaves result untouched");
    }

    // 빈 fixedSize → 무시 (유효하지 않은 크기를 쓰지 않는다)
    {
        SIZE sz{77, 77};
        MSG msg{};
        msg.message = WM_GETDPISCALEDSIZE;
        msg.wParam = 120;
        msg.lParam = reinterpret_cast<LPARAM>(&sz);
        qintptr result = 0;
        const bool handled = DpiSizeLock::handle(generic, &msg, &result, QSize());
        CHECK(!handled, "empty fixedSize not handled");
        CHECK(sz.cx == 77 && sz.cy == 77, "empty fixedSize leaves size untouched %ldx%ld", sz.cx, sz.cy);
        const bool handled0 = DpiSizeLock::handle(generic, &msg, &result, QSize(0, 0));
        CHECK(!handled0, "zero fixedSize not handled");
    }
}

// ---------------------------------------------------------------------------------------------
// 실제 HWND 에 WM_GETDPISCALEDSIZE → 어느 DPI 로 가든 32*scale 정사각형
static void testOverlayDpiScaledSize()
{
    std::printf("[2] CatOverlay WM_GETDPISCALEDSIZE\n");
    CatOverlay o;
    const WPARAM dpis[] = {96, 120, 144};
    for (int s = Config::kMinScale; s <= Config::kMaxScale; ++s) {
        o.setScale(s);
        HWND hwnd = toHwnd(o.winId());
        CHECK(hwnd != nullptr, "scale %d has native window", s);
        if (!hwnd)
            continue;
        const int side = CatSprite::Width * s;
        for (WPARAM dpi : dpis) {
            const DpiReply r = sendGetDpiScaledSize(hwnd, dpi);
            CHECK(r.ret != 0, "scale %d dpi %llu returns nonzero", s, static_cast<unsigned long long>(dpi));
            CHECK(r.size.cx == side && r.size.cy == side, "scale %d dpi %llu size %ldx%ld (want %d)", s,
                  static_cast<unsigned long long>(dpi), r.size.cx, r.size.cy, side);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// place() 자기 치유: Qt 가 WM_DPICHANGED 로 창을 줄이거나 밀어도 다음 setFrame 에서 크기 / 위치가 복구된다
static void testOverlaySelfHeal()
{
    std::printf("[3] CatOverlay self-heal\n");
    QImage img(CatSprite::Width, CatSprite::Height, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    const QPoint anchor(400, 400);

    for (int s = Config::kMinScale; s <= Config::kMaxScale; ++s) {
        CatOverlay o;
        o.setScale(s);
        HWND hwnd = toHwnd(o.winId());
        CHECK(hwnd != nullptr, "scale %d has native window", s);
        if (!hwnd)
            continue;
        const int side = CatSprite::Width * s;
        const QPoint topLeft = anchor - CatSprite::anchorIn(CatGravity::Down) * s;
        const QRect want(topLeft, QSize(side, side));

        o.setFrame(img, anchor, CatGravity::Down);
        CHECK(nativeRect(hwnd) == want, "scale %d first placement (%d,%d %dx%d)", s, nativeRect(hwnd).x(),
              nativeRect(hwnd).y(), nativeRect(hwnd).width(), nativeRect(hwnd).height());

        // (a) 크기와 위치가 같이 틀어짐: DPI 변경으로 77x77 로 줄고 이동
        SetWindowPos(hwnd, nullptr, topLeft.x() + 5, topLeft.y() - 3, 77, 77, SWP_NOACTIVATE | SWP_NOZORDER);
        o.setFrame(img, anchor, CatGravity::Down);   // 앵커가 그대로여도
        QRect got = nativeRect(hwnd);
        CHECK(got.size() == want.size(), "scale %d size healed %dx%d (want %d)", s, got.width(), got.height(), side);
        CHECK(got.topLeft() == topLeft, "scale %d pos healed (%d,%d) want (%d,%d)", s, got.x(), got.y(), topLeft.x(),
              topLeft.y());

        // (b) 크기만 틀어짐
        SetWindowPos(hwnd, nullptr, topLeft.x(), topLeft.y(), 77, 77, SWP_NOACTIVATE | SWP_NOZORDER);
        o.setFrame(img, anchor, CatGravity::Down);
        got = nativeRect(hwnd);
        CHECK(got == want, "scale %d size-only healed (%d,%d %dx%d)", s, got.x(), got.y(), got.width(), got.height());

        // (c) 위치만 몇 px 밀림 (크기는 정상)
        SetWindowPos(hwnd, nullptr, topLeft.x() + 4, topLeft.y() + 2, side, side, SWP_NOACTIVATE | SWP_NOZORDER);
        o.setFrame(img, anchor, CatGravity::Down);
        got = nativeRect(hwnd);
        CHECK(got == want, "scale %d pos-only healed (%d,%d %dx%d)", s, got.x(), got.y(), got.width(), got.height());

        // 정상 상태에서 다시 불러도 그대로
        o.setFrame(img, anchor, CatGravity::Down);
        CHECK(nativeRect(hwnd) == want, "scale %d stable when untouched", s);

        // 앵커가 움직이면 새 위치로
        const QPoint anchor2 = anchor + QPoint(30, -20);
        o.setFrame(img, anchor2, CatGravity::Down);
        const QRect want2(anchor2 - CatSprite::anchorIn(CatGravity::Down) * s, QSize(side, side));
        CHECK(nativeRect(hwnd) == want2, "scale %d moves with anchor", s);
    }
}

// ---------------------------------------------------------------------------------------------
static void testPopupDpiScaledSize()
{
    std::printf("[4] QuitPopup WM_GETDPISCALEDSIZE\n");
    QuitPopup p;
    p.setScale(3);   // relayout → setFixedSize
    HWND hwnd = toHwnd(p.winId());
    CHECK(hwnd != nullptr, "popup has native window");
    if (!hwnd)
        return;
    const QSize sz = p.size();
    CHECK(sz.isValid() && sz.width() > 0 && sz.height() > 0, "popup size %dx%d", sz.width(), sz.height());

    for (WPARAM dpi : {WPARAM(96), WPARAM(120), WPARAM(144)}) {
        const DpiReply r = sendGetDpiScaledSize(hwnd, dpi);
        CHECK(r.ret != 0, "dpi %llu returns nonzero", static_cast<unsigned long long>(dpi));
        CHECK(r.size.cx == sz.width() && r.size.cy == sz.height(), "dpi %llu size %ldx%ld (want %dx%d)",
              static_cast<unsigned long long>(dpi), r.size.cx, r.size.cy, sz.width(), sz.height());
    }

    // 배율을 바꿔 크기가 달라져도 최신 size() 를 돌려준다
    p.setScale(1);
    const QSize sz1 = p.size();
    const DpiReply r1 = sendGetDpiScaledSize(hwnd, 120);
    CHECK(r1.ret != 0 && r1.size.cx == sz1.width() && r1.size.cy == sz1.height(), "after setScale(1) size %ldx%ld (want %dx%d)",
          r1.size.cx, r1.size.cy, sz1.width(), sz1.height());
}

int main(int argc, char **argv)
{
    qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
    QApplication app(argc, argv);

    testHandleDirect();
    testOverlayDpiScaledSize();
    testOverlaySelfHeal();
    testPopupDpiScaledSize();
    return testing::testResult("test_dpi");
}
