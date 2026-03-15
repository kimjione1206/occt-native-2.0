#include "sensor_model.h"
#include "sensor_manager.h"

#include <QMap>

namespace occt {

static QString group_name_for_unit(const std::string& unit) {
    if (unit == "C")   return QStringLiteral("Temperatures");
    if (unit == "V")   return QStringLiteral("Voltages");
    if (unit == "W")   return QStringLiteral("Power");
    if (unit == "RPM") return QStringLiteral("Fans");
    if (unit == "MHz") return QStringLiteral("Clocks");
    if (unit == "%")   return QStringLiteral("Load");
    return QStringLiteral("Other");
}

QVector<HardwareNode> build_hardware_tree(const QVector<SensorReading>& flat_readings) {
    // category -> HardwareNode
    QMap<QString, HardwareNode> node_map;

    for (const auto& r : flat_readings) {
        QString cat = QString::fromStdString(r.category);
        if (!node_map.contains(cat)) {
            HardwareNode node;
            node.id   = cat.toLower().replace(' ', '_');
            node.name = cat;
            node.type = cat;
            node_map[cat] = node;
        }

        HardwareNode& node = node_map[cat];
        QString gname = group_name_for_unit(r.unit);

        // Find or create the group
        SensorGroup* grp = nullptr;
        for (auto& g : node.groups) {
            if (g.name == gname) { grp = &g; break; }
        }
        if (!grp) {
            node.groups.append(SensorGroup{gname, {}});
            grp = &node.groups.last();
        }
        grp->readings.append(r);
    }

    QVector<HardwareNode> result;
    // Desired order: CPU, GPU, Motherboard, Storage, then any others
    static const QStringList order = {"CPU", "GPU", "Motherboard", "Storage"};
    for (const auto& key : order) {
        if (node_map.contains(key)) {
            result.append(node_map.take(key));
        }
    }
    // Remaining categories
    for (auto it = node_map.begin(); it != node_map.end(); ++it) {
        result.append(it.value());
    }
    return result;
}

QVector<SensorReading> flatten_hardware_tree(const QVector<HardwareNode>& nodes) {
    QVector<SensorReading> result;
    for (const auto& node : nodes) {
        for (const auto& grp : node.groups) {
            for (const auto& r : grp.readings) {
                result.append(r);
            }
        }
    }
    return result;
}

} // namespace occt
