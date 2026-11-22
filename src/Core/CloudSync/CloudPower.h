#pragma once

#include <QString>
#include <functional>

class QObject;

namespace CloudPower {
bool supported();
void readBootId(QObject* owner, std::function<void(const QString&)> done);
bool request(bool restart, QString* error);
}
