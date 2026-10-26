#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QVersionNumber>

#include <optional>

// 새 릴리스 확인 (README 5.15). 플랫폼 공통: Qt Network 로 GitHub 최신 릴리스만 조회한다.
// 판정은 순수 함수로 분리해 테스트한다 (네트워크 없이).
namespace UpdateCheck {

// "1.3.0" / "v1.3.0" → 버전. 접미사가 붙은 태그("1.4.0-beta")나 숫자가 아닌 태그는 nullopt
std::optional<QVersionNumber> parseVersion(const QString &tag);

// GitHub `releases/latest` 응답 JSON → tag_name 의 버전. 초안/프리릴리스, 손상된 JSON 은 nullopt
std::optional<QVersionNumber> parseLatestRelease(const QByteArray &json);

// latest 가 current 보다 새롭고, 건너뛴 버전(skipped)보다도 새로울 때만 알린다
bool shouldNotify(const QVersionNumber &latest, const QVersionNumber &current,
                  const std::optional<QVersionNumber> &skipped);

} // namespace UpdateCheck

// 시작 후 Config::kUpdateCheckDelayMs 뒤 한 번 조회한다. 실패(오프라인, 시간 초과, 응답 이상)는 조용히 무시.
// 건너뛴 버전은 QSettings(Config::kSettingsUpdateSkippedKey) 에서 읽는다.
class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(QObject *parent = nullptr);

    void start(const QString &currentVersion);   // 지연 후 1회 조회 예약

    static void skipVersion(const QString &version);   // "이 버전 건너뛰기" 저장

signals:
    void updateAvailable(const QString &version);   // 정규화된 버전 문자열 ("1.4.0")

private:
    void check();

    QNetworkAccessManager m_network;
    QVersionNumber m_current;
};
