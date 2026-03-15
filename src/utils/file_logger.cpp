#include "file_logger.h"
#include "portable_paths.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QTextStream>

#include <cstdio>

namespace occt { namespace utils {

QFile FileLogger::logFile_;
QMutex FileLogger::mutex_;
bool FileLogger::initialized_ = false;

void FileLogger::init()
{
    if (initialized_) return;

    openLogFile();
    qInstallMessageHandler(messageHandler);
    initialized_ = true;
}

void FileLogger::shutdown()
{
    if (!initialized_) return;

    qInstallMessageHandler(nullptr); // restore default handler
    QMutexLocker lock(&mutex_);
    if (logFile_.isOpen()) {
        logFile_.close();
    }
    initialized_ = false;
}

void FileLogger::messageHandler(QtMsgType type,
                                const QMessageLogContext& /*context*/,
                                const QString& msg)
{
    const char* level = nullptr;
    switch (type) {
    case QtDebugMsg:    level = "DEBUG"; break;
    case QtInfoMsg:     level = "INFO "; break;
    case QtWarningMsg:  level = "WARN "; break;
    case QtCriticalMsg: level = "ERROR"; break;
    case QtFatalMsg:    level = "FATAL"; break;
    }

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    QString formatted = QString("[%1] %2 %3\n").arg(timestamp, level, msg);

    // Always forward to stderr so qDebug() output still appears in console
    QByteArray utf8 = formatted.toUtf8();
    std::fputs(utf8.constData(), stderr);

    // Write to log file
    QMutexLocker lock(&mutex_);
    if (logFile_.isOpen()) {
        rotateIfNeeded();
        if (logFile_.write(utf8) == -1) {
            // Fail silently — avoid recursive logging from within the logger
            return;
        }
        logFile_.flush();
    }

    if (type == QtFatalMsg) {
        std::abort();
    }
}

void FileLogger::rotateIfNeeded()
{
    if (logFile_.size() < MAX_FILE_SIZE) return;

    logFile_.close();

    QString basePath = logFile_.fileName();
    QFileInfo fi(basePath);
    QString dir = fi.absolutePath();
    QString baseName = fi.completeBaseName();
    QString ext = fi.suffix();

    // Remove oldest rotated file
    QString oldest = QString("%1/%2.%3.%4").arg(dir, baseName).arg(MAX_ROTATED_FILES).arg(ext);
    QFile::remove(oldest);

    // Shift rotated files: .4 -> .5, .3 -> .4, etc.
    for (int i = MAX_ROTATED_FILES - 1; i >= 1; --i) {
        QString src = QString("%1/%2.%3.%4").arg(dir, baseName).arg(i).arg(ext);
        QString dst = QString("%1/%2.%3.%4").arg(dir, baseName).arg(i + 1).arg(ext);
        QFile::rename(src, dst);
    }

    // Current log becomes .1
    QString first = QString("%1/%2.1.%3").arg(dir, baseName, ext);
    QFile::rename(basePath, first);

    // Open a fresh log file
    logFile_.setFileName(basePath);
    (void)logFile_.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

void FileLogger::openLogFile()
{
    QString logsDir = PortablePaths::logsDir();
    QDir().mkpath(logsDir);

    QString date = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    QString filePath = logsDir + "/occt_" + date + ".log";

    logFile_.setFileName(filePath);
    (void)logFile_.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

}} // namespace occt::utils
