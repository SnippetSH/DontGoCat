#pragma once

#include <QByteArray>
#include <QObject>
#include <QTimer>

#include <functional>
#include <vector>

class QRandomGenerator;

// 고양이 울음소리. Config::kMeowMinMs ~ kMeowMaxMs 사이 무작위 간격마다 판정해서
// Config::kMeowChance % 확률로 리소스(:/sounds/meow1~N.wav) 중 하나를 골라 재생한다.
// 재생은 Win32 PlaySound(SND_MEMORY | SND_ASYNC) — Qt Multimedia 없이 exe 안의 wav 를 그대로 쓴다.
// - 크기: PlaySound 에 볼륨이 없어서 로드 시 샘플에 Config::kMeowVolume % 를 곱해 둔다.
// - 꺼져 있거나(setEnabled(false)) canMeow() 가 false(자는 중 / 숨은 중)면 판정 없이 다음 간격만 다시 잡는다.
class CatVoice : public QObject
{
    Q_OBJECT

public:
    using Player = std::function<void(int index)>;   // 재생 대체 (테스트용, 비면 PlaySound)
    using Gate = std::function<bool()>;              // 지금 울어도 되는가 (비면 항상 true)

    explicit CatVoice(QObject *parent = nullptr, Player player = {});
    ~CatVoice() override;

    // 테스트용 이음새: 난수 발생기를 바꾼다 (소유권 없음, nullptr 이면 QRandomGenerator::global())
    void setRandomGenerator(QRandomGenerator *rng) { m_rng = rng; }
    void setCanMeow(Gate gate) { m_canMeow = std::move(gate); }

    void setEnabled(bool enabled);   // 끄면 재생 중인 소리도 멈춘다
    bool isEnabled() const { return m_enabled; }

    void start();                    // 첫 판정 타이머 시작
    int nextIntervalMs() const { return m_timer.interval(); }

    // 16-bit PCM wav 의 data 청크 샘플에 percent/100 을 곱한 사본 (그 외 형식이나 깨진 파일은 그대로 돌려준다)
    static QByteArray scaledWav(QByteArray wav, int percent);

public slots:
    void meowTick();                 // 타이머 만료와 동일 (테스트에서 직접 호출): 판정 → 다음 간격 예약

signals:
    void meowed(int index);          // 재생한 소리 번호 (0 ~ kMeowSoundCount-1)

private:
    QRandomGenerator &rng() const;
    void play(int index);
    void stopPlayback();

    Player m_player;
    Gate m_canMeow;
    QRandomGenerator *m_rng = nullptr;
    QTimer m_timer;
    std::vector<QByteArray> m_sounds;   // wav 전체 (SND_ASYNC 재생 중에도 살아 있어야 한다)
    bool m_enabled = true;
};
