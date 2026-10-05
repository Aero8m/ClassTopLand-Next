#pragma once

#include "../Model/Profile.h"
#include <QByteArray>
#include <QStringList>

namespace ProfileExchange {
enum class Format { NativeJson, CsesYaml };

// Failure leaves the caller's result unchanged. Native JSON accepts legacy files;
// exchange callers additionally apply validateTransfer before import/export.
bool decode(const QByteArray& contents, Format format, Profile& result,
            QStringList* warnings = nullptr, QString* error = nullptr);
bool readFile(const QString& path, Format format, Profile& result,
              QStringList* warnings = nullptr, QString* error = nullptr);
bool serialize(const Profile& profile, Format format, QByteArray& contents,
               QStringList* warnings = nullptr, QString* error = nullptr);
bool writeFile(const QString& path, const QByteArray& contents, QString* error = nullptr);
// Stricter exchange validation, without changing legacy on-disk JSON acceptance.
bool validateTransfer(const Profile& profile, QString* error = nullptr);
}
