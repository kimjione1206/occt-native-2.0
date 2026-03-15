#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QFrame>

namespace occt { namespace gui {

class SysInfoPanel : public QWidget {
    Q_OBJECT

public:
    explicit SysInfoPanel(QWidget* parent = nullptr);

private:
    void setupUi();
    QFrame* createSection(const QString& title, const QVector<QPair<QString,QString>>& rows);

    QLabel* cpuModelLabel_  = nullptr;
    QLabel* gpuModelLabel_  = nullptr;
    QLabel* ramTotalLabel_  = nullptr;
    QLabel* osNameLabel_    = nullptr;
};

}} // namespace occt::gui
