// DesktopScanner / MouseWatcher 점검용 콘솔 도구.
//   ccat_scan_dump            : 1회 스캔 결과 출력
//   ccat_scan_dump --watch    : 1초마다 다시 출력 (Ctrl+C 로 종료)
//   ccat_scan_dump --mouse    : MouseWatcher 시그널 출력 (Ctrl+C 로 종료)
//   --scale N                 : 고양이 배율 (기본 Config::kDefaultScale)

#include "Config.hpp"
#include "DesktopScanner.hpp"
#include "MouseWatcher.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <cstdio>

namespace {

QString rectStr(const QRect &r)
{
    return QString("(%1,%2 %3x%4)").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
}

QString titleOf(WindowHandle h)
{
    wchar_t buf[256] = {};
    GetWindowTextW(reinterpret_cast<HWND>(h), buf, 256);
    return QString::fromWCharArray(buf);
}

QString classOf(WindowHandle h)
{
    wchar_t buf[256] = {};
    GetClassNameW(reinterpret_cast<HWND>(h), buf, 256);
    return QString::fromWCharArray(buf);
}

const char *kindStr(SurfaceKind k)
{
    switch (k) {
    case SurfaceKind::Floor:         return "Floor";
    case SurfaceKind::WallLeftSide:  return "WallL";
    case SurfaceKind::WallRightSide: return "WallR";
    }
    return "?";
}

void out(const QString &s)
{
    std::fputs(s.toUtf8().constData(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void dumpTaskbar()
{
    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    const bool autoHide = (SHAppBarMessage(ABM_GETSTATE, &abd) & ABS_AUTOHIDE) != 0;
    QString line = QString("taskbar: autohide=%1").arg(autoHide);
    if (const HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr)) {
        RECT rc{};
        GetWindowRect(tray, &rc);
        line += QString(" primary=(%1,%2 %3x%4) visible=%5").arg(rc.left).arg(rc.top)
                    .arg(rc.right - rc.left).arg(rc.bottom - rc.top).arg(IsWindowVisible(tray) != 0);
    }
    out(line);
}

void dump(const DesktopSnapshot &snap)
{
    dumpTaskbar();
    out(QString("=== scan t=%1ms  virtual=%2  tray=%3")
            .arg(snap.timestampMs).arg(rectStr(snap.virtualBounds)).arg(rectStr(snap.trayRect)));

    for (int i = 0; i < static_cast<int>(snap.monitors.size()); ++i) {
        const MonitorInfo &m = snap.monitors[i];
        out(QString("monitor[%1] bounds=%2 work=%3 floorY=%4 taskbarFloor=%5 fullscreen=%6")
                .arg(i).arg(rectStr(m.bounds)).arg(rectStr(m.workArea)).arg(m.floorY)
                .arg(m.hasTaskbarFloor).arg(m.fullscreen));
    }

    out(QString("segments: %1").arg(snap.segments.size()));
    for (const SurfaceSegment &s : snap.segments) {
        const bool floor = s.id.kind == SurfaceKind::Floor;
        QString line = QString("  %1 line=%2 [%3,%4) len=%5 ")
                           .arg(kindStr(s.id.kind), -5).arg(s.line, 5).arg(s.a, 5).arg(s.b, 5).arg(s.length(), 5);
        if (s.id.owner == 0) {
            line += QString("monitor-floor #%1").arg(s.id.monitor);
        } else {
            line += QString("hwnd=0x%1 [%2] \"%3\" owner=%4")
                        .arg(s.id.owner, 0, 16).arg(classOf(s.id.owner)).arg(titleOf(s.id.owner))
                        .arg(rectStr(s.ownerRect));
        }
        Q_UNUSED(floor);
        out(line);
    }
}

} // namespace

int main(int argc, char *argv[])
{
    // Qt GUI 앱이 아니므로 물리 px 를 얻으려면 직접 DPI 인식을 켠다
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();

    int scale = Config::kDefaultScale;
    const int si = static_cast<int>(args.indexOf("--scale"));
    if (si >= 0 && si + 1 < args.size())
        scale = args.at(si + 1).toInt();

    DesktopScanner scanner;
    scanner.setScale(scale);

    if (args.contains("--mouse")) {
        MouseWatcher watcher;
        QObject::connect(&watcher, &MouseWatcher::userMoved, [](QPoint p) {
            out(QString("userMoved (%1,%2)").arg(p.x()).arg(p.y()));
        });
        QObject::connect(&watcher, &MouseWatcher::becameIdle, [](QPoint p) {
            out(QString("becameIdle (%1,%2)").arg(p.x()).arg(p.y()));
        });
        QTimer status;
        status.setInterval(1000);
        QObject::connect(&status, &QTimer::timeout, [&watcher] {
            out(QString("  pos=(%1,%2) idle=%3ms buttons=%4")
                    .arg(watcher.pos().x()).arg(watcher.pos().y()).arg(watcher.idleMs())
                    .arg(watcher.anyButtonDown()));
        });
        watcher.start();
        status.start();
        // --mouse-seconds N : N 초 후 자동 종료 (자동 점검용)
        const int ti = static_cast<int>(args.indexOf("--mouse-seconds"));
        if (ti >= 0 && ti + 1 < args.size())
            QTimer::singleShot(args.at(ti + 1).toInt() * 1000, &app, &QCoreApplication::quit);
        return app.exec();
    }

    dump(scanner.scanNow());
    if (!args.contains("--watch"))
        return 0;

    QTimer timer;
    timer.setInterval(1000);
    QObject::connect(&timer, &QTimer::timeout, [&scanner] { dump(scanner.scanNow()); });
    timer.start();
    const int ti = static_cast<int>(args.indexOf("--watch-seconds"));
    if (ti >= 0 && ti + 1 < args.size())
        QTimer::singleShot(args.at(ti + 1).toInt() * 1000, &app, &QCoreApplication::quit);
    return app.exec();
}
