// CatStats 테스트 (README 5.14): 누적/세션 합산, QSettings 저장·복원(임시 INI 파일, 실제 레지스트리 설정은 건드리지 않음),
// since 날짜, 표시 형식.
#include "TestCheck.hpp"

#include "CatStats.hpp"
#include "Config.hpp"

#include <QCoreApplication>
#include <QDate>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>

static void testAdd()
{
    std::printf("[add]\n");
    CatStats st;
    st.addRunMs(1000);
    st.addRoamMs(200);
    st.addSleepMs(300);
    st.addPlayMs(400);
    st.addNudge();
    st.addNudge();
    st.addQuitAttempt(true);
    st.addQuitAttempt(false);
    st.addQuitAttempt(false);
    st.addGrab();
    st.addJump();
    st.addJump();
    st.addFall();
    st.addDistanceM(1.5);
    st.addDistanceM(0.5);
    for (const CatStatsValues *v : {&st.total(), &st.session()}) {
        CHECK(v->runMs == 1000 && v->roamMs == 200 && v->sleepMs == 300 && v->playMs == 400, "times");
        CHECK(v->nudges == 2 && v->grabs == 1 && v->jumps == 2 && v->falls == 1, "counts");
        CHECK(v->quitAttempts == 3 && v->quitBlocks == 1, "quit %d/%d", v->quitBlocks, v->quitAttempts);
        CHECK(v->distanceM == 2.0, "distance");
    }
}

static void testPersist(const QString &dir)
{
    std::printf("[persist]\n");
    const QString path = dir + "/a.ini";
    {
        CatStats st;
        QSettings s(path, QSettings::IniFormat);
        st.load(s);
        CHECK(st.since() == QDate::currentDate(), "since defaults to today");
        CHECK(st.total().runMs == 0 && st.total().distanceM == 0.0, "empty load = 0");
        st.addRunMs(123456789012LL);
        st.addRoamMs(5);
        st.addSleepMs(6);
        st.addPlayMs(7);
        st.addNudge();
        st.addQuitAttempt(true);
        st.addGrab();
        st.addJump();
        st.addFall();
        st.addDistanceM(12.25);
        st.save(s);
        s.sync();
    }
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(Config::kSettingsStatsGroup);
        CHECK(s.value("since").toString() == QDate::currentDate().toString("yyyy-MM-dd"), "since ISO saved");
        s.endGroup();
        CatStats st;
        st.load(s);
        const CatStatsValues &t = st.total();
        CHECK(t.runMs == 123456789012LL && t.roamMs == 5 && t.sleepMs == 6 && t.playMs == 7, "times restored");
        CHECK(t.nudges == 1 && t.quitAttempts == 1 && t.quitBlocks == 1 && t.grabs == 1 && t.jumps == 1 && t.falls == 1, "counts restored");
        CHECK(t.distanceM == 12.25, "distance restored");
        const CatStatsValues &ss = st.session();
        CHECK(ss.runMs == 0 && ss.nudges == 0 && ss.distanceM == 0.0 && ss.quitAttempts == 0, "session starts 0");
        st.addNudge();
        CHECK(st.total().nudges == 2 && st.session().nudges == 1, "accumulates on top");
    }
}

static void testSince(const QString &dir)
{
    std::printf("[since / corrupt]\n");
    const QString path = dir + "/b.ini";
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(Config::kSettingsStatsGroup);
        s.setValue("since", "2020-02-29");
        s.setValue("runMs", -5);
        s.setValue("roamMs", "abc");
        s.setValue("sleepMs", 42);
        s.setValue("jumps", -1);
        s.setValue("falls", "x");
        s.setValue("distanceM", -3.5);
        s.setValue("playMs", "");
        s.endGroup();
    }
    QSettings s(path, QSettings::IniFormat);
    CatStats st;
    st.load(s);
    CHECK(st.since() == QDate(2020, 2, 29), "since kept");
    const CatStatsValues &t = st.total();
    CHECK(t.runMs == 0 && t.roamMs == 0 && t.playMs == 0, "negative/non-numeric/empty -> 0");
    CHECK(t.sleepMs == 42, "valid kept");
    CHECK(t.jumps == 0 && t.falls == 0 && t.distanceM == 0.0, "bad counts/distance -> 0");
    CHECK(t.nudges == 0 && t.grabs == 0, "missing -> 0");

    s.beginGroup(Config::kSettingsStatsGroup);
    s.setValue("since", "not-a-date");
    s.endGroup();
    CatStats st2;
    st2.load(s);
    CHECK(st2.since() == QDate::currentDate(), "invalid since -> today");
    st2.save(s);
    s.beginGroup(Config::kSettingsStatsGroup);
    CHECK(s.value("since").toString() == QDate::currentDate().toString(Qt::ISODate), "fixed since saved");
    s.endGroup();
}

