#include "AppSettingsFile.hpp"

#include "Config.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace {

// "true" / "false" (대소문자 무시). 그 밖의 값이면 current 유지
bool parseBool(const QString &value, bool current)
{
    if (value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0)
        return true;
    if (value.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0)
        return false;
    return current;
}

} // namespace

AppFileSettings AppSettingsFile::parse(const QString &text)
{
    AppFileSettings result;
    QString body = text;
    if (body.startsWith(QChar(0xFEFF)))   // UTF-8 BOM
        body.remove(0, 1);
    const QStringList lines = body.split(QLatin1Char('\n'));   // CRLF 의 '\r' 은 trimmed() 가 지운다
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const qsizetype colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString key = line.left(colon).trimmed();
        const QString value = line.mid(colon + 1).trimmed();
        if (key.compare(QLatin1String("enable-log"), Qt::CaseInsensitive) == 0)
            result.enableLog = parseBool(value, result.enableLog);
    }
    return result;
}

AppFileSettings AppSettingsFile::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return parse(QString::fromUtf8(file.readAll()));
}

QString AppSettingsFile::defaultPath()
{
#ifdef Q_OS_MACOS
    return QDir::homePath() + QStringLiteral("/Library/Application Support/") + QLatin1String(Config::kSettingsApp)
        + QLatin1Char('/') + QLatin1String(Config::kSettingsFileName);
#else
    return QCoreApplication::applicationDirPath() + QLatin1Char('/') + QLatin1String(Config::kSettingsFileName);
#endif
}

bool AppSettingsFile::writeDefaultIfMissing(const QString &path)
{
    if (QFile::exists(path))
        return true;
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    file.write("# DontGoCat settings. Edit with a text editor, then restart DontGoCat.\n"
               "# Lines starting with '#' are comments.\n"
               "\n"
               "# enable-log: true  -> write DontGoCat.log in this folder (recreated on every start).\n"
               "# enable-log: false -> no log file.\n"
               "enable-log: true\n");
    return true;
}
