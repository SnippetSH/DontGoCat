#pragma once

#include <QString>

#include <functional>

// 새 버전 알림을 어떻게 보여 줄지 (README 5.15). 표시 방식만 갈아 끼울 수 있게 인터페이스로 분리한다.
// 지금은 DialogUpdateNotifier(QMessageBox). 트레이 풍선 등으로 바꿀 때는 구현을 하나 더 만들고
// CatApp 에서 생성하는 한 줄만 바꾼다. 선택 결과 처리(페이지 열기 / 건너뛰기 저장)는 CatApp 이 한다.
enum class UpdateChoice
{
    OpenPage,   // 릴리스 페이지를 브라우저로 연다
    Later,      // 아무것도 하지 않는다 (다음 실행 때 다시 알림)
    Skip,       // 이 버전은 다시 알리지 않는다
};

class UpdateNotifier
{
public:
    virtual ~UpdateNotifier() = default;

    // 비동기: 사용자가 고르면 done 을 한 번 부른다 (창을 닫기만 하면 Later)
    virtual void notify(const QString &latestVersion, const QString &currentVersion,
                        std::function<void(UpdateChoice)> done) = 0;
};

// 비모달 QMessageBox: [릴리스 페이지 열기] [나중에] [이 버전 건너뛰기]
class DialogUpdateNotifier : public UpdateNotifier
{
public:
    void notify(const QString &latestVersion, const QString &currentVersion,
                std::function<void(UpdateChoice)> done) override;
};
