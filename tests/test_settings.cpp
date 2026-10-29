// AppSettingsFile / FileLogger 테스트: settings.txt 파싱(대소문자·공백·주석·BOM·CRLF), 파일 로드,
// 로그 파일 기록(줄 형식, 실행마다 초기화, 쓸 수 없는 디렉터리면 다음 후보로 대체).
#include "TestCheck.hpp"

#include "AppSettingsFile.hpp"
#include "Config.hpp"
#include "FileLogger.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QTemporaryDir>

#include <cstdio>

Q_LOGGING_CATEGORY(lcTest, "ccat.test")

namespace {

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(data);
    return true;
}

QString readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
}

} // namespace

static void testParse()
{
    std::printf("[parse]\n");
    CHECK(!AppSettingsFile::parse(QString()).enableLog, "empty -> false");
    CHECK(AppSettingsFile::parse("enable-log: true").enableLog, "true");
    CHECK(!AppSettingsFile::parse("enable-log: false").enableLog, "false");
    CHECK(AppSettingsFile::parse("  Enable-Log :  TRUE  ").enableLog, "case + whitespace");
    CHECK(AppSettingsFile::parse("enable-log:true").enableLog, "no space");
    CHECK(!AppSettingsFile::parse("# enable-log: true\n\n  \n# note").enableLog, "comments / empty lines ignored");
    CHECK(!AppSettingsFile::parse("enable-log: yes").enableLog, "invalid value keeps false");
    CHECK(!AppSettingsFile::parse("enable-log: 1").enableLog, "1 is not true");
    CHECK(!AppSettingsFile::parse("enable-log true").enableLog, "no colon ignored");
    CHECK(AppSettingsFile::parse("enable-log: true\nenable-log: maybe").enableLog, "invalid later value keeps previous");
    CHECK(!AppSettingsFile::parse("enable-log: true\nenable-log: false").enableLog, "last value wins (false)");
    CHECK(AppSettingsFile::parse("enable-log: false\nenable-log: true").enableLog, "last value wins (true)");
    CHECK(AppSettingsFile::parse("other: 1\nfoo\nenable-log: true\nbar: x: y").enableLog, "unknown keys ignored");
    CHECK(AppSettingsFile::parse(QString(QChar(0xFEFF)) + "enable-log: true").enableLog, "BOM");
    CHECK(AppSettingsFile::parse("# c\r\nenable-log: true\r\n").enableLog, "CRLF");
}

static void testLoad(const QString &dir)
{
    std::printf("[load]\n");
    CHECK(!AppSettingsFile::load(dir + "/missing.txt").enableLog, "missing file -> false");
    const QString path = dir + "/settings.txt";
    CHECK(writeFile(path, "\xEF\xBB\xBF# log\r\nenable-log: true\r\n"), "write settings");
    CHECK(AppSettingsFile::load(path).enableLog, "load true (BOM + CRLF)");
    CHECK(writeFile(path, "enable-log: false\n"), "rewrite settings");
    CHECK(!AppSettingsFile::load(path).enableLog, "load false");
    CHECK(AppSettingsFile::defaultPath().endsWith(Config::kSettingsFileName), "defaultPath file name");
}

static void testLogger(const QString &dir)
{
    std::printf("[logger]\n");
    CHECK(!FileLogger::isActive(), "inactive at start");
    const QString sub = dir + "/logs/sub";   // 없는 디렉터리 (만들 수 있음)
    const QString path = FileLogger::start({sub});
    CHECK(!path.isEmpty() && FileLogger::isActive(), "start");
    CHECK(path == QDir(sub).filePath(Config::kLogFileName), "path: %s", path.toStdString().c_str());
    qCInfo(lcTest) << "first line";
    qWarning("plain warning");
    FileLogger::stop();
    CHECK(!FileLogger::isActive(), "stopped");
    const QString text = readFile(path);
    CHECK(text.contains("first line"), "message written");
    CHECK(text.contains(" I [ccat.test] first line"), "line format");
    CHECK(text.contains(" W [default] plain warning"), "default category + type letter");

    // 두 번째 start 는 이전 내용을 지운다
    CHECK(!FileLogger::start({sub}).isEmpty(), "second start");
    qCInfo(lcTest) << "second run";
    FileLogger::stop();
    const QString text2 = readFile(path);
    CHECK(text2.contains("second run") && !text2.contains("first line"), "truncated on restart");

    // 첫 후보가 일반 파일이면 mkpath/open 이 실패 → 두 번째 후보
    const QString blocker = dir + "/blocker";
    CHECK(writeFile(blocker, "x"), "blocker file");
    const QString fallback = dir + "/fallback";
    const QString path2 = FileLogger::start({blocker, fallback});
    CHECK(path2 == QDir(fallback).filePath(Config::kLogFileName), "fallback dir used: %s", path2.toStdString().c_str());
    FileLogger::stop();

    CHECK(FileLogger::start({blocker}).isEmpty() && !FileLogger::isActive(), "all dirs fail -> empty");
    FileLogger::stop();   // 시작 전/후 어느 쪽이든 안전
}

// 기본 settings.txt 만들기 (macOS 첫 실행): 없는 폴더까지 만들고 enable-log 켜짐, 이미 있으면 덮어쓰지 않음
static void testWriteDefault(const QString &dir)
{
    std::printf("[default file]\n");
    const QString path = dir + QStringLiteral("/sub/settings.txt");
    CHECK(AppSettingsFile::writeDefaultIfMissing(path), "creates folder and file");
    CHECK(QFile::exists(path) && AppSettingsFile::load(path).enableLog, "default enable-log true");
    CHECK(writeFile(path, "enable-log: false\n"), "edit");
    CHECK(AppSettingsFile::writeDefaultIfMissing(path) && !AppSettingsFile::load(path).enableLog, "existing file kept");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("test_settings\n");
    QTemporaryDir dir;
    CHECK(dir.isValid(), "temp dir");
    testParse();
    testLoad(dir.path());
    testLogger(dir.path());
    testWriteDefault(dir.path());
    return testing::testResult("test_settings");
}
