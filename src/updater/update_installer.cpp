#include "update_installer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTextStream>

#include "utils/portable_paths.h"

namespace occt { namespace updater {

using occt::utils::PortablePaths;

UpdateInstaller::UpdateInstaller(QObject* parent)
    : QObject(parent)
{
}

// ─── Main install entry point ────────────────────────────────────────────────
bool UpdateInstaller::install(const QString& zipPath)
{
    const QString updateDir = PortablePaths::tempDir() + QStringLiteral("/occt_update");
    const QString appDir    = PortablePaths::appDir();

    // Clean previous update directory if it exists
    QDir dir(updateDir);
    if (dir.exists()) {
        dir.removeRecursively();
    }
    dir.mkpath(updateDir);

    // Stage 1: Extract ZIP
    emit stageChanged(QStringLiteral("파일 검증 중..."));
    if (!extractZip(zipPath, updateDir)) {
        emit installFailed(QStringLiteral("ZIP 파일 해제에 실패했습니다."));
        return false;
    }

    // Stage 2: Replace files
    emit stageChanged(QStringLiteral("설치 준비 중..."));

#ifdef Q_OS_WIN
    if (!createAndRunBatchScript(updateDir, appDir)) {
        emit installFailed(QStringLiteral("업데이트 스크립트 실행에 실패했습니다."));
        return false;
    }
#else
    if (!directReplace(updateDir, appDir)) {
        emit installFailed(QStringLiteral("파일 교체에 실패했습니다."));
        return false;
    }

    // Clean up update directory
    QDir(updateDir).removeRecursively();
#endif

    emit installStarted();
    return true;
}

// ─── ZIP extraction ──────────────────────────────────────────────────────────
bool UpdateInstaller::extractZip(const QString& zipPath, const QString& destDir)
{
#ifdef Q_OS_WIN
    // Use PowerShell Expand-Archive on Windows
    QProcess ps;
    ps.setProgram(QStringLiteral("powershell.exe"));
    ps.setArguments({
        QStringLiteral("-NoProfile"),
        QStringLiteral("-Command"),
        QStringLiteral("Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
            .arg(zipPath, destDir)
    });
    ps.start();
    ps.waitForFinished(120000); // 2 min timeout
    return ps.exitCode() == 0;
#else
    // Use unzip command on macOS/Linux
    QProcess proc;
    proc.setProgram(QStringLiteral("unzip"));
    proc.setArguments({QStringLiteral("-o"), zipPath, QStringLiteral("-d"), destDir});
    proc.start();
    proc.waitForFinished(120000);
    return proc.exitCode() == 0;
#endif
}

// ─── Windows: batch script strategy ─────────────────────────────────────────
#ifdef Q_OS_WIN
bool UpdateInstaller::createAndRunBatchScript(const QString& updateDir,
                                              const QString& appDir)
{
    const QString batPath = PortablePaths::tempDir() + QStringLiteral("/update.bat");

    QFile batFile(batPath);
    if (!batFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    // Native path separators for batch script
    const QString nativeUpdateDir = QDir::toNativeSeparators(updateDir);
    const QString nativeAppDir    = QDir::toNativeSeparators(appDir);
    const QString exeName         = QFileInfo(QCoreApplication::applicationFilePath()).fileName();

    QTextStream out(&batFile);
    out.setEncoding(QStringConverter::Utf8);
    out << "@echo off\r\n";
    out << "chcp 65001 >NUL\r\n";
    out << "echo [OCCT Updater] 업데이트 진행 중...\r\n";
    out << "echo [OCCT Updater] 프로세스 종료 대기...\r\n";
    out << ":wait\r\n";
    out << "tasklist /FI \"IMAGENAME eq " << exeName
        << "\" 2>NUL | find /I \"" << exeName << "\" >NUL\r\n";
    out << "if not errorlevel 1 (timeout /t 1 /nobreak >NUL & goto wait)\r\n";
    out << "echo [OCCT Updater] 파일 교체 중...\r\n";
    out << "xcopy /Y /E \"" << nativeUpdateDir << "\\*\" \""
        << nativeAppDir << "\\\"\r\n";
    out << "echo [OCCT Updater] 재시작...\r\n";
    out << "start \"\" \"" << nativeAppDir << "\\" << exeName
        << "\" --post-update\r\n";
    out << "echo [OCCT Updater] 정리...\r\n";
    out << "rmdir /S /Q \"" << nativeUpdateDir << "\"\r\n";
    out << "del \"%~f0\"\r\n";

    batFile.close();

    // Launch batch script detached and quit the app
    bool ok = QProcess::startDetached(
        QStringLiteral("cmd.exe"),
        {QStringLiteral("/C"), batPath});

    if (ok) {
        QCoreApplication::quit();
    }
    return ok;
}
#else
bool UpdateInstaller::createAndRunBatchScript(const QString& /*updateDir*/,
                                              const QString& /*appDir*/)
{
    return false; // Not used on non-Windows
}
#endif

// ─── macOS/Linux: direct file replacement ───────────────────────────────────
bool UpdateInstaller::directReplace(const QString& updateDir, const QString& appDir)
{
    return copyDirectory(updateDir, appDir);
}

// ─── Recursive directory copy ────────────────────────────────────────────────
bool UpdateInstaller::copyDirectory(const QString& srcDir, const QString& dstDir)
{
    QDir src(srcDir);
    QDir dst(dstDir);

    if (!dst.exists()) {
        dst.mkpath(QStringLiteral("."));
    }

    const auto entries = src.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);

    for (const QFileInfo& entry : entries) {
        const QString srcPath = entry.absoluteFilePath();
        const QString dstPath = dstDir + QStringLiteral("/") + entry.fileName();

        if (entry.isDir()) {
            if (!copyDirectory(srcPath, dstPath)) {
                return false;
            }
        } else {
            // Remove existing file first (needed for executables on some platforms)
            if (QFile::exists(dstPath)) {
                QFile::remove(dstPath);
            }
            if (!QFile::copy(srcPath, dstPath)) {
                return false;
            }
            // Preserve executable permissions
            QFile::setPermissions(dstPath, entry.permissions());
        }
    }
    return true;
}

// ─── Restart application ────────────────────────────────────────────────────
void UpdateInstaller::restartApp(const QStringList& extraArgs)
{
    const QString appPath = QCoreApplication::applicationFilePath();
    QStringList args = extraArgs;

    QProcess::startDetached(appPath, args);
    QCoreApplication::quit();
}

}} // namespace occt::updater
