// UpdateCheck 순수 함수 테스트 (README 5.15). 네트워크 / QSettings 는 쓰지 않는다.
//   - parseVersion: "1.3.0" / "v1.3.0" / 공백, 접미사·빈 문자열·숫자 아님은 거절
//   - parseLatestRelease: tag_name 추출, 초안/프리릴리스·tag_name 없음·손상된 JSON 은 거절
//   - shouldNotify: 새 버전만, 같거나 낮으면 아님, 1.4 == 1.4.0, 건너뛴 버전 이하면 아님 / 더 새 버전이면 다시 알림
#include "TestCheck.hpp"

#include "UpdateChecker.hpp"

#include <cstdio>

using UpdateCheck::parseLatestRelease;
using UpdateCheck::parseVersion;
using UpdateCheck::shouldNotify;

static void testParseVersion()
{
    std::printf("[1] parseVersion\n");
    CHECK(parseVersion("1.3.0") == QVersionNumber(1, 3, 0), "plain");
    CHECK(parseVersion("v1.3.0") == QVersionNumber(1, 3, 0), "v prefix");
    CHECK(parseVersion("V2.0") == QVersionNumber(2, 0), "V prefix");
    CHECK(parseVersion(" 1.10.2 \n") == QVersionNumber(1, 10, 2), "trimmed");
    CHECK(!parseVersion("1.4.0-beta"), "suffix rejected");
    CHECK(!parseVersion(""), "empty rejected");
    CHECK(!parseVersion("v"), "only v rejected");
    CHECK(!parseVersion("release"), "word rejected");
}

static void testParseLatestRelease()
{
    std::printf("[2] parseLatestRelease\n");
    CHECK(parseLatestRelease(R"({"tag_name":"1.4.0","draft":false,"prerelease":false})") == QVersionNumber(1, 4, 0),
          "tag");
    CHECK(parseLatestRelease(R"({"tag_name":"v1.4.0"})") == QVersionNumber(1, 4, 0), "no flags");
    CHECK(!parseLatestRelease(R"({"tag_name":"1.4.0","draft":true})"), "draft rejected");
    CHECK(!parseLatestRelease(R"({"tag_name":"1.4.0","prerelease":true})"), "prerelease rejected");
    CHECK(!parseLatestRelease(R"({"message":"Not Found"})"), "no tag (404 body)");
    CHECK(!parseLatestRelease(R"([1,2,3])"), "array rejected");
    CHECK(!parseLatestRelease("not json"), "broken json");
    CHECK(!parseLatestRelease(""), "empty");
}

static void testShouldNotify()
{
    std::printf("[3] shouldNotify\n");
    const QVersionNumber current(1, 3, 0);
    CHECK(shouldNotify(QVersionNumber(1, 4, 0), current, std::nullopt), "newer minor");
    CHECK(shouldNotify(QVersionNumber(1, 3, 1), current, std::nullopt), "newer patch");
    CHECK(shouldNotify(QVersionNumber(2, 0, 0), current, std::nullopt), "newer major");
    CHECK(shouldNotify(QVersionNumber(1, 10, 0), QVersionNumber(1, 9, 0), std::nullopt), "1.10 > 1.9 (not string)");
    CHECK(!shouldNotify(current, current, std::nullopt), "same");
    CHECK(!shouldNotify(QVersionNumber(1, 3), current, std::nullopt), "1.3 == 1.3.0");
    CHECK(!shouldNotify(QVersionNumber(1, 2, 9), current, std::nullopt), "older");

    // 건너뛰기
    CHECK(!shouldNotify(QVersionNumber(1, 4, 0), current, QVersionNumber(1, 4, 0)), "skipped same");
    CHECK(!shouldNotify(QVersionNumber(1, 4, 0), current, QVersionNumber(1, 4)), "skipped 1.4 == 1.4.0");
    CHECK(!shouldNotify(QVersionNumber(1, 4, 0), current, QVersionNumber(1, 5, 0)), "skipped newer");
    CHECK(shouldNotify(QVersionNumber(1, 4, 1), current, QVersionNumber(1, 4, 0)), "newer than skipped");
}

int main()
{
    testParseVersion();
    testParseLatestRelease();
    testShouldNotify();
    return testing::testResult("test_update");
}
