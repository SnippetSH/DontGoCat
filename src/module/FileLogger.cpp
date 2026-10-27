#include "FileLogger.hpp"

#include "Config.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>

namespace {

QMutex g_mutex;
QFile g_file;
QtMessageHandler g_previous = nullptr;
bool g_active = false;

char typeLetter(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return 'D';
    case QtInfoMsg: return 'I';
    case QtWarningMsg: return 'W';
    case QtCriticalMsg: return 'C';
    case QtFatalMsg: return 'F';
    }
    return 'I';
}

void handler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    QtMessageHandler previous = nullptr;
    {
        QMutexLocker lock(&g_mutex);
        previous = g_previous;
        if (g_active && g_file.isOpen()) {
            const QString category = (ctx.category && *ctx.category) ? QString::fromUtf8(ctx.category) : QStringLiteral("default");
            const QString line = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
                + QLatin1Char(' ') + QLatin1Char(typeLetter(type)) + QLatin1String(" [") + category + QLatin1String("] ")
                + msg + QLatin1Char('\n');
            g_file.write(line.toUtf8());
            g_file.flush();
        }
    }
    if (previous)
        previous(type, ctx, msg);
}

} // namespace

QString FileLogger::start(const QStringList &dirs)
{
    QMutexLocker lock(&g_mutex);
    if (g_active)
        return g_file.fileName();
    for (const QString &dir : dirs) {
        if (dir.isEmpty() || !QDir().mkpath(dir))
            continue;
        g_file.setFileName(QDir(dir).filePath(QLatin1String(Config::kLogFileName)));
        if (!g_file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            continue;
        g_active = true;
        g_previous = qInstallMessageHandler(handler);
        return g_file.fileName();
    }
    return {};
}

void FileLogger::stop()
{
    QMutexLocker lock(&g_mutex);
    if (!g_active)
        return;
    qInstallMessageHandler(g_previous);
    g_previous = nullptr;
    g_file.close();
    g_active = false;
}

bool FileLogger::isActive()
{
    QMutexLocker lock(&g_mutex);
    return g_active;
}
