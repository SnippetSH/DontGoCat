#include "UpdateChecker.hpp"

#include "Config.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUrl>

Q_LOGGING_CATEGORY(lcUpdate, "ccat.update")

namespace UpdateCheck {

std::optional<QVersionNumber> parseVersion(const QString &tag)
{
    QString s = tag.trimmed();
    if (s.startsWith(u'v') || s.startsWith(u'V'))
        s.remove(0, 1);
    qsizetype suffix = 0;
    const QVersionNumber v = QVersionNumber::fromString(s, &suffix);
    if (v.isNull() || suffix != s.size())
        return std::nullopt;
    return v;
}

std::optional<QVersionNumber> parseLatestRelease(const QByteArray &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return std::nullopt;
    const QJsonObject release = doc.object();
    // releases/latest 는 원래 초안/프리릴리스를 주지 않지만, 응답만 믿지 않고 한 번 더 거른다
    if (release.value(QStringLiteral("draft")).toBool() || release.value(QStringLiteral("prerelease")).toBool())
        return std::nullopt;
    return parseVersion(release.value(QStringLiteral("tag_name")).toString());
}

bool shouldNotify(const QVersionNumber &latest, const QVersionNumber &current,
                  const std::optional<QVersionNumber> &skipped)
{
    // 1.4 와 1.4.0 을 같게 본다
    const QVersionNumber l = latest.normalized();
    if (l <= current.normalized())
        return false;
    return !skipped || l > skipped->normalized();
}

} // namespace UpdateCheck

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent) {}

void UpdateChecker::start(const QString &currentVersion)
{
    const std::optional<QVersionNumber> current = UpdateCheck::parseVersion(currentVersion);
    if (!current) {
        qCWarning(lcUpdate) << "invalid current version" << currentVersion;
        return;
    }
    m_current = *current;
    QTimer::singleShot(Config::kUpdateCheckDelayMs, this, &UpdateChecker::check);
}

void UpdateChecker::skipVersion(const QString &version)
{
    QSettings().setValue(Config::kSettingsUpdateSkippedKey, version);
}

void UpdateChecker::check()
{
    QNetworkRequest request(QUrl(QString::fromLatin1(Config::kUpdateApiUrl)));
    // GitHub API 는 User-Agent 가 없으면 거절한다
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DontGoCat/%1").arg(m_current.toString()));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(Config::kUpdateTimeoutMs);

    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qCInfo(lcUpdate) << "check failed:" << reply->errorString();
            return;
        }
        const std::optional<QVersionNumber> latest = UpdateCheck::parseLatestRelease(reply->readAll());
        if (!latest) {
            qCInfo(lcUpdate) << "no usable release in response";
            return;
        }
        const std::optional<QVersionNumber> skipped =
            UpdateCheck::parseVersion(QSettings().value(Config::kSettingsUpdateSkippedKey).toString());
        qCInfo(lcUpdate) << "latest" << latest->toString() << "current" << m_current.toString();
        if (UpdateCheck::shouldNotify(*latest, m_current, skipped))
            emit updateAvailable(latest->toString());
    });
}
