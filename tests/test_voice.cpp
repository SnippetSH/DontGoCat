// CatVoice 테스트. 재생은 가짜 Player 로 바꿔 실제 소리를 내지 않는다.
//   - meow() 한 번 = 정확히 한 번 재생, 소리 번호 ∈ [0, kMeowSoundCount)
//   - 여러 번 부르면 (시드 고정 난수) 모든 소리 번호가 한 번 이상 나온다
//   - 꺼져 있으면 재생하지 않는다
//   - scaledWav: 16-bit PCM data 청크만 percent 비율로 줄이고 헤더 / 다른 청크는 그대로
//   - setVolume 은 0~100 으로 자르고, 기본값은 kMeowVolume
#include "TestCheck.hpp"

#include "CatVoice.hpp"
#include "Config.hpp"

#include <QCoreApplication>
#include <QRandomGenerator>
#include <QtEndian>

#include <cstdio>
#include <vector>

static void testMeowOnce()
{
    std::printf("[1] meow once\n");
    QRandomGenerator rng(1234);
    int played = 0;
    int last = -1;
    CatVoice voice(nullptr, [&](int index) {
        ++played;
        last = index;
    });
    voice.setRandomGenerator(&rng);
    voice.meow();
    CHECK(played == 1, "played once (%d)", played);
    CHECK(last >= 0 && last < Config::kMeowSoundCount, "index in range (%d)", last);
}

static void testAllSoundsAppear()
{
    std::printf("[2] all sounds appear / disabled\n");
    QRandomGenerator rng(99);
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

    const int n = 200;
    for (int i = 0; i < n; ++i)
        voice.meow();
    CHECK(played == n, "every meow plays (%d / %d)", played, n);
    CHECK(bad == 0, "sound index out of range (%d)", bad);
    for (int i = 0; i < Config::kMeowSoundCount; ++i)
        CHECK(counts[i] > 0, "sound %d played (%d)", i, counts[i]);

    voice.setEnabled(false);
    for (int i = 0; i < n; ++i)
        voice.meow();
    CHECK(played == n, "disabled: no play (%d)", played);
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

static void testSetVolume()
{
    std::printf("[4] setVolume\n");
    CatVoice voice(nullptr, [](int) {});
    CHECK(voice.volume() == Config::kMeowVolume, "default volume %d", voice.volume());
    voice.setVolume(40);
    CHECK(voice.volume() == 40, "volume 40 (%d)", voice.volume());
    voice.setVolume(250);
    CHECK(voice.volume() == 100, "clamped high (%d)", voice.volume());
    voice.setVolume(-5);
    CHECK(voice.volume() == 0, "clamped low (%d)", voice.volume());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testMeowOnce();
    testAllSoundsAppear();
    testVolume();
    testSetVolume();
    return testing::testResult("test_voice");
}
