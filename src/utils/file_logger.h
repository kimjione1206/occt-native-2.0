#pragma once

#include <QFile>
#include <QMutex>
#include <QString>

namespace occt { namespace utils {

class FileLogger {
public:
    static void init();
    static void shutdown();

private:
    static void messageHandler(QtMsgType type,
                               const QMessageLogContext& context,
                               const QString& msg);
    static void rotateIfNeeded();
    static void openLogFile();

    static QFile logFile_;
    static QMutex mutex_;
    static bool initialized_;

    static constexpr qint64 MAX_FILE_SIZE = 5 * 1024 * 1024; // 5 MB
    static constexpr int MAX_ROTATED_FILES = 5;
};

}} // namespace occt::utils
