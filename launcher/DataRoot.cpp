// SPDX-FileCopyrightText: 2026 PollyMC Continued Contributors
//
// SPDX-License-Identifier: GPL-3.0-only

#include "DataRoot.h"

#include "BuildConfig.h"
#include "DesktopServices.h"
#include "FileSystem.h"

#include <QDebug>
#include <QDir>
#include <QFile>

QString dataRootPointerFile()
{
    return QStringLiteral("dataroot.txt");
}

QString dataRootOptOutFile()
{
    return BuildConfig.LAUNCHER_APP_BINARY_NAME + "_dataroot_nomigrate.txt";
}

namespace {
bool hasInstances(const QString& dataRoot)
{
    return !QDir(FS::PathCombine(dataRoot, "instances")).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();
}

/**
 * \brief Read the data directory pinned in the app root, or an empty string when
 * there is none or it does not name an absolute path.
 */
QString readPointer(const QString& appRootPath)
{
    QFile file(FS::PathCombine(appRootPath, dataRootPointerFile()));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    const QString target = QString::fromUtf8(file.readAll()).trimmed();
    if (target.isEmpty() || !QDir::isAbsolutePath(target)) {
        qWarning() << "Ignoring" << file.fileName() << "- it does not hold an absolute path";
        return {};
    }
    return QDir(target).absolutePath();
}
}  // namespace

DataRootChoice resolveDataRoot(const DataRootInputs& in)
{
    DataRootChoice choice;

    // Whether the *install* is portable is a property of the app root, not of
    // where the data ends up. It is computed up front so that an explicit --dir
    // or $POLLYMC_DATA_DIR cannot change which updater asset gets downloaded.
#ifndef Q_OS_MACOS
    choice.portableInstall = QDir(FS::PathCombine(in.appRootPath, "UserData")).exists() ||
                             QFile::exists(FS::PathCombine(in.appRootPath, "portable.txt"));
#endif

    if (!in.dirParam.isEmpty()) {
        choice.path = in.dirParam;
        choice.adjustedBy = "Command line";
        return choice;
    }

    if (!in.dataDirEnv.isEmpty()) {
        choice.path = in.dataDirEnv;
        choice.adjustedBy = "System environment";
        return choice;
    }

    // An explicit pointer outranks the portable markers, since it is a decision
    // the user made about this specific install.
    if (const QString pointed = readPointer(in.appRootPath); !pointed.isEmpty()) {
        choice.path = pointed;
        choice.adjustedBy = "Data root pointer";
        return choice;
    }

    QDir base;
    if (DesktopServices::isSnap()) {
        base = QDir(qEnvironmentVariable("SNAP_USER_COMMON"));
    } else {
        base = QDir(in.appDataLocationParent);
    }
    choice.path = base.absolutePath();
    choice.adjustedBy = "Persistent data path";

    // Only portable.txt can strand data: UserData/ is part of the app root's own
    // layout, and anything else resolving here is what the user explicitly asked for.
    bool resolvedToAppRoot = false;
#ifndef Q_OS_MACOS
    if (auto portableUserData = FS::PathCombine(in.appRootPath, "UserData"); QDir(portableUserData).exists()) {
        choice.path = portableUserData;
        choice.adjustedBy = "Portable user data path";
    } else if (QFile::exists(FS::PathCombine(in.appRootPath, "portable.txt"))) {
        choice.path = in.appRootPath;
        choice.adjustedBy = "Portable data path";
        resolvedToAppRoot = true;
    }
#endif

    // The install is portable but holds nothing of its own, while the app data
    // location does hold instances: a previous non-portable run left the user's
    // data there, and this install is where they are now running the launcher
    // from. Use that folder where it lies instead of starting out empty. Nothing
    // is copied, so every setting comes across exactly as it was.
    if (resolvedToAppRoot && !hasInstances(in.appRootPath) && hasInstances(in.appDataLocationParent) &&
        !QFile::exists(FS::PathCombine(in.appRootPath, dataRootOptOutFile()))) {
        choice.path = in.appDataLocationParent;
        choice.adjustedBy = "Recovered data root";
        choice.recovered = true;
    }

    return choice;
}
