#pragma once

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QPushButton>

namespace occt { namespace gui {

class TokenDialog : public QDialog {
    Q_OBJECT
public:
    explicit TokenDialog(QWidget* parent = nullptr);

private slots:
    void onSaveClicked();
    void onDeleteClicked();
    void onValidateClicked();

private:
    void updateStatus();

    QLineEdit* tokenEdit_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* descLabel_ = nullptr;
    QPushButton* validateBtn_ = nullptr;
    QPushButton* saveBtn_ = nullptr;
    QPushButton* deleteBtn_ = nullptr;
    QNetworkAccessManager* nam_ = nullptr;
};

}} // namespace occt::gui
