#pragma once

#include <QString>
#include <QVector>

namespace occt {

struct SensorReading;  // forward decl from sensor_manager.h

/// A group of sensors under one hardware component (e.g. "Temperatures", "Voltages").
struct SensorGroup {
    QString name;  // e.g. "Temperatures", "Voltages", "Clocks", "Fans", "Power"
    QVector<SensorReading> readings;
};

/// A single hardware component with grouped sensor readings.
struct HardwareNode {
    QString id;     // unique identifier (e.g. "cpu0", "gpu0", "mb0")
    QString name;   // human-readable name (e.g. "Intel Core i9-14900K")
    QString type;   // "CPU", "GPU", "Motherboard", "Storage"
    QVector<SensorGroup> groups;
};

/// Build a hierarchical tree of HardwareNode from a flat list of SensorReading.
/// Groups readings by category (type) and then by unit-based sensor group.
QVector<HardwareNode> build_hardware_tree(const QVector<SensorReading>& flat_readings);

/// Flatten a hardware tree back to a simple reading list (for backward compat).
QVector<SensorReading> flatten_hardware_tree(const QVector<HardwareNode>& nodes);

} // namespace occt
