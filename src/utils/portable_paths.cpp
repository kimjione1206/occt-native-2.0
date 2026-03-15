#include "portable_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace occt { namespace utils {

bool PortablePaths::portable_ = false;
QString PortablePaths::baseDir_;

void PortablePaths::init()
{
    QString exeDir = QCoreApplication::applicationDirPath();

    // Check if portable.ini exists next to the executable
    QString portableMarker = exeDir + "/portable.ini";
    if (QFile::exists(portableMarker) && checkWritable(exeDir)) {
        portable_ = true;
        baseDir_ = exeDir;
    } else if (checkWritable(exeDir)) {
        // Writable exe directory but no marker — still use portable layout
        portable_ = true;
        baseDir_ = exeDir;
    } else {
        // Fall back to standard paths (e.g., Program Files is not writable)
        portable_ = false;
        baseDir_ = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }

    // Ensure required directories exist
    QDir dir;
    dir.mkpath(configDir());
    dir.mkpath(logsDir());
    dir.mkpath(tempDir());
}

QString PortablePaths::appDir()
{
    return QCoreApplication::applicationDirPath();
}

QString PortablePaths::configDir()
{
    return baseDir_ + "/config";
}

QString PortablePaths::logsDir()
{
    return baseDir_ + "/logs";
}

QString PortablePaths::tempDir()
{
    QString tmp = baseDir_ + "/temp";
    if (checkWritable(baseDir_)) {
        return tmp;
    }
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation)
           + "/occt-native";
}

bool PortablePaths::isPortable()
{
    return portable_;
}

bool PortablePaths::checkWritable(const QString& path)
{
    QDir dir(path);
    if (!dir.exists()) {
        return dir.mkpath(path);
    }

    // Try creating a temporary file to verify write access
    QTemporaryFile testFile(path + "/occt_write_test_XXXXXX");
    testFile.setAutoRemove(true);
    return testFile.open();
}

}} // namespace occt::utils
