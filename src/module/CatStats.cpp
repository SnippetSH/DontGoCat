#include "CatStats.hpp"

#include "Config.hpp"

#include <QLocale>
#include <QSettings>

#include <algorithm>
#include <climits>
#include <cmath>

// QSettings 키 (그룹 Config::kSettingsStatsGroup 안, README 5.14):
//   since(QDate ISO "yyyy-MM-dd"), runMs, roamMs, sleepMs, playMs (qint64),
//   nudges, quitAttempts, quitBlocks, grabs, jumps, falls (int), distanceM (double)

namespace {

// 키가 없거나 숫자가 아니거나 음수면 0
qint64 readCount(const QSettings &s, const char *key)
{
    bool ok = false;
    const qint64 v = s.value(QLatin1String(key)).toLongLong(&ok);
    return (ok && v > 0) ? v : 0;
}

double readMeters(const QSettings &s, const char *key)
{
    bool ok = false;
    const double v = s.value(QLatin1String(key)).toDouble(&ok);
    return (ok && v > 0.0 && std::isfinite(v)) ? v : 0.0;
}

}   // namespace

void CatStats::load(QSettings &settings)
{
    settings.beginGroup(QLatin1String(Config::kSettingsStatsGroup));
    CatStatsValues t;
    t.runMs = readCount(settings, "runMs");
    t.roamMs = readCount(settings, "roamMs");
    t.sleepMs = readCount(settings, "sleepMs");
    t.playMs = readCount(settings, "playMs");
    t.nudges = int(std::min<qint64>(readCount(settings, "nudges"), INT_MAX));
    t.quitAttempts = int(std::min<qint64>(readCount(settings, "quitAttempts"), INT_MAX));
    t.quitBlocks = int(std::min<qint64>(readCount(settings, "quitBlocks"), INT_MAX));
    t.grabs = int(std::min<qint64>(readCount(settings, "grabs"), INT_MAX));
    t.jumps = int(std::min<qint64>(readCount(settings, "jumps"), INT_MAX));
    t.falls = int(std::min<qint64>(readCount(settings, "falls"), INT_MAX));
    t.distanceM = readMeters(settings, "distanceM");
    const QDate since = QDate::fromString(settings.value(QLatin1String("since")).toString(), Qt::ISODate);
    settings.endGroup();

    m_total = t;
    m_session = CatStatsValues{};
    m_since = since.isValid() ? since : QDate::currentDate();   // 없거나 틀리면 오늘 (다음 save 에 기록)
}

void CatStats::save(QSettings &settings) const
{
    settings.beginGroup(QLatin1String(Config::kSettingsStatsGroup));
    settings.setValue(QLatin1String("since"), (m_since.isValid() ? m_since : QDate::currentDate()).toString(Qt::ISODate));
    settings.setValue(QLatin1String("runMs"), m_total.runMs);
    settings.setValue(QLatin1String("roamMs"), m_total.roamMs);
    settings.setValue(QLatin1String("sleepMs"), m_total.sleepMs);
    settings.setValue(QLatin1String("playMs"), m_total.playMs);
    settings.setValue(QLatin1String("nudges"), m_total.nudges);
    settings.setValue(QLatin1String("quitAttempts"), m_total.quitAttempts);
    settings.setValue(QLatin1String("quitBlocks"), m_total.quitBlocks);
    settings.setValue(QLatin1String("grabs"), m_total.grabs);
    settings.setValue(QLatin1String("jumps"), m_total.jumps);
    settings.setValue(QLatin1String("falls"), m_total.falls);
    settings.setValue(QLatin1String("distanceM"), m_total.distanceM);
    settings.endGroup();
}

void CatStats::addRunMs(qint64 ms) { m_total.runMs += ms; m_session.runMs += ms; }
void CatStats::addRoamMs(qint64 ms) { m_total.roamMs += ms; m_session.roamMs += ms; }
void CatStats::addSleepMs(qint64 ms) { m_total.sleepMs += ms; m_session.sleepMs += ms; }
void CatStats::addPlayMs(qint64 ms) { m_total.playMs += ms; m_session.playMs += ms; }
void CatStats::addNudge() { ++m_total.nudges; ++m_session.nudges; }

void CatStats::addQuitAttempt(bool blocked)
{
    ++m_total.quitAttempts;
    ++m_session.quitAttempts;
    if (blocked) {
        ++m_total.quitBlocks;
        ++m_session.quitBlocks;
    }
}

void CatStats::addGrab() { ++m_total.grabs; ++m_session.grabs; }
void CatStats::addJump() { ++m_total.jumps; ++m_session.jumps; }
void CatStats::addFall() { ++m_total.falls; ++m_session.falls; }
void CatStats::addDistanceM(double meters) { m_total.distanceM += meters; m_session.distanceM += meters; }

QString CatStats::formatDuration(qint64 ms)
{
    const qint64 totalSec = std::max<qint64>(ms, 0) / 1000;   // 초 단위 버림
    const qint64 sec = totalSec % 60;
    const qint64 totalMin = totalSec / 60;
    const qint64 min = totalMin % 60;
    const qint64 totalHour = totalMin / 60;
    const qint64 hour = totalHour % 24;
    const qint64 day = totalHour / 24;
    if (totalSec < 60)
        return QStringLiteral("%1초").arg(totalSec);
    if (totalMin < 60)
        return QStringLiteral("%1분 %2초").arg(min).arg(sec);
    if (totalHour < 24)
        return QStringLiteral("%1시간 %2분").arg(hour).arg(min);
    return QStringLiteral("%1일 %2시간 %3분").arg(day).arg(hour).arg(min);
}

QString CatStats::formatDistance(double meters)
{
    if (!(meters > 0.0))
        meters = 0.0;
    // 단위 경계는 반올림한 표시값 기준 (0.996m → "1.0 m", 999.96m → "1.00 km")
    const int cm = meters < 1.0 ? qRound(meters * 100.0) : 100;   // 큰 값에서 int 오버플로 방지
    if (cm < 100)
        return QStringLiteral("%1 cm").arg(cm);
    const double m1 = qRound64(meters * 10.0) / 10.0;
    if (m1 < 1000.0)
        return QStringLiteral("%1 m").arg(m1, 0, 'f', 1);
    return QStringLiteral("%1 km").arg(meters / 1000.0, 0, 'f', 2);
}

QString CatStats::formatCount(int n)
{
    QLocale loc = QLocale::c();
    loc.setNumberOptions({});   // C 로케일 기본은 구분자 생략 → 천 단위 쉼표 "12,345" 를 켠다
    return loc.toString(n) + QStringLiteral("회");
}

QString CatStats::formatRate(int part, int whole)
{
    if (whole == 0)
        return QStringLiteral("-");
    return QStringLiteral("%1%").arg(qRound(100.0 * part / whole));
}
