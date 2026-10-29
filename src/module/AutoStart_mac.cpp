#include "AutoStart.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>

// macOS 로그인 시 자동 실행 (README 5.11 의 macOS 판).
// ~/Library/LaunchAgents/com.dontgocat.app.plist 에 LaunchAgent 를 쓰고 지운다.
// 모든 함수의 runKey 는 plist 파일 경로이며, 비우면 실제 LaunchAgents 경로를 쓴다 (테스트는 임시 경로를 넘긴다).
// plist 쓰기/읽기는 QSettings::NativeFormat (macOS 에서 파일 경로를 주면 plist 파일)로 한다.

namespace {

constexpr const char *kLabel = "com.dontgocat.app";   // Info.plist CFBundleIdentifier 와 같다

QString plistOf(const QString &runKey)
{
    return runKey.isEmpty() ? AutoStart::defaultRunKey() : runKey;
}

} // namespace

namespace AutoStart {

QString defaultRunKey()
{
    return QDir::homePath() + QStringLiteral("/Library/LaunchAgents/") + QLatin1String(kLabel)
        + QStringLiteral(".plist");
}

QString currentCommand()
{
    // 번들 안 실행 파일 (DontGoCat.app/Contents/MacOS/DontGoCat). ProgramArguments 배열 원소라 따옴표 없음
    return QCoreApplication::applicationFilePath();
}

QString registeredCommand(const QString &runKey)
{
    const QString path = plistOf(runKey);
    if (!QFile::exists(path))
        return {};
    QSettings plist(path, QSettings::NativeFormat);
    const QStringList args = plist.value(QStringLiteral("ProgramArguments")).toStringList();
    return args.isEmpty() ? QString() : args.first();
}

bool isEnabled(const QString &runKey)
{
    return QFile::exists(plistOf(runKey));
}

bool setEnabled(bool enabled, const QString &runKey)
{
    const QString path = plistOf(runKey);
    if (!enabled)
        return !QFile::exists(path) || QFile::remove(path);

    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QSettings plist(path, QSettings::NativeFormat);
    plist.clear();
    plist.setValue(QStringLiteral("Label"), QLatin1String(kLabel));
    plist.setValue(QStringLiteral("ProgramArguments"), QStringList{currentCommand()});
    plist.setValue(QStringLiteral("RunAtLoad"), true);
    plist.setValue(QStringLiteral("ProcessType"), QStringLiteral("Interactive"));
    plist.sync();
    return plist.status() == QSettings::NoError;
}

void refreshPath(const QString &runKey)
{
    if (!isEnabled(runKey))
        return;
    // 앱을 다른 위치로 옮겼으면 현재 경로로 다시 쓴다
    if (registeredCommand(runKey) != currentCommand())
        setEnabled(true, runKey);
}

} // namespace AutoStart
