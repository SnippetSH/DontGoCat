#pragma once

#include "CatSprite.hpp"

#include <QByteArray>
#include <QPoint>
#include <QRect>
#include <QRegion>
#include <QSize>
#include <QVector>
#include <QString>
#include <QWidget>

class QTimer;

// 고양이 옆에 뜨는 픽셀 말풍선 (README 5.15). 업데이트 알림 전용으로 만들었지만 문구는 바깥에서 받는다.
// - 테두리 없는 최상위 창. 포커스를 가져가지 않는다 (WA_ShowWithoutActivating, WS_EX_NOACTIVATE). 클릭은 받는다
// - 모양: 흰 바탕 + 스프라이트 px 단위 각진 테두리 + 고양이 쪽 꼬리. 테두리/꼬리 두께는 배율을 따르고 글자 크기는 고정
// - 내용: 본문 문구, 오른쪽 위 ×, 아래 작은 링크 "이 버전 건너뛰기"
// - 클릭: × → laterClicked, 링크 → skipClicked, 그 밖의 말풍선 → openClicked (클릭 후 스스로 닫힌다)
// - 표시 후 Config::kBubbleTimeoutMs 동안 아무것도 안 누르면 timedOut 후 닫힌다. 커서가 올라가 있는 동안은 시간이 멈춘다
// - 위치: 바닥/지붕(gravity Down)이면 고양이 위, 벽(Left/Right)이면 벽 반대쪽 옆. monitor 밖으로 나가지 않게 clamp
// - 크기는 물리 px 고정 (DpiSizeLock)
class SpeechBubble : public QWidget
{
    Q_OBJECT

public:
    explicit SpeechBubble(QWidget *parent = nullptr);

    void setScale(int scale);   // 보이는 중이면 마지막 배치 기준으로 다시 배치
    int scale() const { return m_scale; }

    void setText(const QString &message);   // 본문 (예: "새 버전 1.4.0 나왔다냥!")
    QString text() const { return m_text; }

    // catRect: 화면에 그려진 고양이 이미지의 데스크탑 사각형 (회전 반영), gravity: 고양이 중력 방향,
    // monitor: 고양이가 있는 모니터 사각형 (화면 밖 방지)
    void showFor(const QRect &catRect, CatGravity gravity, const QRect &monitor);   // 표시 + 타이머 시작
    void follow(const QRect &catRect, CatGravity gravity, const QRect &monitor);    // 보이는 동안 위치만 갱신
    void dismiss();          // 시그널 없이 닫는다 (타이머 정지)
    void ensureTopmost();    // 스캔 주기마다 최상위 재확인 (CatOverlay 와 같은 이유)

    // 테스트용 이음새
    void setTimeoutMs(int ms);
    QRect closeRect() const;   // × 영역 (위젯 좌표)
    QRect skipRect() const;    // "이 버전 건너뛰기" 링크 영역 (위젯 좌표)
    QRect bodyRect() const;    // 말풍선 몸통 (꼬리 제외, 위젯 좌표)
    CatGravity placedFor() const { return m_gravity; }   // 마지막 배치의 gravity

signals:
    void openClicked();
    void laterClicked();
    void skipClicked();
    void timedOut();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    void relayout();   // 문구 / 배율로 크기 계산 → 마지막 배치 기준으로 place()
    void place();      // m_catRect / m_gravity / m_monitor 로 창 위치 결정

    QString m_text;
    int m_scale = 3;
    QTimer *m_timeout = nullptr;
    QRect m_catRect;
    CatGravity m_gravity = CatGravity::Down;
    QRect m_monitor;
    enum class TailSide { Bottom, Top, Left, Right };   // 꼬리가 붙은 몸통 변 (고양이 쪽)
    enum class Region { None, Close, Skip, Body };      // 클릭 판정 영역

    void applyShape(TailSide side, int tailLat);   // 꼬리 방향에 맞춰 창 크기 / 몸통 위치 / 모양(mask) 갱신
    void buildShape(QVector<QRect> &outer, QVector<QRect> &inner) const;   // 테두리색 / 흰색 사각형 목록 (위젯 좌표)
    Region regionAt(const QPoint &pos) const;
    void hideBubble();   // 타이머 / hover 상태 정리 후 숨김 (시그널 없음)
    void pauseTimer();
    void resumeTimer();

    int unit() const;       // 테두리 한 칸 = kBubbleBorderSpritePx × scale (물리 px)
    int tailLength() const; // 꼬리 길이 (물리 px)

    TailSide m_side = TailSide::Bottom;
    int m_tailLat = 0;       // 꼬리 가로(세로 변이면 x, 가로 변이면 y) 시작 위치 (위젯 좌표)
    QPoint m_bodyOrigin;     // 몸통 왼쪽 위 (위젯 좌표)
    QSize m_bodySize;        // 몸통 크기 (꼬리 제외)
    QRect m_textLocal;       // 아래 세 영역은 몸통 기준 좌표
    QRect m_closeLocal;
    QRect m_skipLocal;
    QRegion m_shape;         // 말풍선 모양 (꼬리 포함, setMask 와 클릭 판정에 공용)
    Region m_pressRegion = Region::None;
    Region m_hoverRegion = Region::None;
    bool m_hover = false;    // 커서가 말풍선 위 → 시간 정지
    bool m_paused = false;
    int m_remainingMs = 0;
};
