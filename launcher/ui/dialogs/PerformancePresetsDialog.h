// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include "Application.h"
#include "HardwareInfo.h"

class PerformancePresetsDialog : public QDialog {
    Q_OBJECT

   public:
    explicit PerformancePresetsDialog(QWidget* parent = nullptr) : QDialog(parent)
    {
        setWindowTitle(tr("Performance Presets"));
        setMinimumWidth(500);

        auto layout = new QVBoxLayout(this);

        // Detect system RAM
        uint64_t totalRamMB = HardwareInfo::totalRamMiB();
        int recommended = 3072;

        if (totalRamMB <= 4096) {
            recommended = 1536;
        } else if (totalRamMB <= 8192) {
            recommended = 3072;
        } else {
            recommended = 6144;
        }

        // System info
        auto infoGroup = new QGroupBox(tr("System Info"), this);
        auto infoLayout = new QVBoxLayout(infoGroup);

        auto ramLabel = new QLabel(
            tr("Detected RAM: %1 GB").arg(QString::number(totalRamMB / 1024.0, 'f', 1)), this);
        ramLabel->setStyleSheet("font-weight: bold; color: #4CAF50; font-size: 14px;");
        infoLayout->addWidget(ramLabel);

        auto recLabel = new QLabel(
            tr("Recommended preset: %1").arg(recommended <= 1536 ? "Low" : recommended <= 3072 ? "Medium" : "High"), this);
        recLabel->setStyleSheet("color: #888888;");
        infoLayout->addWidget(recLabel);
        layout->addWidget(infoGroup);

        // Preset selection
        auto presetGroup = new QGroupBox(tr("Select Preset"), this);
        auto presetLayout = new QVBoxLayout(presetGroup);

        m_combo = new QComboBox(this);
        m_combo->addItem(tr("Low (4GB RAM or less)"), 1536);
        m_combo->addItem(tr("Medium (8GB RAM)"), 3072);
        m_combo->addItem(tr("High (16GB+ RAM)"), 6144);

        // Auto-select recommended
        for (int i = 0; i < m_combo->count(); i++) {
            if (m_combo->itemData(i).toInt() == recommended) {
                m_combo->setCurrentIndex(i);
                break;
            }
        }
        presetLayout->addWidget(m_combo);

        // Preset details
        m_details = new QTextEdit(this);
        m_details->setReadOnly(true);
        m_details->setMaximumHeight(120);
        m_details->setStyleSheet("background-color: transparent; border: none;");
        updateDetails(recommended);
        presetLayout->addWidget(m_details);
        layout->addWidget(presetGroup);

        // Help section
        auto helpGroup = new QGroupBox(tr("How it works"), this);
        auto helpLayout = new QVBoxLayout(helpGroup);

        auto helpText = new QLabel(this);
        helpText->setWordWrap(true);
        helpText->setText(
            tr("Performance presets adjust how much RAM Minecraft can use.\n\n"
               "- Low: Best for laptops with 4GB RAM. Less memory = fewer chunks loaded.\n"
               "- Medium: Good for most PCs with 8GB RAM.\n"
               "- High: For gaming PCs with 16GB+ RAM. More memory = more chunks and mods.\n\n"
               "These are global defaults. You can override per-instance in instance settings.\n"
               "Minecraft will use this as -Xms (min) and -Xmx (max) JVM arguments."));
        helpText->setStyleSheet("color: #aaaaaa; font-size: 11px;");
        helpLayout->addWidget(helpText);
        layout->addWidget(helpGroup);

        // Buttons
        auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        layout->addWidget(buttons);

        connect(buttons, &QDialogButtonBox::accepted, this, &PerformancePresetsDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &PerformancePresetsDialog::reject);
        connect(m_combo, &QComboBox::currentIndexChanged, this, [this]() {
            updateDetails(m_combo->currentData().toInt());
        });
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
    void updateDetails(int mem)
    {
        int minMem = mem / 2;
        QString details;

        if (mem == 1536) {
            details = tr("Low Preset\n"
                         "-Xms%1m -Xmx%2m\n\n"
                         "Best for: Laptops, 4GB RAM PCs\n"
                         "Recommended for: Vanilla Minecraft, small modpacks\n"
                         "Warning: Large modpacks may crash with out-of-memory")
                          .arg(minMem)
                          .arg(mem);
        } else if (mem == 3072) {
            details = tr("Medium Preset\n"
                         "-Xms%1m -Xmx%2m\n\n"
                         "Best for: Desktop PCs, 8GB RAM\n"
                         "Recommended for: Medium modpacks, shaders\n"
                         "Good balance of performance and stability")
                          .arg(minMem)
                          .arg(mem);
        } else {
            details = tr("High Preset\n"
                         "-Xms%1m -Xmx%2m\n\n"
                         "Best for: Gaming PCs, 16GB+ RAM\n"
                         "Recommended for: Large modpacks, heavy shaders\n"
                         "Maximum performance for demanding setups")
                          .arg(minMem)
                          .arg(mem);
        }

        m_details->setPlainText(details);
    }

    QComboBox* m_combo;
    QTextEdit* m_details;
};
