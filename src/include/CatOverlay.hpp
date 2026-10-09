#pragma once

#include "CatSprite.hpp"
#include "Surface.hpp"

#include <QImage>
#include <QWidget>

// 고양이를 그리는 투명 최상위 창 (README 3.4, 5.4).
// - 한 변 32 × scale 정사각형 고정 크기 (모든 회전을 담음)
// - 기본은 클릭 통과 (WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW)
// - 종료 버튼을 덮는 동안만 setClickThrough(false) → 불투명 픽셀만 클릭을 받는다
class CatOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit CatOverlay(QWidget *parent = nullptr);

    void setScale(int scale);
    int scale() const { return m_scale; }

    // orientedSprite: CatSprite::renderOriented() 결과 (스프라이트 px)
    // anchorDesktop : 앵커(CatSprite::anchorIn(gravity))가 놓일 데스크탑 물리 px 좌표
    void setFrame(const QImage &orientedSprite, QPoint anchorDesktop, CatGravity gravity);

    void setClickThrough(bool on);
    bool isClickThrough() const { return m_clickThrough; }

    // 작업표시줄 클릭 등으로 밀려난 최상위 상태 재확인 (스캔 주기마다 호출)
    void ensureTopmost();

    WindowHandle handle() const;

signals:
    void clicked(Qt::KeyboardModifiers modifiers);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void applyNativeStyle();
    void place();   // m_anchorDesktop / m_gravity / m_scale 로 창 왼쪽 위를 계산해 이동

    QImage m_sprite;
    CatGravity m_gravity = CatGravity::Down;
    int m_scale = 3;
    bool m_clickThrough = true;
    QPoint m_anchorDesktop;
    QPoint m_topLeft;
    bool m_placed = false;
    bool m_hasFrame = false;   // setFrame 가 한 번이라도 불렸는지
};
