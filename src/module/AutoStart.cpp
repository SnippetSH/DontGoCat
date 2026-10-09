#include "AutoStart.hpp"

#include <QCoreApplication>
#include <QDir>

#include <vector>

#include <windows.h>

namespace {

std::wstring keyOf(const QString &runKey)
{
    return (runKey.isEmpty() ? AutoStart::defaultRunKey() : runKey).toStdWString();
}

} // namespace

namespace AutoStart {

QString defaultRunKey()
{
    return QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Run");
}

QString currentCommand()
{
    const QString path = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    return QLatin1Char('"') + path + QLatin1Char('"');
}

QString registeredCommand(const QString &runKey)
{
    const std::wstring key = keyOf(runKey);
    DWORD bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), kValueName, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS
        || bytes < sizeof(wchar_t))
        return {};
    std::vector<wchar_t> buf(bytes / sizeof(wchar_t) + 1, L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), kValueName, RRF_RT_REG_SZ, nullptr, buf.data(), &bytes) != ERROR_SUCCESS)
        return {};
    return QString::fromWCharArray(buf.data());
}

bool isEnabled(const QString &runKey)
{
    const std::wstring key = keyOf(runKey);
    return RegGetValueW(HKEY_CURRENT_USER, key.c_str(), kValueName, RRF_RT_ANY, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

bool setEnabled(bool enabled, const QString &runKey)
{
    const std::wstring key = keyOf(runKey);
    if (enabled) {
        const std::wstring cmd = currentCommand().toStdWString();
        return RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), kValueName, REG_SZ, cmd.c_str(),
                               static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    }
    const LONG r = RegDeleteKeyValueW(HKEY_CURRENT_USER, key.c_str(), kValueName);
    return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND || r == ERROR_PATH_NOT_FOUND;
}

void refreshPath(const QString &runKey)
{
    if (!isEnabled(runKey))
        return;
    // 대소문자만 다른 경로는 같은 경로로 본다
    if (registeredCommand(runKey).compare(currentCommand(), Qt::CaseInsensitive) != 0)
        setEnabled(true, runKey);
}

} // namespace AutoStart
