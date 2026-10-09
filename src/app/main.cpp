#include "AutoStart.hpp"
#include "CatApp.hpp"
#include "Config.hpp"

#include <QApplication>

#include <algorithm>
#include <optional>

int main(int argc, char *argv[])
{
    // 모든 좌표를 물리 px 로 맞춘다 (README 3.1)
    qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");

    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName(Config::kSettingsOrg);
    QCoreApplication::setApplicationName(Config::kSettingsApp);

    // --scale N (1~4)
    std::optional<int> scale;
    const QStringList args = app.arguments();
    if (const qsizetype i = args.indexOf("--scale"); i >= 0 && i + 1 < args.size()) {
        bool ok = false;
        const int n = args[i + 1].toInt(&ok);
        if (ok)
            scale = std::clamp(n, Config::kMinScale, Config::kMaxScale);
    }

    AutoStart::refreshPath();   // 자동 시작이 켜져 있는데 exe 경로가 바뀌었으면 현재 경로로 갱신 (README 5.11)

    CatApp cat(scale);
    cat.start();
    return app.exec();
}
