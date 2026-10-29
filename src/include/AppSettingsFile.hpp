#pragma once

#include <QString>

// exe 옆 settings.txt (Config::kSettingsFileName). portable 은 사용자가 직접 고치고,
// 설치판은 설치 마법사 체크박스가 같은 파일의 값을 써 넣는다 → 읽는 코드는 이것 하나.
// macOS: ~/Library/Application Support/DontGoCat/settings.txt (번들 안 파일을 고치면 서명이 깨진다). 없으면 기본 파일(enable-log: true)을 만든다
//
// 형식: 한 줄에 "key: value". 앞뒤 공백 무시, 키/값 대소문자 무시, '#' 으로 시작하는 줄과 빈 줄은 무시.
// 모르는 키는 무시하고, 같은 키가 여러 번 나오면 마지막 값을 쓴다.
//   enable-log: true | false   (그 밖의 값이면 기본값 유지)
struct AppFileSettings
{
    bool enableLog = false;   // 파일이 없거나 값이 틀리면 꺼짐
};

namespace AppSettingsFile {

AppFileSettings parse(const QString &text);
AppFileSettings load(const QString &path);   // 없거나 못 읽으면 기본값 (UTF-8, BOM 허용)
QString defaultPath();                       // QCoreApplication::applicationDirPath() + "/" + kSettingsFileName (macOS 는 위 경로)
bool writeDefaultIfMissing(const QString &path);   // 파일이 없으면 폴더와 기본 내용(enable-log: true)을 만든다. 있으면 그대로

} // namespace AppSettingsFile
