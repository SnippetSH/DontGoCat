// AutoStart 레지스트리 테스트. 실제 Run 키는 절대 건드리지 않고 HKCU\Software\CCatTest\Run 임시 키만 쓴다 (끝에서 삭제).
#include "TestCheck.hpp"

#include "AutoStart.hpp"

#include <QCoreApplication>
#include <QDir>

#include <cstdio>

#include <windows.h>

static const QString kTestRoot = QStringLiteral("Software\\CCatTest");
static const QString kTestKey = QStringLiteral("Software\\CCatTest\\Run");

static void writeRaw(const QString &value)
{
    const std::wstring key = kTestKey.toStdWString();
    const std::wstring v = value.toStdWString();
    RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), AutoStart::kValueName, REG_SZ, v.c_str(),
                    static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
}

static void testEnableDisable()
{
    std::printf("[1] enable / disable\n");
    CHECK(AutoStart::defaultRunKey() != kTestKey, "test key differs from the real Run key");
    CHECK(!AutoStart::isEnabled(kTestKey), "initially disabled");
    CHECK(AutoStart::registeredCommand(kTestKey).isEmpty(), "no value initially");

    CHECK(AutoStart::setEnabled(true, kTestKey), "enable succeeds");
    CHECK(AutoStart::isEnabled(kTestKey), "enabled after enable");
    const QString expected = QLatin1Char('"') + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + QLatin1Char('"');
    const QString value = AutoStart::registeredCommand(kTestKey);
    std::printf("value: %s\n", qPrintable(value));
    CHECK(value == expected, "value is the quoted native exe path");
    CHECK(value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')), "value is quoted");
    CHECK(value.contains(QLatin1Char('\\')) && !value.contains(QLatin1Char('/')), "native separators");
    CHECK(AutoStart::setEnabled(true, kTestKey), "enable is idempotent");

    CHECK(AutoStart::setEnabled(false, kTestKey), "disable succeeds");
    CHECK(!AutoStart::isEnabled(kTestKey), "disabled after disable");
    CHECK(AutoStart::registeredCommand(kTestKey).isEmpty(), "value removed");
    CHECK(AutoStart::setEnabled(false, kTestKey), "disable when already disabled succeeds");
}

static void testRefreshPath()
{
    std::printf("[2] refreshPath\n");
    AutoStart::refreshPath(kTestKey);
    CHECK(!AutoStart::isEnabled(kTestKey), "refresh does not enable a disabled entry");

    writeRaw(QStringLiteral("\"C:\\Old\\Place\\DontGoCat.exe\""));
    CHECK(AutoStart::isEnabled(kTestKey), "stale value counts as enabled");
    AutoStart::refreshPath(kTestKey);
    CHECK(AutoStart::registeredCommand(kTestKey) == AutoStart::currentCommand(), "stale path rewritten to current exe");

    const QString before = AutoStart::registeredCommand(kTestKey);
    AutoStart::refreshPath(kTestKey);
    CHECK(AutoStart::registeredCommand(kTestKey) == before, "up-to-date path left alone");
}

static void testFailure()
{
    std::printf("[3] failure\n");
    const QString badKey = kTestRoot + QLatin1Char('\\') + QString(300, QLatin1Char('a'));   // 키 이름 255자 초과 → 실패
    CHECK(!AutoStart::setEnabled(true, badKey), "enable fails on an invalid key");
    CHECK(!AutoStart::isEnabled(badKey), "invalid key is not enabled");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // 이전 실행이 남긴 키 정리
    RegDeleteTreeW(HKEY_CURRENT_USER, kTestRoot.toStdWString().c_str());
    testEnableDisable();
    testRefreshPath();
    testFailure();
    RegDeleteTreeW(HKEY_CURRENT_USER, kTestRoot.toStdWString().c_str());
    CHECK(RegGetValueW(HKEY_CURRENT_USER, kTestKey.toStdWString().c_str(), nullptr, RRF_RT_ANY, nullptr, nullptr, nullptr) != ERROR_SUCCESS,
          "test key cleaned up");
    return testing::testResult("test_autostart");
}
