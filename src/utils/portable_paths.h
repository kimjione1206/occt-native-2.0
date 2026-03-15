#pragma once

#include <QString>

namespace occt { namespace utils {

class PortablePaths {
public:
    static void init();
    static QString appDir();
    static QString configDir();
    static QString logsDir();
    static QString tempDir();
    static bool isPortable();

private:
    static bool checkWritable(const QString& path);
    static bool portable_;
    static QString baseDir_;
};

}} // namespace occt::utils
