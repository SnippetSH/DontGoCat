#include "BubbleUpdateNotifier.hpp"

#include "CatBrain.hpp"
#include "CatVoice.hpp"
#include "Config.hpp"
#include "SpeechBubble.hpp"

BubbleUpdateNotifier::BubbleUpdateNotifier(CatBrain &brain, SpeechBubble &bubble, CatVoice &voice,
                                           std::function<BubblePlacement()> placement, QObject *parent)
    : QObject(parent)
    , m_brain(brain)
    , m_bubble(bubble)
    , m_voice(voice)
    , m_placement(std::move(placement))
{
    // 시그널은 생성자에서 한 번만 연결한다. 진행 중이 아닐 때 오는 것은 핸들러에서 걸러낸다
    connect(&m_brain, &CatBrain::announceStarted, this, &BubbleUpdateNotifier::onAnnounceStarted);
    connect(&m_brain, &CatBrain::announceInterrupted, this, &BubbleUpdateNotifier::onAnnounceInterrupted);
    // 말풍선 선택 (말풍선은 클릭 / 시간 초과 후 스스로 닫힌다). 표시 전의 시그널은 무시
    connect(&m_bubble, &SpeechBubble::openClicked, this, [this] {
        if (m_active && m_started)
            finish(UpdateChoice::OpenPage, true);
    });
    connect(&m_bubble, &SpeechBubble::laterClicked, this, [this] {
        if (m_active && m_started)
            finish(UpdateChoice::Later, true);
    });
    connect(&m_bubble, &SpeechBubble::skipClicked, this, [this] {
        if (m_active && m_started)
            finish(UpdateChoice::Skip, true);
    });
    connect(&m_bubble, &SpeechBubble::timedOut, this, [this] {
        if (m_active && m_started)
            finish(UpdateChoice::Later, true);
    });
}

void BubbleUpdateNotifier::notify(const QString &latestVersion, const QString &currentVersion,
                                  std::function<void(UpdateChoice)> done)
{
    Q_UNUSED(currentVersion)   // 말풍선 문구에는 새 버전만 쓴다

    // 앞의 알림이 아직 진행 중이면 Later 로 끝내고 말풍선 / 브레인 말하기를 정리한 뒤 새로 시작
    if (m_active) {
        m_bubble.dismiss();
        finish(UpdateChoice::Later, true);
    }

    m_active = true;
    m_started = false;
    m_latest = latestVersion;
    m_done = std::move(done);
    m_brain.requestAnnounce();   // 시작은 브레인이 announceStarted 로 알린다 (자는 중 / 방해 중이면 기다린다)
}

void BubbleUpdateNotifier::onAnnounceStarted()
{
    if (!m_active || m_started)
        return;
    m_started = true;
    m_bubble.setText(QString::fromUtf8(Config::kBubbleUpdateText).arg(m_latest));
    const BubblePlacement p = m_placement();
    m_bubble.showFor(p.catRect, p.gravity, p.monitor);
    m_voice.meow();   // 소리 on/off / 볼륨은 CatVoice 가 알아서 따른다
}

void BubbleUpdateNotifier::onAnnounceInterrupted()
{
    if (!m_active)
        return;
    m_bubble.dismiss();
    // 브레인은 이미 Announce 를 떠났으므로 endAnnounce 는 부르지 않는다 (불필요)
    finish(UpdateChoice::Later, false);
}

void BubbleUpdateNotifier::finish(UpdateChoice choice, bool callEndAnnounce)
{
    // done 을 먼저 멤버에서 꺼내고 상태를 비운다: done 안에서 notify 가 다시 불려도 안전, 두 번 호출 불가
    auto done = std::move(m_done);
    m_done = nullptr;
    m_active = false;
    m_started = false;
    m_latest.clear();

    if (callEndAnnounce)
        m_brain.endAnnounce();
    if (done)
        done(choice);
}
