#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPoint>
#include <QTimer>

#include <functional>
#include <optional>

// 커서 폴링 / 정지 감지 / 커서 툭툭 이동 (README 5.3).
// 키보드 입력과 무관하게 커서 위치만 본다. nudge() 로 고양이가 옮긴 이동은 사용자 이동으로 치지 않는다.
class MouseWatcher : public QObject
{
    Q_OBJECT

public:
    explicit MouseWatcher(QObject *parent = nullptr);

    void start();   // Config::kMousePollMs 주기
    void stop();

    QPoint pos() const { return m_pos; }
    qint64 idleMs() const;          // 마지막 "사용자" 이동 후 경과 ms
    bool anyButtonDown() const;     // 좌/우/가운데 버튼

    // 커서를 (dx, dy) 만큼 실제로 옮긴다. 버튼이 눌려 있으면 아무것도 안 하고 false.
    bool nudge(int dx, int dy);

    // 테스트용 이음새: 설정하면 nudge() 가 Win32 를 전혀 건드리지 않고 이 함수(dx, dy → 성공 여부)만 호출한다.
    // 비어 있으면(기본) 동작은 그대로. 실제 커서를 옮기지 않고 장난 로직을 검증할 때 쓴다.
    void setNudgeHook(std::function<bool(int, int)> hook) { m_nudgeHook = std::move(hook); }

signals:
    void userMoved(QPoint pos);
    void becameIdle(QPoint pos);    // idleMs 가 Config::kMouseIdleMs 를 넘는 순간 1회

private:
    void poll();

    QTimer m_timer;
    QElapsedTimer m_sinceMove;
    QPoint m_pos;
    std::optional<QPoint> m_expectedAfterNudge;
    bool m_idleSignaled = false;
    std::function<bool(int, int)> m_nudgeHook;
};
