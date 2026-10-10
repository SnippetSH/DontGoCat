#include "CatVoice.hpp"

#include "Config.hpp"

#include <QFile>
#include <QRandomGenerator>
#include <QtEndian>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif

#include <algorithm>
#include <cstring>
#include <utility>

static_assert(Config::kMeowVolume >= 0 && Config::kMeowVolume <= 100, "kMeowVolume 은 0~100 (%)");   // 기본값

CatVoice::CatVoice(QObject *parent, Player player)
    : QObject(parent)
    , m_player(std::move(player))
{
    if (!m_player) {
        // 리소스에서 한 번만 읽어 둔다. 없거나 비어 있으면 그 번호는 건너뛴다 (play 에서 확인)
        auto load = [this](const QString &path) {
            QFile f(path);
            m_originals.push_back(f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray());
        };
        for (int i = 1; i <= Config::kMeowSoundCount; ++i)
            load(QStringLiteral(":/sounds/meow%1.wav").arg(i));
        for (int i = 1; i <= Config::kGrabSoundCount; ++i)
            load(QStringLiteral(":/sounds/grab%1.wav").arg(i));
    }
    setVolume(Config::kMeowVolume);
}

CatVoice::~CatVoice()
{
    stopPlayback();   // 버퍼가 사라지기 전에 비동기 재생을 끊는다
}

QByteArray CatVoice::scaledWav(QByteArray wav, int percent)
{
    percent = std::clamp(percent, 0, 100);
    if (percent == 100 || wav.size() < 12 || std::memcmp(wav.constData(), "RIFF", 4) != 0
        || std::memcmp(wav.constData() + 8, "WAVE", 4) != 0)
        return wav;

    // 청크 순회: fmt 에서 형식 확인, data 에서 샘플 조정
    bool pcm16 = false;
    qsizetype pos = 12;
    while (pos + 8 <= wav.size()) {
        const char *chunk = wav.constData() + pos;
        const qsizetype size = qFromLittleEndian<quint32>(chunk + 4);
        const qsizetype body = pos + 8;
        if (body + size > wav.size())
            break;
        if (std::memcmp(chunk, "fmt ", 4) == 0 && size >= 16) {
            const quint16 format = qFromLittleEndian<quint16>(chunk + 8);
            const quint16 bits = qFromLittleEndian<quint16>(chunk + 22);
            pcm16 = format == 1 && bits == 16;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            if (!pcm16)
                break;
            char *data = wav.data() + body;   // 분리(detach)된 사본에 쓴다
            for (qsizetype i = 0; i + 1 < size; i += 2) {
                const qint16 s = qFromLittleEndian<qint16>(data + i);
                qToLittleEndian<qint16>(qint16(s * percent / 100), data + i);
            }
            break;
        }
        pos = body + size + (size & 1);   // 청크는 짝수 바이트 정렬
    }
    return wav;
}

QRandomGenerator &CatVoice::rng() const
{
    return m_rng ? *m_rng : *QRandomGenerator::global();
}

void CatVoice::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!enabled)
        stopPlayback();
}

void CatVoice::setVolume(int percent)
{
    m_volume = std::clamp(percent, 0, 100);
    stopPlayback();   // 재생 중인 버퍼를 바꾸기 전에 끊는다
    m_sounds.clear();
    for (const QByteArray &wav : m_originals)
        m_sounds.push_back(scaledWav(wav, m_volume));
}

void CatVoice::meow()
{
    if (m_enabled)
        play(rng().bounded(Config::kMeowSoundCount));
}

void CatVoice::grabCry()
{
    if (m_enabled)
        play(Config::kMeowSoundCount + rng().bounded(Config::kGrabSoundCount));
}

void CatVoice::play(int index)
{
    if (m_player) {
        m_player(index);
    } else {
#ifdef Q_OS_WIN
        const QByteArray &wav = m_sounds[index];
        if (wav.isEmpty())
            return;
        PlaySoundW(reinterpret_cast<LPCWSTR>(wav.constData()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#endif
    }
    emit meowed(index);
}

void CatVoice::stopPlayback()
{
#ifdef Q_OS_WIN
    if (!m_player)
        PlaySoundW(nullptr, nullptr, 0);
#endif
}
