#include "QuitGuard.hpp"

#include "Config.hpp"

#include <QRandomGenerator>

#include <algorithm>

QuitGuard::QuitGuard(Rng rng)
    : m_rng(std::move(rng))
{
}

bool QuitGuard::attempt(qint64 nowMs)
{
    // 직전 시도 후 재시도 창 안이면 확률을 한 단계 깎고(최소 0), 아니면 기본 확률로 되돌린다
    if (m_lastAttemptMs && nowMs - *m_lastAttemptMs <= Config::kQuitRetryWindowMs)
        m_chance = std::max(0, m_chance - Config::kQuitBlockStep);
    else
        m_chance = Config::kQuitBlockBaseChance;
    m_lastAttemptMs = nowMs;

    const int roll = m_rng ? m_rng() : int(QRandomGenerator::global()->bounded(100));
    return roll < m_chance;   // true = 방해
}

void QuitGuard::reset()
{
    m_chance = 0;
    m_lastAttemptMs.reset();
}
