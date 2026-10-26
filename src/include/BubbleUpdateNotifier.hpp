#pragma once

#include "CatSprite.hpp"
#include "UpdateNotifier.hpp"

#include <QObject>
#include <QRect>

#include <functional>

class CatBrain;
class CatVoice;
class SpeechBubble;

// 말풍선을 띄울 위치 (CatApp 이 현재 프레임에서 계산해 준다)
struct BubblePlacement
{
    QRect catRect;                          // 화면에 그려진 고양이 이미지의 데스크탑 사각형
    CatGravity gravity = CatGravity::Down;
    QRect monitor;                          // 고양이가 있는 모니터
};

// 고양이 말풍선으로 새 버전을 알린다 (README 5.15).
//   notify → brain.requestAnnounce() → announceStarted: 문구 설정 + bubble.showFor(placement()) + voice.meow() 1회
//   말풍선 선택: open → OpenPage, × → Later, 링크 → Skip, 시간 초과 → Later
//   announceInterrupted (종료 방해 / 트레이 접근 / 잡기 / 숨김 / 낙하) → bubble.dismiss(), Later
//   어떤 경우든 done 은 정확히 한 번, 선택으로 끝나면 brain.endAnnounce() 를 부른다.
//   다시 숨었다 뜨는 일은 없다 (끼어들면 닫힌다) → 울음도 한 번뿐.
// 진행 중 알림이 있는데 notify 가 또 오면 앞의 것을 Later 로 끝내고 새로 시작한다.
class BubbleUpdateNotifier : public QObject, public UpdateNotifier
{
    Q_OBJECT

public:
    BubbleUpdateNotifier(CatBrain &brain, SpeechBubble &bubble, CatVoice &voice,
                         std::function<BubblePlacement()> placement, QObject *parent = nullptr);

    void notify(const QString &latestVersion, const QString &currentVersion,
                std::function<void(UpdateChoice)> done) override;

private:
    CatBrain &m_brain;
    SpeechBubble &m_bubble;
    CatVoice &m_voice;
    std::function<BubblePlacement()> m_placement;
    // 진행 중 알림 상태. m_active 가 false 면 들어오는 시그널은 모두 무시한다
    void onAnnounceStarted();
    void onAnnounceInterrupted();
    // 알림을 끝낸다: 상태를 먼저 비우고 (done 안에서 notify 가 다시 불려도 안전) done 을 정확히 한 번 부른다.
    // callEndAnnounce: 브레인이 아직 Announce 중이면 true (선택 / 교체), 이미 빠져나갔으면 false (끼어듦)
    void finish(UpdateChoice choice, bool callEndAnnounce);

    bool m_active = false;    // notify 후 done 호출 전까지
    bool m_started = false;   // announceStarted 를 받아 말풍선을 띄웠다 (울음 한 번 보장)
    QString m_latest;
    std::function<void(UpdateChoice)> m_done;
};