static void testFormat()
{
    std::printf("[format]\n");
    const auto dur = [](qint64 ms) { return CatStats::formatDuration(ms).toStdString(); };
    CHECK(CatStats::formatDuration(0) == QString::fromUtf8("0초"), "0: %s", dur(0).c_str());
    CHECK(CatStats::formatDuration(999) == QString::fromUtf8("0초"), "999ms floors to 0초");
    CHECK(CatStats::formatDuration(59999) == QString::fromUtf8("59초"), "59.999s: %s", dur(59999).c_str());
    CHECK(CatStats::formatDuration(60000) == QString::fromUtf8("1분 0초"), "1min: %s", dur(60000).c_str());
    CHECK(CatStats::formatDuration(3599000) == QString::fromUtf8("59분 59초"), "59m59s: %s", dur(3599000).c_str());
    CHECK(CatStats::formatDuration(3600000) == QString::fromUtf8("1시간 0분"), "1h: %s", dur(3600000).c_str());
    CHECK(CatStats::formatDuration(3600000 + 59 * 1000) == QString::fromUtf8("1시간 0분"), "1h59s floors");
    CHECK(CatStats::formatDuration(86399000) == QString::fromUtf8("23시간 59분"), "23h59m: %s", dur(86399000).c_str());
    CHECK(CatStats::formatDuration(86400000) == QString::fromUtf8("1일 0시간 0분"), "1d: %s", dur(86400000).c_str());
    CHECK(CatStats::formatDuration(86400000LL * 2 + 3600000 * 5 + 60000 * 7) == QString::fromUtf8("2일 5시간 7분"), "2d5h7m");

    CHECK(CatStats::formatDistance(0.0) == "0 cm", "0 cm");
    CHECK(CatStats::formatDistance(0.99) == "99 cm", "0.99m = 99 cm");
    CHECK(CatStats::formatDistance(0.126) == "13 cm", "rounds cm");
    CHECK(CatStats::formatDistance(1.0) == "1.0 m", "1m");
    CHECK(CatStats::formatDistance(12.34) == "12.3 m", "12.3 m");
    CHECK(CatStats::formatDistance(999.0) == "999.0 m", "999m");
    CHECK(CatStats::formatDistance(1000.0) == "1.00 km", "1km");
    CHECK(CatStats::formatDistance(0.996) == "1.0 m", "0.996m rounds up to 1.0 m (not 100 cm)");
    CHECK(CatStats::formatDistance(0.994) == "99 cm", "0.994m = 99 cm");
    CHECK(CatStats::formatDistance(999.94) == "999.9 m", "999.94m = 999.9 m");
    CHECK(CatStats::formatDistance(999.96) == "1.00 km", "999.96m rounds up to 1.00 km (not 1000.0 m)");
    CHECK(CatStats::formatDistance(12345.0) == "12.35 km" || CatStats::formatDistance(12345.0) == "12.34 km", "12.34x km");

    CHECK(CatStats::formatCount(0) == QString::fromUtf8("0회"), "0회");
    CHECK(CatStats::formatCount(999) == QString::fromUtf8("999회"), "999회");
    CHECK(CatStats::formatCount(1000) == QString::fromUtf8("1,000회"), "1,000회");
    CHECK(CatStats::formatCount(12345) == QString::fromUtf8("12,345회"), "12,345회");
    CHECK(CatStats::formatCount(1234567) == QString::fromUtf8("1,234,567회"), "millions");

    CHECK(CatStats::formatRate(0, 0) == "-", "0/0");
    CHECK(CatStats::formatRate(5, 0) == "-", "x/0");
    CHECK(CatStats::formatRate(0, 4) == "0%", "0%");
    CHECK(CatStats::formatRate(12, 15) == "80%", "80%");
    CHECK(CatStats::formatRate(1, 3) == "33%", "33%");
    CHECK(CatStats::formatRate(2, 3) == "67%", "67% rounds");
    CHECK(CatStats::formatRate(3, 3) == "100%", "100%");
}

