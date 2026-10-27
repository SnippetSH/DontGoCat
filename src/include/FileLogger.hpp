#pragma once

#include <QString>
#include <QStringList>

// Qt 메시지(qDebug/qInfo/qWarning/qCritical/qFatal, qC* 포함)를 로그 파일에 남긴다.
// - start: dirs 를 앞에서부터 시도해 처음으로 쓸 수 있는 디렉터리에 Config::kLogFileName 을 새로 만든다
//   (기존 내용은 지운다 = 실행마다 초기화). 디렉터리가 없으면 만든다. 실패하면 빈 문자열.
// - 줄 형식: "yyyy-MM-dd HH:mm:ss.zzz I [ccat.brain] 메시지" (D/I/W/C/F = 메시지 종류, 카테고리 없으면 [default])
// - 매 줄 바로 flush 한다 (강제 종료 / 크래시 직전까지 남도록). 여러 스레드에서 불려도 안전.
// - 이전 메시지 핸들러(기본: 디버거 출력)에도 그대로 넘긴다.
namespace FileLogger {

QString start(const QStringList &dirs);   // 연 로그 파일 경로
void stop();                              // 핸들러 원복 + 파일 닫기 (start 전이면 아무것도 안 함)
bool isActive();

} // namespace FileLogger
