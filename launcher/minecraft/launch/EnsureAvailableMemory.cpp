// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PollyMC-Continued - Minecraft Launcher
 *  Copyright (C) 2026 Octol1ttle <l1ttleofficial@outlook.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "EnsureAvailableMemory.h"

#include "HardwareInfo.h"
#include "ui/dialogs/CustomMessageBox.h"

EnsureAvailableMemory::EnsureAvailableMemory(LaunchTask* parent, MinecraftInstance* instance) : LaunchStep(parent), m_instance(instance) {}

void EnsureAvailableMemory::executeTask()
{
    const uint64_t total = HardwareInfo::totalRamMiB();
    const uint64_t available = HardwareInfo::availableRamMiB();
    const uint64_t min = m_instance->settings()->get("MinMemAlloc").toUInt();
    const uint64_t max = m_instance->settings()->get("MaxMemAlloc").toUInt();
    const uint64_t required = std::max(min, max);

    // Reserve a safety margin for the OS and other running software rather
    // than requiring the full amount to be free right this instant — total
    // installed RAM is what determines whether an allocation is fundamentally
    // viable on this hardware, not a momentary snapshot of free memory.
    constexpr uint64_t safetyMarginMiB = 1024;
    const uint64_t usableTotal = (total > safetyMarginMiB) ? (total - safetyMarginMiB) : 0;

    if (total > 0 && required > usableTotal) {
        // Hard incompatibility: the system physically cannot back this
        // allocation no matter what else is running. Keep this as a blocking
        // warning (existing Yes/No dialog), but base the message on total RAM.
        auto* dialog = CustomMessageBox::selectable(
            nullptr, tr("Not enough RAM"),
            tr("This instance is configured to use more memory than your system has installed.\n\n"
               "Required: %1 MiB\nTotal system RAM: %2 MiB\n\n"
               "Continue anyway? This may cause severe slowdowns or crashes.")
                .arg(required)
                .arg(total),
            QMessageBox::Icon::Warning, QMessageBox::StandardButton::Yes | QMessageBox::StandardButton::No,
            QMessageBox::StandardButton::No);
        const auto response = dialog->exec();
        dialog->deleteLater();

        const auto message = tr("Not enough RAM available to launch this instance");
        if (response == QMessageBox::No) {
            emit logLine(message, MessageLevel::Fatal);
            emitFailed(message);
            return;
        }

        emit logLine(message, MessageLevel::Warning);
    } else if (required > available) {
        // Soft warning: fundamentally viable on this hardware, but not enough
        // is free right now due to other running programs. Log only, don't
        // block launch — this is not the instance's fault.
        emit logLine(tr("Note: only %1 MiB RAM is currently free, but this instance requests %2 MiB. "
                         "Other running programs may cause slowdowns; consider closing some before playing.")
                         .arg(available)
                         .arg(required),
                     MessageLevel::Warning);
    }

    emitSucceeded();
}
