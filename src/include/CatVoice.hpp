#pragma once

#include <QByteArray>
#include <QObject>

#include <functional>
#include <vector>

class QRandomGenerator;

// 고양이 울음소리 (README 5.12). 이벤트가 있을 때만 운다 — 무작위 타이머 없음.
//   종료 방해 시작 / 방해 유지 반응 / 방해 해제 (TrayController::blockRequested / blockReact / blockReleased)
//   음량 슬라이더 확정 (미리듣기)
// meow() 한 번 = 리소스(:/sounds/meow1~N.wav) 중 무작위 하나 재생. 재생 중에 다시 부르면 앞 소리를 끊고 새로 재생.
// 재생은 Win32 PlaySound(SND_MEMORY | SND_ASYNC) — Qt Multimedia 없이 exe 안의 wav 를 그대로 쓴다.
// - 크기: PlaySound 에 볼륨이 없어서 원본 wav 를 보관해 두고, setVolume 때마다 샘플에 % 를 곱한 사본을 다시 만든다.
// - 꺼져 있으면(setEnabled(false)) meow() 는 아무것도 하지 않는다.
class CatVoice : public QObject
{
    Q_OBJECT

public:
    using Player = std::function<void(int index)>;   // 재생 대체 (테스트용, 비면 PlaySound)

    explicit CatVoice(QObject *parent = nullptr, Player player = {});
    ~CatVoice() override;

    // 테스트용 이음새: 난수 발생기를 바꾼다 (소유권 없음, nullptr 이면 QRandomGenerator::global())
    void setRandomGenerator(QRandomGenerator *rng) { m_rng = rng; }

    void setEnabled(bool enabled);   // 끄면 재생 중인 소리도 멈춘다
    bool isEnabled() const { return m_enabled; }

    void setVolume(int percent);     // 0~100. 재생 중인 소리를 멈추고 사본을 다시 만든다
    int volume() const { return m_volume; }

    // 16-bit PCM wav 의 data 청크 샘플에 percent/100 을 곱한 사본 (그 외 형식이나 깨진 파일은 그대로 돌려준다)
    static QByteArray scaledWav(QByteArray wav, int percent);

public slots:
    void meow();                     // 켜져 있으면 무작위 소리 하나 즉시 재생

signals:
    void meowed(int index);          // 재생한 소리 번호 (0 ~ kMeowSoundCount-1)

private:
    QRandomGenerator &rng() const;
    void play(int index);
    void stopPlayback();

    Player m_player;
    QRandomGenerator *m_rng = nullptr;
    std::vector<QByteArray> m_originals;   // 리소스에서 읽은 원본 wav
    std::vector<QByteArray> m_sounds;      // 크기 적용 사본 (SND_ASYNC 재생 중에도 살아 있어야 한다)
    int m_volume = 100;
    bool m_enabled = true;
};
