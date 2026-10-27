#include "AppSettingsFile.hpp"
#include "AutoStart.hpp"
#include "CatApp.hpp"
#include "Config.hpp"
#include "FileLogger.hpp"

#include <QApplication>
#include <QDir>
#include <QLockFile>
#include <QStandardPaths>

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

    // 중복 실행 방지: 이미 실행 중이면 조용히 끝낸다 (통계 저장이 서로 덮어쓰지 않게, 로그 파일도 비우지 않게 로그보다 먼저)
    // 나이 기준 stale 판정은 끄고(0), 잠근 프로세스가 사라진 경우에만 잠금을 넘겨받는다
    QLockFile instanceLock(QDir::temp().filePath(QLatin1String(Config::kInstanceLockFileName)));
    instanceLock.setStaleLockTime(0);
    if (!instanceLock.tryLock(0))
        return 0;

    // 로그 (settings.txt 의 enable-log). exe 옆에 못 쓰면 %LOCALAPPDATA%\DontGoCat
    const QString settingsPath = AppSettingsFile::defaultPath();
    if (AppSettingsFile::load(settingsPath).enableLog) {
        const QString fallbackDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QLatin1Char('/') + QLatin1String(Config::kSettingsApp);
        const QString logPath = FileLogger::start({QCoreApplication::applicationDirPath(), fallbackDir});
        if (!logPath.isEmpty())
            qInfo().noquote() << "DontGoCat" << CCAT_VERSION << "log started, settings" << QDir::toNativeSeparators(settingsPath);
    }

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

    int rc = 0;
    {
        CatApp cat(scale);
        cat.start();
        rc = app.exec();
    }   // CatApp 정리 중 메시지까지 로그에 남긴 뒤 닫는다
    FileLogger::stop();
    return rc;
}
