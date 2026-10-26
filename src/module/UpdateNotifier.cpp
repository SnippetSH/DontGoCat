#include "UpdateNotifier.hpp"

#include <QMessageBox>
#include <QPushButton>

void DialogUpdateNotifier::notify(const QString &latestVersion, const QString &currentVersion,
                                  std::function<void(UpdateChoice)> done)
{
    // 부모 없는 비모달 창: 메인 tick 을 막지 않는다. 고양이 오버레이(최상위)에 가리지 않게 최상위로 띄운다
    auto *box = new QMessageBox(QMessageBox::Information, QStringLiteral("DontGoCat"),
                                QStringLiteral("새 버전 %1 이 나왔어요.\n지금 버전: %2").arg(latestVersion, currentVersion));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowFlag(Qt::WindowStaysOnTopHint);
    QPushButton *open = box->addButton(QStringLiteral("릴리스 페이지 열기"), QMessageBox::AcceptRole);
    QPushButton *later = box->addButton(QStringLiteral("나중에"), QMessageBox::RejectRole);
    QPushButton *skip = box->addButton(QStringLiteral("이 버전 건너뛰기"), QMessageBox::DestructiveRole);
    box->setDefaultButton(open);
    box->setEscapeButton(later);   // 닫기(X) / Esc = 나중에

    QObject::connect(box, &QMessageBox::buttonClicked, box, [=](QAbstractButton *button) {
        if (button == open)
            done(UpdateChoice::OpenPage);
        else if (button == skip)
            done(UpdateChoice::Skip);
        else
            done(UpdateChoice::Later);
    });
    box->show();
    box->raise();
    box->activateWindow();
}
