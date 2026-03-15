#include "html_report.h"

#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

namespace occt {

// Escape a string for safe insertion into HTML content/attributes.
static QString escapeHtml(const QString& s)
{
    QString out;
    out.reserve(s.size());
    for (QChar c : s) {
        switch (c.unicode()) {
        case '&':  out += "&amp;";  break;
        case '<':  out += "&lt;";   break;
        case '>':  out += "&gt;";   break;
        case '"':  out += "&quot;"; break;
        case '\'': out += "&#39;";  break;
        default:   out += c;        break;
        }
    }
    return out;
}

bool HtmlReport::save(const TestResults& results, const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    out << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
        << "<meta charset=\"utf-8\">\n"
        << "<title>OCCT Native Stress Test Report</title>\n"
        << "<style>\n"
        << "* { margin: 0; padding: 0; box-sizing: border-box; }\n"
        << "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; "
        << "background: #0D1117; color: #C9D1D9; padding: 0; }\n"
        << ".header { background: #C0392B; padding: 24px 40px; }\n"
        << ".header h1 { color: white; font-size: 24px; }\n"
        << ".header .sub { color: rgba(255,255,255,0.7); font-size: 13px; margin-top: 4px; }\n"
        << ".container { max-width: 1100px; margin: 0 auto; padding: 30px 40px; }\n"
        << "h2 { color: #F0F6FC; font-size: 18px; margin: 28px 0 12px; border-bottom: 2px solid #C0392B; padding-bottom: 6px; }\n"
        << ".info-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }\n"
        << ".info-item { background: #161B22; padding: 12px 16px; border-radius: 8px; border: 1px solid #30363D; }\n"
        << ".info-item .label { color: #8B949E; font-size: 12px; }\n"
        << ".info-item .value { color: #F0F6FC; font-size: 14px; margin-top: 2px; }\n"
        << "table { width: 100%; border-collapse: collapse; margin-top: 12px; }\n"
        << "thead th { background: #161B22; color: #F0F6FC; padding: 12px; text-align: left; "
        << "border-bottom: 2px solid #C0392B; font-size: 13px; }\n"
        << "tbody td { padding: 10px 12px; border-bottom: 1px solid #30363D; font-size: 13px; }\n"
        << "tbody tr:nth-child(even) { background: #161B22; }\n"
        << "tbody tr:hover { background: #1F2937; }\n"
        << ".pass { color: #2ECC71; font-weight: bold; }\n"
        << ".fail { color: #E74C3C; font-weight: bold; }\n"
        << ".warn { color: #F1C40F; font-weight: bold; }\n"
        << ".verdict { text-align: center; padding: 20px; margin-top: 24px; border-radius: 8px; font-size: 20px; font-weight: bold; }\n"
        << ".verdict.pass-bg { background: rgba(46,204,113,0.15); border: 2px solid #2ECC71; color: #2ECC71; }\n"
        << ".verdict.fail-bg { background: rgba(231,76,60,0.15); border: 2px solid #E74C3C; color: #E74C3C; }\n"
        << ".chart-container { background: #161B22; border: 1px solid #30363D; border-radius: 8px; padding: 20px; margin-top: 12px; }\n"
        << "canvas { max-width: 100%; }\n"
        << ".footer { text-align: center; color: #8B949E; font-size: 12px; margin-top: 40px; padding: 20px; border-top: 1px solid #30363D; }\n"
        << "</style>\n"
        << "</head>\n<body>\n";

    // Header
    QString genTime = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    out << "<div class=\"header\">\n"
        << "  <h1>OCCT Native Stress Test Report</h1>\n"
        << "  <div class=\"sub\">Generated: " << genTime << "</div>\n"
        << "</div>\n";

    out << "<div class=\"container\">\n";

    // System Information
    const auto& si = results.system_info;
    out << "<h2>System Information</h2>\n"
        << "<div class=\"info-grid\">\n"
        << "  <div class=\"info-item\"><div class=\"label\">CPU</div><div class=\"value\">"
        << escapeHtml(si.cpu_name) << " (" << si.cpu_cores << "C/" << si.cpu_threads << "T)</div></div>\n"
        << "  <div class=\"info-item\"><div class=\"label\">GPU</div><div class=\"value\">"
        << escapeHtml(si.gpu_name) << "</div></div>\n"
        << "  <div class=\"info-item\"><div class=\"label\">RAM</div><div class=\"value\">"
        << escapeHtml(si.ram_total) << "</div></div>\n"
        << "  <div class=\"info-item\"><div class=\"label\">OS</div><div class=\"value\">"
        << escapeHtml(si.os_name) << "</div></div>\n"
        << "</div>\n";

    // Test Results
    out << "<h2>Test Results</h2>\n"
        << "<table>\n<thead><tr>"
        << "<th>Timestamp</th><th>Test</th><th>Mode</th><th>Duration</th>"
        << "<th>Errors</th><th>Result</th></tr></thead>\n<tbody>\n";

    int pass_count = 0, fail_count = 0;
    for (const auto& r : results.results) {
        out << "<tr><td>" << escapeHtml(r.timestamp) << "</td>"
            << "<td>" << escapeHtml(r.test_type) << "</td>"
            << "<td>" << escapeHtml(r.mode) << "</td>"
            << "<td>" << escapeHtml(r.duration) << "</td>"
            << "<td>" << r.error_count << "</td>"
            << "<td class=\"" << (r.passed ? "pass" : "fail") << "\">"
            << (r.passed ? "PASS" : "FAIL") << "</td></tr>\n";
        if (r.passed) pass_count++; else fail_count++;
    }
    out << "</tbody></table>\n";

    // Sensor Data Chart (inline JavaScript with simple SVG polyline)
    if (!results.sensor_series.isEmpty()) {
        out << "<h2>Sensor Data</h2>\n"
            << "<div class=\"chart-container\">\n"
            << "<canvas id=\"sensorChart\" width=\"1000\" height=\"300\"></canvas>\n"
            << "</div>\n";

        // Embed sensor data as JS array using JSON serialization for safety
        QJsonArray sensorArray;
        for (int i = 0; i < results.sensor_series.size(); ++i) {
            const auto& dp = results.sensor_series[i];
            QJsonObject obj;
            obj["t"] = dp.timestamp_sec;
            obj["v"] = dp.value;
            obj["n"] = dp.sensor_name;
            sensorArray.append(obj);
        }
        QJsonDocument sensorDoc(sensorArray);
        out << "<script>\n"
            << "var sensorData = " << sensorDoc.toJson(QJsonDocument::Compact) << ";\n";

        // Simple canvas chart renderer
        out << R"(
(function() {
    var canvas = document.getElementById('sensorChart');
    if (!canvas) return;
    var ctx = canvas.getContext('2d');
    var W = canvas.width, H = canvas.height;
    var pad = {t:20,r:20,b:40,l:60};

    if (sensorData.length === 0) return;

    var minV = Infinity, maxV = -Infinity, maxT = 0;
    sensorData.forEach(function(d) {
        if (d.v < minV) minV = d.v;
        if (d.v > maxV) maxV = d.v;
        if (d.t > maxT) maxT = d.t;
    });
    var range = maxV - minV || 10;
    minV -= range * 0.05; maxV += range * 0.05;
    range = maxV - minV;
    if (maxT <= 0) maxT = 1;

    // Background
    ctx.fillStyle = '#161B22';
    ctx.fillRect(0, 0, W, H);

    // Grid
    ctx.strokeStyle = '#30363D';
    ctx.lineWidth = 0.5;
    for (var i = 0; i <= 5; i++) {
        var gy = pad.t + (H - pad.t - pad.b) * i / 5;
        ctx.beginPath(); ctx.moveTo(pad.l, gy); ctx.lineTo(W - pad.r, gy); ctx.stroke();
        ctx.fillStyle = '#8B949E'; ctx.font = '10px Arial';
        ctx.fillText((maxV - range * i / 5).toFixed(1), 5, gy + 4);
    }

    // Data line
    ctx.strokeStyle = '#3498DB';
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    sensorData.forEach(function(d, idx) {
        var x = pad.l + (d.t / maxT) * (W - pad.l - pad.r);
        var y = pad.t + (1 - (d.v - minV) / range) * (H - pad.t - pad.b);
        if (idx === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    });
    ctx.stroke();

    // X-axis labels
    ctx.fillStyle = '#8B949E'; ctx.font = '10px Arial';
    ctx.fillText('0s', pad.l, H - 10);
    ctx.fillText(maxT.toFixed(0) + 's', W - pad.r - 30, H - 10);
})();
)";
        out << "</script>\n";
    }

    // Overall verdict
    bool overall_pass = (fail_count == 0 && pass_count > 0);
    out << "<div class=\"verdict " << (overall_pass ? "pass-bg" : "fail-bg") << "\">"
        << "Overall: " << (overall_pass ? "PASS" : "FAIL")
        << " (" << pass_count << " passed, " << fail_count << " failed)"
        << "</div>\n";

    // Footer
    out << "<div class=\"footer\">"
        << "OCCT Native Stress Test v1.0.0 | Report generated on " << genTime
        << "</div>\n";

    out << "</div>\n</body>\n</html>\n";

    file.close();
    return true;
}

} // namespace occt
