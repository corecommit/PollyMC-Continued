// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PollyMC-Continued - Minecraft Launcher
 *  Copyright (c) 2026 PollyMC-Continued Contributors
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

#pragma once

#include <memory>

#include <QDialog>
#include <QHash>
#include <QImage>
#include <QList>
#include <QPixmap>

#include "minecraft/skins/CraftySkinAPI.h"
#include "minecraft/skins/SkinModel.h"
#include "ui/dialogs/skins/draw/SkinOpenGLWindow.h"

class QLabel;
class QListWidgetItem;

namespace Ui {
class SkinBrowserDialog;
}

// browses the crafty.gg catalog and hands the picked skin back to the skin manager
class SkinBrowserDialog : public QDialog, public SkinProvider {
    Q_OBJECT

   public:
    // the skin the user confirmed, applied by the caller
    struct Choice {
        QString texturePath;
        QString playerName;
        QString hash;
        SkinModel::Model model = SkinModel::CLASSIC;
    };

    explicit SkinBrowserDialog(QWidget* parent);
    ~SkinBrowserDialog() override;

    const Choice& choice() const { return m_choice; }

    SkinModel* getSelectedSkin() override { return m_previewModel.get(); }
    QHash<QString, QImage> capes() override { return {}; }

   protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void accept() override;
    void reject() override;

   private slots:
    void on_searchBtn_clicked();
    void on_searchEdit_returnPressed();
    void on_prevBtn_clicked();
    void on_nextBtn_clicked();
    void on_siteBtn_clicked();
    void on_useSkinBtn_clicked();
    void on_skinList_currentRowChanged(int row);

   private:
    void loadPage(int page);
    void fillList();
    void updateDetails();
    void showError(const QString& reason);
    QPixmap iconPixmap(const Crafty::SkinPtr& skin) const;
    // name and icon for one row, re-run when the texture (and with it the player name) arrives
    void decorateItem(QListWidgetItem* item, const Crafty::SkinPtr& skin) const;

   private:
    Ui::SkinBrowserDialog* m_ui;
    Crafty::API* m_api;
    QList<Crafty::SkinPtr> m_skins;
    Crafty::SkinPtr m_current;
    std::unique_ptr<SkinModel> m_previewModel;
    SkinOpenGLWindow* m_skinPreview = nullptr;
    QLabel* m_skinPreviewLabel = nullptr;
    QString m_query;
    int m_page = 1;
    int m_request = 0;
    bool m_hasMore = false;
    Choice m_choice;
};