// 누적은 줄지 않는다: 저장값이 더 크면 받아들이고(ahead), 바깥에서 되돌려졌으면 감지해 메모리 값으로 다시 쓴다(behind)
static void testMerge(const QString &dir)
{
    std::printf("[merge]\n");
    const QString path = dir + "/c.ini";
    const auto stored = [&path](const char *key) {
        QSettings s(path, QSettings::IniFormat);
        return s.value(QStringLiteral("%1/%2").arg(Config::kSettingsStatsGroup, key));
    };
    const auto put = [&path](const char *key, const QVariant &v) {
        QSettings s(path, QSettings::IniFormat);
        s.setValue(QStringLiteral("%1/%2").arg(Config::kSettingsStatsGroup, key), v);
    };

    put("runMs", 1000);
    put("nudges", 3);
    put("since", "2026-10-11");
    QSettings s(path, QSettings::IniFormat);
    CatStats st;
    st.load(s);
    st.addRunMs(500);
    st.addNudge();
    CatStatsSaveResult r = st.save(s);
    s.sync();
    CHECK(r.ahead.isEmpty() && r.behind.isEmpty(), "normal save reports nothing");
    CHECK(stored("runMs").toLongLong() == 1500 && stored("nudges").toInt() == 4, "normal save writes memory");

    // 바깥에서 되돌림 (이번 사건): 저장값이 마지막으로 쓴 값보다 작다 → behind, 메모리 값으로 다시 쓴다
    put("runMs", 200);
    put("nudges", 1);
    s.sync();
    st.addRunMs(10);
    r = st.save(s);
    s.sync();
    CHECK(r.behind.contains("runMs") && r.behind.contains("nudges") && r.ahead.isEmpty(), "rollback detected: %s",
          r.behind.join(",").toStdString().c_str());
    CHECK(r.lastRunMs == 1500 && r.storedRunMs == 200, "runMs last %lld stored %lld", r.lastRunMs, r.storedRunMs);
    CHECK(stored("runMs").toLongLong() == 1510 && stored("nudges").toInt() == 4, "rewritten from memory");

    // 저장값이 더 크다 (다른 무언가가 키움): 받아들여 누적에 반영, 줄여 쓰지 않는다
    put("runMs", 99999);
    put("grabs", 7);
    put("since", "2026-01-01");
    s.sync();
    r = st.save(s);
    s.sync();
    CHECK(r.ahead.contains("runMs") && r.ahead.contains("grabs") && r.ahead.contains("since"), "ahead: %s",
          r.ahead.join(",").toStdString().c_str());
    CHECK(st.total().runMs == 99999 && st.total().grabs == 7 && st.since() == QDate(2026, 1, 1), "memory adopts larger");
    CHECK(st.session().runMs == 510 && st.session().grabs == 0, "session untouched");
    CHECK(stored("runMs").toLongLong() == 99999 && stored("since").toString() == "2026-01-01", "larger kept in store");
    st.addRunMs(1);
    r = st.save(s);
    CHECK(r.ahead.isEmpty() && r.behind.isEmpty() && st.total().runMs == 100000, "continues from adopted value");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("test_stats\n");
    QTemporaryDir dir;
    CHECK(dir.isValid(), "temp dir");
    testAdd();
    testPersist(dir.path());
    testSince(dir.path());
    testMerge(dir.path());
    testFormat();
    return testing::testResult("test_stats");
}
