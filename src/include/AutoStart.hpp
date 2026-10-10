#pragma once

#include <QString>

// 윈도우 시작 시 자동 실행 (README 5.11).
// HKCU\Software\Microsoft\Windows\CurrentVersion\Run 에 값 "DontGoCat" = "<DontGoCat.exe 전체 경로>" 를 넣고 뺀다.
// 모든 함수의 runKey 는 HKCU 아래 하위 키 경로이며, 비우면 실제 Run 키를 쓴다 (테스트는 별도 키를 넘긴다).
namespace AutoStart {

inline constexpr const wchar_t *kValueName = L"DontGoCat";

QString defaultRunKey();                                    // Software\Microsoft\Windows\CurrentVersion\Run

bool isEnabled(const QString &runKey = QString());                 // 값이 있으면 true (경로 일치 여부는 보지 않는다)
bool setEnabled(bool enabled, const QString &runKey = QString());  // 성공 시 true. 끄기는 값이 이미 없어도 성공
void refreshPath(const QString &runKey = QString());               // 값이 있는데 현재 exe 경로와 다르면 현재 경로로 갱신
QString registeredCommand(const QString &runKey = QString());      // 등록된 값 데이터 (없으면 빈 문자열)
QString currentCommand();                                   // 현재 exe 의 따옴표 친 네이티브 절대 경로

} // namespace AutoStart
