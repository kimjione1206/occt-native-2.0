#include "cert_generator.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPainter>
#include <QFont>
#include <QSysInfo>
#include <QThread>

namespace occt {

QString CertGenerator::collect_system_info()
{
    QJsonObject info;
    info["os"] = QSysInfo::prettyProductName();
    info["kernel"] = QSysInfo::kernelVersion();
    info["arch"] = QSysInfo::currentCpuArchitecture();
    info["hostname"] = QSysInfo::machineHostName();
    info["cpu_threads"] = QThread::idealThreadCount();
    return QJsonDocument(info).toJson(QJsonDocument::Compact);
}

QString CertGenerator::compute_hash(const QString& system_info, const QJsonObject& results_json)
{
    QByteArray data;
    data.append(system_info.toUtf8());
    data.append(QJsonDocument(results_json).toJson(QJsonDocument::Compact));
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

QJsonObject CertGenerator::generate_json(const Certificate& cert) const
{
    QJsonObject obj;
    obj["tier"] = cert_tier_name(cert.tier);
    obj["passed"] = cert.passed;
    obj["hash_sha256"] = cert.hash_sha256;
    obj["issued_at"] = cert.issued_at.toString(Qt::ISODate);
    obj["expires_at"] = cert.expires_at.toString(Qt::ISODate);
    obj["system_info"] = QJsonDocument::fromJson(cert.system_info_json.toUtf8()).object();

    QJsonArray results;
    for (const auto& r : cert.results) {
        QJsonObject ro;
        ro["engine"] = r.engine;
        ro["mode"] = r.mode;
        ro["passed"] = r.passed;
        ro["errors"] = r.errors;
        ro["duration_secs"] = r.duration_secs;
        results.append(ro);
    }
    obj["results"] = results;

    return obj;
}

QString CertGenerator::generate_html(const Certificate& cert) const
{
    QString tierName = cert_tier_name(cert.tier);
    QString tierColor = cert_tier_color(cert.tier);
    QString passStatus = cert.passed ? "PASSED" : "FAILED";
    QString passColor = cert.passed ? "#27AE60" : "#C0392B";

    QString html;
    html += "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n";
    html += "<title>OCCT Stability Certificate - " + tierName + "</title>\n";
    html += "<style>\n";
    html += "  * { margin: 0; padding: 0; box-sizing: border-box; }\n";
    html += "  body { background: #0D1117; color: #C9D1D9; font-family: 'Segoe UI', Tahoma, sans-serif; padding: 40px; }\n";
    html += "  .cert { max-width: 800px; margin: 0 auto; border: 2px solid " + tierColor + "; border-radius: 12px; padding: 40px; background: #161B22; }\n";
    html += "  .header { text-align: center; margin-bottom: 30px; }\n";
    html += "  .header h1 { font-size: 28px; color: " + tierColor + "; margin-bottom: 8px; }\n";
    html += "  .header h2 { font-size: 18px; color: #8B949E; font-weight: normal; }\n";
    html += "  .badge { display: inline-block; padding: 8px 24px; border-radius: 20px; font-size: 20px; font-weight: bold; color: white; background: " + passColor + "; margin: 16px 0; }\n";
    html += "  .section { margin: 24px 0; }\n";
    html += "  .section h3 { color: #F0F6FC; font-size: 16px; margin-bottom: 12px; border-bottom: 1px solid #30363D; padding-bottom: 8px; }\n";
    html += "  table { width: 100%; border-collapse: collapse; }\n";
    html += "  th, td { padding: 8px 12px; text-align: left; border-bottom: 1px solid #21262D; }\n";
    html += "  th { color: #8B949E; font-size: 12px; text-transform: uppercase; }\n";
    html += "  td { color: #C9D1D9; font-size: 14px; }\n";
    html += "  .pass { color: #27AE60; font-weight: bold; }\n";
    html += "  .fail { color: #C0392B; font-weight: bold; }\n";
    html += "  .info-row { display: flex; justify-content: space-between; margin: 4px 0; }\n";
    html += "  .info-label { color: #8B949E; }\n";
    html += "  .info-value { color: #F0F6FC; }\n";
    html += "  .hash { font-family: monospace; font-size: 11px; color: #8B949E; word-break: break-all; margin-top: 20px; text-align: center; }\n";
    html += "  .footer { text-align: center; margin-top: 30px; color: #484F58; font-size: 12px; }\n";
    html += "</style>\n</head>\n<body>\n<div class=\"cert\">\n";

    // Header
    html += "  <div class=\"header\">\n";
    html += "    <h1>OCCT Stability Certificate</h1>\n";
    html += "    <h2>" + tierName + " Tier</h2>\n";
    html += "    <div class=\"badge\">" + passStatus + "</div>\n";
    html += "  </div>\n";

    // System info
    auto sysInfo = QJsonDocument::fromJson(cert.system_info_json.toUtf8()).object();
    html += "  <div class=\"section\">\n    <h3>System Information</h3>\n";
    for (auto it = sysInfo.begin(); it != sysInfo.end(); ++it) {
        html += "    <div class=\"info-row\"><span class=\"info-label\">" + it.key() + "</span>";
        html += "<span class=\"info-value\">" + it.value().toVariant().toString() + "</span></div>\n";
    }
    html += "  </div>\n";

    // Test results table
    html += "  <div class=\"section\">\n    <h3>Test Results</h3>\n";
    html += "    <table>\n      <tr><th>Engine</th><th>Mode</th><th>Duration</th><th>Errors</th><th>Status</th></tr>\n";
    for (const auto& r : cert.results) {
        QString statusCls = r.passed ? "pass" : "fail";
        QString statusText = r.passed ? "PASS" : "FAIL";
        int mins = static_cast<int>(r.duration_secs) / 60;
        int secs = static_cast<int>(r.duration_secs) % 60;
        html += "      <tr><td>" + r.engine + "</td><td>" + r.mode + "</td>";
        html += "<td>" + QString::number(mins) + "m " + QString::number(secs) + "s</td>";
        html += "<td>" + QString::number(r.errors) + "</td>";
        html += "<td class=\"" + statusCls + "\">" + statusText + "</td></tr>\n";
    }
    html += "    </table>\n  </div>\n";

    // Timestamps
    html += "  <div class=\"section\">\n    <h3>Certificate Details</h3>\n";
    html += "    <div class=\"info-row\"><span class=\"info-label\">Issued</span>";
    html += "<span class=\"info-value\">" + cert.issued_at.toString("yyyy-MM-dd hh:mm:ss") + "</span></div>\n";
    html += "    <div class=\"info-row\"><span class=\"info-label\">Expires</span>";
    html += "<span class=\"info-value\">" + cert.expires_at.toString("yyyy-MM-dd hh:mm:ss") + "</span></div>\n";
    html += "  </div>\n";

    // Hash
    html += "  <div class=\"hash\">SHA-256: " + cert.hash_sha256 + "</div>\n";

    html += "  <div class=\"footer\">Generated by OCCT Native Stress Test v1.0.0</div>\n";
    html += "</div>\n</body>\n</html>\n";

    return html;
}

QImage CertGenerator::generate_image(const Certificate& cert, int width, int height) const
{
    QImage img(width, height, QImage::Format_ARGB32);
    img.fill(QColor("#0D1117"));

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    QString tierName = cert_tier_name(cert.tier);
    QColor tierColor(cert_tier_color(cert.tier));
    QColor passColor(cert.passed ? "#27AE60" : "#C0392B");
    QString passText = cert.passed ? "PASSED" : "FAILED";

    // Border
    p.setPen(QPen(tierColor, 3));
    p.setBrush(QColor("#161B22"));
    p.drawRoundedRect(QRect(20, 20, width - 40, height - 40), 12, 12);

    // Title
    QFont titleFont("Segoe UI", 22, QFont::Bold);
    p.setFont(titleFont);
    p.setPen(tierColor);
    p.drawText(QRect(40, 40, width - 80, 40), Qt::AlignCenter, "OCCT Stability Certificate");

    // Tier
    QFont tierFont("Segoe UI", 16);
    p.setFont(tierFont);
    p.setPen(QColor("#8B949E"));
    p.drawText(QRect(40, 80, width - 80, 30), Qt::AlignCenter, tierName + " Tier");

    // Pass/Fail badge
    int badgeW = 160, badgeH = 36;
    int badgeX = (width - badgeW) / 2, badgeY = 120;
    p.setPen(Qt::NoPen);
    p.setBrush(passColor);
    p.drawRoundedRect(badgeX, badgeY, badgeW, badgeH, 18, 18);

    QFont badgeFont("Segoe UI", 14, QFont::Bold);
    p.setFont(badgeFont);
    p.setPen(Qt::white);
    p.drawText(QRect(badgeX, badgeY, badgeW, badgeH), Qt::AlignCenter, passText);

    // System info
    int y = 180;
    QFont labelFont("Segoe UI", 10);
    QFont valueFont("Segoe UI", 10, QFont::Bold);

    auto sysInfo = QJsonDocument::fromJson(cert.system_info_json.toUtf8()).object();

    p.setFont(QFont("Segoe UI", 12, QFont::Bold));
    p.setPen(QColor("#F0F6FC"));
    p.drawText(40, y, "System Information");
    y += 6;
    p.setPen(QColor("#30363D"));
    p.drawLine(40, y, width - 40, y);
    y += 12;

    for (auto it = sysInfo.begin(); it != sysInfo.end(); ++it) {
        p.setFont(labelFont);
        p.setPen(QColor("#8B949E"));
        p.drawText(60, y, it.key() + ":");
        p.setFont(valueFont);
        p.setPen(QColor("#F0F6FC"));
        p.drawText(200, y, it.value().toVariant().toString());
        y += 18;
    }

    // Test results
    y += 10;
    p.setFont(QFont("Segoe UI", 12, QFont::Bold));
    p.setPen(QColor("#F0F6FC"));
    p.drawText(40, y, "Test Results");
    y += 6;
    p.setPen(QColor("#30363D"));
    p.drawLine(40, y, width - 40, y);
    y += 16;

    // Table header
    p.setFont(QFont("Segoe UI", 9));
    p.setPen(QColor("#8B949E"));
    p.drawText(60, y, "ENGINE");
    p.drawText(200, y, "MODE");
    p.drawText(360, y, "DURATION");
    p.drawText(500, y, "ERRORS");
    p.drawText(620, y, "STATUS");
    y += 16;

    for (const auto& r : cert.results) {
        p.setFont(valueFont);
        p.setPen(QColor("#C9D1D9"));
        p.drawText(60, y, r.engine);
        p.drawText(200, y, r.mode);
        int mins = static_cast<int>(r.duration_secs) / 60;
        p.drawText(360, y, QString::number(mins) + " min");
        p.drawText(500, y, QString::number(r.errors));
        p.setPen(r.passed ? QColor("#27AE60") : QColor("#C0392B"));
        p.drawText(620, y, r.passed ? "PASS" : "FAIL");
        y += 18;
    }

    // Timestamps
    y += 10;
    p.setFont(labelFont);
    p.setPen(QColor("#8B949E"));
    p.drawText(60, y, "Issued: " + cert.issued_at.toString("yyyy-MM-dd hh:mm:ss"));
    y += 16;
    p.drawText(60, y, "Expires: " + cert.expires_at.toString("yyyy-MM-dd hh:mm:ss"));

    // Hash at bottom
    p.setFont(QFont("Courier New", 8));
    p.setPen(QColor("#484F58"));
    p.drawText(QRect(40, height - 50, width - 80, 20), Qt::AlignCenter,
               "SHA-256: " + cert.hash_sha256);

    // Footer
    p.setFont(QFont("Segoe UI", 8));
    p.drawText(QRect(40, height - 35, width - 80, 16), Qt::AlignCenter,
               "Generated by OCCT Native Stress Test v1.0.0");

    p.end();
    return img;
}

} // namespace occt
