// SPDX-FileCopyrightText: 2026 PollyMC Continued Contributors
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

/**
 * \brief Everything that decides where the launcher keeps its data.
 *
 * The inputs are passed in rather than read from the command line and the
 * environment inside the resolver, so that the launcher and the updater cannot
 * drift apart. They used to carry two separate copies of this chain, and the
 * updater's copy disagreed with the launcher's on the --dir and
 * $POLLYMC_DATA_DIR cases.
 */
struct DataRootInputs {
    /// Root of the installation: the directory holding bin/, or /usr.
    QString appRootPath;
    /// --dir, empty when not given.
    QString dirParam;
    /// $POLLYMC_DATA_DIR, empty when unset.
    QString dataDirEnv;
    /// Parent of QStandardPaths::AppDataLocation, i.e. ~/.local/share/PollyMC.
    /// The caller resolves this because Qt needs the application and organization
    /// names to be set first.
    QString appDataLocationParent;
};

struct DataRootChoice {
    /// The resolved data directory. Absolute, except when --dir is a relative path.
    QString path;
    /// Human readable reason for the choice, logged as "Adjusted by".
    QString adjustedBy;
    /// True when the app root looks like a portable install.
    ///
    /// Deliberately independent of \a path: this picks which asset the updater
    /// downloads, so a portable install that keeps its data somewhere else is
    /// still a portable install.
    bool portableInstall = false;

    /// True when the data root was recovered from the app data location because
    /// the portable install turned out to hold no instances of its own.
    bool recovered = false;
};

/**
 * \brief Name of the file in the app root that pins the data directory.
 * Its content is the absolute path to use. Empty or non-absolute content is ignored.
 */
QString dataRootPointerFile();

/**
 * \brief Name of the file in the app root that permanently disables the recovery
 * described above. Create it to keep a portable install using its own folder.
 */
QString dataRootOptOutFile();

/**
 * \brief Resolve the data directory, in precedence order:
 *   --dir  >  $POLLYMC_DATA_DIR  >  dataroot.txt  >  UserData/  >  portable.txt  >  app data
 * Under a Snap, the app data path is $SNAP_USER_COMMON instead.
 *
 * The last step is a recovery: a portable install with no instances of its own
 * falls back to the app data location when that one does have instances, since
 * that is where a previous non-portable run left the user's data. This happens
 * without asking, because the settings and instances found there are already
 * ours - nothing is copied, the folder is simply used where it lies.
 *
 * On macOS the UserData/ and portable.txt steps do not apply.
 */
DataRootChoice resolveDataRoot(const DataRootInputs& in);
