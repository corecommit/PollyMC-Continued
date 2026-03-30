// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>
#include "Application.h"
#include "HardwareInfo.h"

class PerformancePresetsDialog : public QDialog {
    Q_OBJECT

   public:
    explicit PerformancePresetsDialog(QWidget* parent = nullptr) : QDialog(parent)
    {
        setWindowTitle(tr("Performance Presets"));
        setMinimumWidth(450);

        auto layout = new QVBoxLayout(this);

        // Detect system RAM
        uint64_t totalRamMB = HardwareInfo::totalRamMiB();
        int recommended = 3072;  // default medium

        if (totalRamMB <= 4096) {
            recommended = 1536;  // Low
        } else if (totalRamMB <= 8192) {
            recommended = 3072;  // Medium
        } else {
            recommended = 6144;  // High
        }

        auto detectLabel = new QLabel(
            tr("Detected system RAM: %1 GB").arg(QString::number(totalRamMB / 1024.0, 'f', 1)), this);
        detectLabel->setStyleSheet("font-weight: bold; color: #4CAF50;");
        layout->addWidget(detectLabel);

        auto label = new QLabel(tr("Select a performance preset:"), this);
        layout->addWidget(label);

        m_combo = new QComboBox(this);
        m_combo->addItem(tr("Low (4GB RAM or less) - 1536MB"), 1536);
        m_combo->addItem(tr("Medium (8GB RAM) - 3072MB"), 3072);
        m_combo->addItem(tr("High (16GB+ RAM) - 6144MB"), 6144);

        // Auto-select recommended
        for (int i = 0; i < m_combo->count(); i++) {
            if (m_combo->itemData(i).toInt() == recommended) {
                m_combo->setCurrentIndex(i);
                break;
            }
        }
        layout->addWidget(m_combo);

        auto desc = new QLabel(this);
        desc->setWordWrap(true);
        desc->setText(tr("This sets the default memory allocation for all instances. "
                          "You can still override per-instance in instance settings."));
        layout->addWidget(desc);

        auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        layout->addWidget(buttons);

        connect(buttons, &QDialogButtonBox::accepted, this, &PerformancePresetsDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &PerformancePresetsDialog::reject);
    }

    void accept() override
    {
        int mem = m_combo->currentData().toInt();
        auto settings = APPLICATION->settings();
        settings->set("MinMemAlloc", mem / 2);
        settings->set("MaxMemAlloc", mem);
        settings->set("PermGen", 128);
        QDialog::accept();
    }

   private:
    QComboBox* m_combo;
};
