// CatVoice 울음 판정 테스트. 재생은 가짜 Player 로 바꿔 실제 소리를 내지 않고, meowTick() 을 직접 호출한다.
//   - 다음 판정 간격 ∈ [kMeowMinMs, kMeowMaxMs]
//   - 판정 중 울음 비율 ≈ kMeowChance %, 소리 번호 ∈ [0, kMeowSoundCount) 이고 모두 한 번 이상 나온다
//   - 꺼져 있거나 canMeow() 가 false 면 울지 않지만 다음 간격은 계속 잡는다
//   - scaledWav: 16-bit PCM data 청크만 percent 비율로 줄이고 헤더 / 다른 청크는 그대로
#include "TestCheck.hpp"

#include "CatVoice.hpp"
#include "Config.hpp"

#include <QCoreApplication>
#include <QRandomGenerator>
#include <QtEndian>

#include <cstdio>
#include <vector>

static void testChanceAndInterval()
{
    std::printf("[1] chance / interval\n");
    QRandomGenerator rng(1234);
    std::vector<int> counts(Config::kMeowSoundCount, 0);
    int played = 0;
    int bad = 0;
    CatVoice voice(nullptr, [&](int index) {
        if (index < 0 || index >= Config::kMeowSoundCount)
            ++bad;
        else
            ++counts[index];
        ++played;
    });
    voice.setRandomGenerator(&rng);

    const int n = 10000;
    int minMs = 1 << 30, maxMs = 0;
    for (int i = 0; i < n; ++i) {
        voice.meowTick();
        minMs = qMin(minMs, voice.nextIntervalMs());
        maxMs = qMax(maxMs, voice.nextIntervalMs());
    }
    const double ratio = 100.0 * played / n;
    std::printf("played %d / %d (%.1f%%), interval %d..%d ms\n", played, n, ratio, minMs, maxMs);
    CHECK(bad == 0, "sound index out of range (%d)", bad);
    CHECK(ratio > Config::kMeowChance - 3 && ratio < Config::kMeowChance + 3, "ratio %.1f%%", ratio);
    CHECK(minMs >= Config::kMeowMinMs, "min interval %d", minMs);
    CHECK(maxMs <= Config::kMeowMaxMs, "max interval %d", maxMs);
    for (int i = 0; i < Config::kMeowSoundCount; ++i)
        CHECK(counts[i] > 0, "sound %d played (%d)", i, counts[i]);
}

static void testSilenced()
{
    std::printf("[2] disabled / gate\n");
    QRandomGenerator rng(99);
    int played = 0;
    CatVoice voice(nullptr, [&](int) { ++played; });
    voice.setRandomGenerator(&rng);

    voice.setEnabled(false);
    for (int i = 0; i < 200; ++i)
        voice.meowTick();
    CHECK(played == 0, "disabled: played %d", played);
    CHECK(voice.nextIntervalMs() >= Config::kMeowMinMs, "disabled: still rescheduled");

    bool canMeow = false;   // 자는 중 / 숨은 중
    voice.setEnabled(true);
    voice.setCanMeow([&]() { return canMeow; });
    for (int i = 0; i < 200; ++i)
        voice.meowTick();
    CHECK(played == 0, "gate closed: played %d", played);

    canMeow = true;
    for (int i = 0; i < 200; ++i)
        voice.meowTick();
    CHECK(played > 0, "gate open: played %d", played);
}

// 16-bit PCM wav 를 직접 만들어 scaledWav 가 data 청크만 비율대로 줄이는지 확인
static void testVolume()
{
    std::printf("[3] volume\n");
    const qint16 samples[] = {1000, -1000, 32767, -32768};
    QByteArray fmt(16, '\0');
    qToLittleEndian<quint16>(1, fmt.data());           // PCM
    qToLittleEndian<quint16>(1, fmt.data() + 2);       // mono
    qToLittleEndian<quint32>(44100, fmt.data() + 4);
    qToLittleEndian<quint32>(88200, fmt.data() + 8);
    qToLittleEndian<quint16>(2, fmt.data() + 12);
    qToLittleEndian<quint16>(16, fmt.data() + 14);
    QByteArray data(sizeof(samples), '\0');
    for (int i = 0; i < 4; ++i)
        qToLittleEndian<qint16>(samples[i], data.data() + 2 * i);
    auto chunk = [](const char *id, const QByteArray &body) {
        QByteArray c(id, 4);
        QByteArray len(4, '\0');
        qToLittleEndian<quint32>(quint32(body.size()), len.data());
        return c + len + body;
    };
    const QByteArray riffBody = QByteArray("WAVE") + chunk("fmt ", fmt) + chunk("LIST", QByteArray("abc\0", 4)) + chunk("data", data);
    const QByteArray wav = chunk("RIFF", riffBody);
    const qsizetype dataAt = wav.size() - data.size();

    auto sampleAt = [&](const QByteArray &w, int i) { return qFromLittleEndian<qint16>(w.constData() + dataAt + 2 * i); };
    const QByteArray half = CatVoice::scaledWav(wav, 50);
    CHECK(half.size() == wav.size(), "size unchanged");
    CHECK(half.left(dataAt) == wav.left(dataAt), "header untouched");
    CHECK(sampleAt(half, 0) == 500 && sampleAt(half, 1) == -500, "50%% (%d, %d)", sampleAt(half, 0), sampleAt(half, 1));
    CHECK(sampleAt(half, 2) == 16383 && sampleAt(half, 3) == -16384, "50%% extremes (%d, %d)", sampleAt(half, 2),
          sampleAt(half, 3));
    CHECK(CatVoice::scaledWav(wav, 100) == wav, "100%% = original");
    const QByteArray mute = CatVoice::scaledWav(wav, 0);
    CHECK(sampleAt(mute, 0) == 0 && sampleAt(mute, 3) == 0, "0%% = silence");
    CHECK(CatVoice::scaledWav(wav, 150) == wav, "over 100%% clamped to original");
    CHECK(CatVoice::scaledWav(QByteArray("junk"), 50) == QByteArray("junk"), "non-wav untouched");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testChanceAndInterval();
    testSilenced();
    testVolume();
    return testing::testResult("test_voice");
}
