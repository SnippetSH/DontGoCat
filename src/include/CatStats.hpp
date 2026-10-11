#pragma once

#include <QDate>
#include <QString>
#include <QtGlobal>

class QSettings;

// 통계 한 묶음 (누적 / 이번 실행 공용). 시간은 ms, 거리는 m (README 5.14)
struct CatStatsValues
{
    qint64 runMs = 0;          // 프로그램이 실행된 시간 (CatApp tick dt 합, 절전 등 긴 공백은 kMaxTickDtMs 로 잘림)
    qint64 roamMs = 0;         // 목적 없이 혼자 돌아다닌 시간 (Autonomous 중 수면 제외)
    qint64 sleepMs = 0;        // 잠잔 시간 (sleep 루프 동안)
    qint64 playMs = 0;         // 마우스 근처에서 논 시간 (MousePlay 전체)
    int nudges = 0;            // 커서를 툭툭 친 횟수 (실제로 커서가 움직인 nudge)
    int quitAttempts = 0;      // 종료 시도 = QuitGuard 판정 횟수 (방해 실패 포함)
    int quitBlocks = 0;        // 종료 방해 성공 = 그중 방해로 판정된 횟수
    int grabs = 0;             // 잡힌 횟수 (Shift 잡기 성공)
    int jumps = 0;             // 점프 횟수
    int falls = 0;             // 낙하 횟수 (점프 제외, 시작 등장 낙하 제외)
    double distanceM = 0.0;    // 걷고 달리고 오른 거리 (모니터 물리 DPI 로 환산한 실제 화면 거리)
};

// 누적(설치 이후) + 이번 실행 통계 (README 5.14).
// - add*() 는 누적과 이번 실행 양쪽에 더한다.
// - 누적만 QSettings(Config::kSettingsStatsGroup 그룹)에 저장/복원한다. 이번 실행 값은 저장하지 않는다.
// - since: 누적 통계를 처음 기록한 날짜 (설정에 없으면 load() 시점의 오늘로 정하고 다음 save() 에 기록)
// 순수 데이터 클래스: 타이머/시그널 없음. 저장 시점은 CatApp 이 정한다.
class CatStats
{
public:
    void load(QSettings &settings);          // 누적 값 + since 복원 (그룹은 내부에서 begin/end)
    void save(QSettings &settings) const;    // 누적 값 + since 기록

    void addRunMs(qint64 ms);
    void addRoamMs(qint64 ms);
    void addSleepMs(qint64 ms);
    void addPlayMs(qint64 ms);
    void addNudge();
    void addQuitAttempt(bool blocked);       // blocked 면 quitBlocks 도 +1
    void addGrab();
    void addJump();
    void addFall();
    void addDistanceM(double meters);

    const CatStatsValues &total() const { return m_total; }
    const CatStatsValues &session() const { return m_session; }
    QDate since() const { return m_since; }

    // 표시 형식 (README 5.14 표시 규칙)
    static QString formatDuration(qint64 ms);
    static QString formatDistance(double meters);
    static QString formatCount(int n);
    static QString formatRate(int part, int whole);   // whole == 0 이면 "-"

private:
    CatStatsValues m_total;
    CatStatsValues m_session;
    QDate m_since;
};
