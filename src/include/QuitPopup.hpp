#pragma once

#include <QIcon>
#include <QRect>
#include <QWidget>

class QButtonGroup;
class QCheckBox;
class QPushButton;
class QSlider;
class QTimer;

// 트레이 아이콘 클릭 시 뜨는 작은 팝업 (README 5.6).
// - Qt::Tool | FramelessWindowHint | WindowStaysOnTopHint  (Qt::Popup 금지: 고양이 클릭이 바깥 클릭이 됨)
// - 상단: 얼굴 아이콘 + "DontGoCat", 배율 선택 1x/2x/3x/4x, 자동 시작 / 소리 체크박스, 음량 슬라이더, 하단: 종료 버튼
// - 팝업 하단과 종료 버튼 하단 = 바닥 y (작업표시줄 윗면) → 바닥의 고양이가 버튼을 덮을 수 있다
// - 종료 버튼 크기 = Config::kQuitButtonW × kQuitButtonH (물리 px 고정, 배율과 무관)
// - 앱 비활성화 / Esc 로 닫힘
class QuitPopup : public QWidget
{
    Q_OBJECT

public:
    explicit QuitPopup(QWidget *parent = nullptr);

    void setScale(int scale);   // 배율 선택 표시 갱신 + 재배치
    void setAutoStartChecked(bool checked);   // 시그널 없이 체크 상태만 갱신 (표시 직전 레지스트리 상태 반영, 실패 시 되돌리기)
    bool autoStartChecked() const;
    void setSoundChecked(bool checked);       // 시그널 없이 체크 상태만 갱신 (표시 직전 현재 설정 반영)
    bool soundChecked() const;
    void setVolume(int percent);              // 시그널 없이 슬라이더 값만 갱신
    int volume() const;

    // anchorRect: 트레이 아이콘(또는 TrayNotifyWnd) 사각형, floorY: 바닥 y, monitor: 화면 밖 방지용
    void showAt(const QRect &anchorRect, int floorY, const QRect &monitor);

    QRect quitButtonDesktopRect() const;
    QRect desktopRect() const { return geometry(); }   // 팝업 전체 사각형 (CatBrain 의 커서 추적 영역 기준)

    // 리소스(:/icons)의 tray_face_*.png 전 크기를 담은 얼굴 아이콘 (트레이/팝업 공용)
    static QIcon faceIcon();

signals:
    void quitHovered();                                   // 종료 버튼 hover 진입
    void quitClicked();                                   // 종료 버튼 클릭 (눈에 보이는 = 덮이지 않은 부분을 눌렀다는 뜻)
    void scaleSelected(int scale);
    void autoStartToggled(bool enabled);                  // 사용자가 체크박스를 눌렀다
    void soundToggled(bool enabled);                      // 사용자가 소리 체크박스를 눌렀다
    void volumeSelected(int percent);                     // 사용자가 음량을 정했다 (변경이 멈춘 뒤 한 번, 같은 값이면 나가지 않음)
    void closed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void relayout();                 // 크기 재계산 + 앵커 기준 재배치
    void closeIfDeactivated();

    QPushButton *m_quitButton = nullptr;
    QButtonGroup *m_scaleGroup = nullptr;
    QCheckBox *m_autoStart = nullptr;
    QCheckBox *m_sound = nullptr;
    QSlider *m_volume = nullptr;
    QTimer *m_volumeCommit = nullptr;   // 클릭 이동 + 놓기, 휠 연속 변경을 한 번의 volumeSelected 로 묶는다
    int m_committedVolume = 0;          // 마지막으로 알린(또는 setVolume 으로 받은) 값
    int m_scale = 3;

    // 마지막 showAt 인자 (배율이 바뀌면 같은 기준으로 다시 배치)
    QRect m_anchorRect;
    QRect m_monitor;
    int m_floorY = 0;
    bool m_hasPlacement = false;
    bool m_wasActive = false;        // 표시 후 한 번이라도 활성화됐는지 (활성화 전 비활성 이벤트로 닫히지 않게)
};
