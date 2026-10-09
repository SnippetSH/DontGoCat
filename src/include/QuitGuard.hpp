#pragma once

#include <QtGlobal>

#include <functional>
#include <optional>

// 종료 방해 확률 판정 (README 5.5). Qt UI 와 무관한 순수 로직.
//   직전 시도 후 Config::kQuitRetryWindowMs 이내 → 직전 확률 - kQuitBlockStep (최소 0)
//   그 외                                         → kQuitBlockBaseChance
// "시도" = 종료 버튼 hover 진입 판정 / 방해 중 고양이 몸 클릭 (버튼 클릭은 시도가 아니라 곧바로 종료)
class QuitGuard
{
public:
    using Rng = std::function<int()>;   // [0, 100) 균등 난수. 비어 있으면 QRandomGenerator::global()

    explicit QuitGuard(Rng rng = {});

    // 시도 1회를 기록하고 판정. true = 방해한다
    bool attempt(qint64 nowMs);

    int lastChance() const { return m_chance; }   // 마지막 판정에 쓴 확률 (%)
    void reset();

private:
    Rng m_rng;
    int m_chance = 0;
    std::optional<qint64> m_lastAttemptMs;
};
