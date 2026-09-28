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

#include "SkinBrowserDialog.h"
#include "ui_SkinBrowserDialog.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QPixmap>
#include <QShowEvent>

#include "Application.h"
#include "DiscordRichPresence.h"
#include "settings/SettingsObject.h"
#include "ui/dialogs/CustomMessageBox.h"

static const QLatin1String geometryKey("SkinBrowserGeometry");

SkinBrowserDialog::SkinBrowserDialog(QWidget* parent)
    : QDialog(parent), m_ui(new Ui::SkinBrowserDialog), m_api(new Crafty::API(this))
{
    m_ui->setupUi(this);
    setWindowModality(Qt::WindowModal);

    auto geometry = APPLICATION->settings()->get(geometryKey).toString();
    if (geometry.isEmpty()) {
        const auto base = parent ? parent->size() : QSize(1024, 768);
        resize(qMax(qRound(base.width() * 0.6), 700), qMax(qRound(base.height() * 0.75), 480));
    } else {
        restoreGeometry(QByteArray::fromBase64(geometry.toUtf8()));
    }

    if (SkinOpenGLWindow::hasOpenGL()) {
        m_skinPreview = new SkinOpenGLWindow(this, palette().color(QPalette::Normal, QPalette::Base));
        m_ui->previewLayout->addWidget(QWidget::createWindowContainer(m_skinPreview, this));
    } else {
        m_skinPreviewLabel = new QLabel(this);
        m_skinPreviewLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_skinPreviewLabel->setAlignment(Qt::AlignCenter);
        m_ui->previewLayout->addWidget(m_skinPreviewLabel);
    }

    m_ui->splitter->setStretchFactor(0, 1);
    m_ui->splitter->setStretchFactor(1, 2);
    m_ui->splitter->setSizes({ 380, 520 });

    connect(m_ui->skinList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { on_useSkinBtn_clicked(); });

    loadPage(1);
}

SkinBrowserDialog::~SkinBrowserDialog()
{
    // invalidates every callback that is still in flight
    m_request++;
    m_api->abortAll();
    delete m_ui;
}

void SkinBrowserDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    DiscordRichPresence::instance()->updateBrowsing(tr("Looking for skins"));
}

void SkinBrowserDialog::hideEvent(QHideEvent* event)
{
    QDialog::hideEvent(event);
    DiscordRichPresence::instance()->browsingClosed();
}

void SkinBrowserDialog::accept()
{
    APPLICATION->settings()->set(geometryKey, QString::fromUtf8(saveGeometry().toBase64()));
    QDialog::accept();
}

void SkinBrowserDialog::reject()
{
    APPLICATION->settings()->set(geometryKey, QString::fromUtf8(saveGeometry().toBase64()));
    QDialog::reject();
}

void SkinBrowserDialog::showError(const QString& reason)
{
    CustomMessageBox::selectable(this, tr("Skin catalog"), reason, QMessageBox::Warning)->exec();
}

void SkinBrowserDialog::on_searchBtn_clicked()
{
    m_query = m_ui->searchEdit->text().trimmed();
    loadPage(1);
}

void SkinBrowserDialog::on_searchEdit_returnPressed()
{
    on_searchBtn_clicked();
}

void SkinBrowserDialog::on_prevBtn_clicked()
{
    if (m_page > 1)
        loadPage(m_page - 1);
}

void SkinBrowserDialog::on_nextBtn_clicked()
{
    if (m_hasMore)
        loadPage(m_page + 1);
}

void SkinBrowserDialog::on_siteBtn_clicked()
{
    if (m_current)
        QDesktopServices::openUrl(QUrl(Crafty::API::pageUrl(m_current->hash)));
}

void SkinBrowserDialog::on_useSkinBtn_clicked()
{
    if (!m_current || m_current->texture.isNull())
        return;
    if (!QFileInfo::exists(m_current->texturePath) && !Crafty::API::cacheTexture(m_current)) {
        showError(tr("The skin image could not be stored on disk."));
        return;
    }
    m_choice.texturePath = m_current->texturePath;
    m_choice.playerName = m_current->username;
    m_choice.hash = m_current->hash;
    m_choice.model = m_current->slim ? SkinModel::SLIM : SkinModel::CLASSIC;
    accept();
}

void SkinBrowserDialog::loadPage(int page)
{
    const int request = ++m_request;
    m_ui->pageLabel->setText(tr("Loading…"));
    m_ui->prevBtn->setEnabled(false);
    m_ui->nextBtn->setEnabled(false);
    m_ui->useSkinBtn->setEnabled(false);

    m_api->fetchSkins(page, m_query, [this, request, page](const Crafty::SkinPage& result, const QString& error) {
        if (request != m_request)
            return;
        if (!error.isEmpty()) {
            m_ui->pageLabel->setText(tr("Page %1").arg(page));
            m_ui->prevBtn->setEnabled(m_page > 1);
            m_ui->nextBtn->setEnabled(m_hasMore);
            updateDetails();
            showError(error);
            return;
        }
        m_ui->pageLabel->setText(tr("Page %1").arg(result.page));
        m_page = result.page;
        m_hasMore = result.hasMore;
        m_ui->prevBtn->setEnabled(m_page > 1);
        m_ui->nextBtn->setEnabled(m_hasMore);
        m_skins = result.skins;
        fillList();
        if (m_skins.isEmpty())
            m_ui->playerLabel->setText(m_query.isEmpty() ? tr("No skins found") : tr("No skins found for %1").arg(m_query));

        m_api->fetchTextures(
            m_skins,
            [this, request](const Crafty::SkinPtr& skin) {
                if (request != m_request)
                    return;
                auto row = m_skins.indexOf(skin);
                if (row < 0)
                    return;
                if (auto item = m_ui->skinList->item(row))
                    decorateItem(item, skin);
                if (row == m_ui->skinList->currentRow())
                    updateDetails();
            },
            [this, request](const QString& textureError) {
                if (request != m_request || textureError.isEmpty())
                    return;
                qWarning() << "Skin catalog: could not download every preview:" << textureError;
            });
    });
}

QPixmap SkinBrowserDialog::iconPixmap(const Crafty::SkinPtr& skin) const
{
    if (!skin->preview.isNull())
        return QPixmap::fromImage(skin->preview).scaled(72, 72, Qt::KeepAspectRatio, Qt::FastTransformation);
    // a plain mannequin, so every row looks filled while its texture is still downloading
    QPixmap placeholder(72, 72);
    placeholder.fill(Qt::transparent);
    QPainter painter(&placeholder);
    painter.setPen(Qt::NoPen);
    painter.setBrush(palette().color(QPalette::Mid));
    painter.drawRect(24, 6, 24, 24);
    painter.drawRect(28, 34, 16, 22);
    painter.drawRect(19, 34, 8, 20);
    painter.drawRect(45, 34, 8, 20);
    painter.drawRect(28, 58, 7, 10);
    painter.drawRect(37, 58, 7, 10);
    return placeholder;
}

void SkinBrowserDialog::decorateItem(QListWidgetItem* item, const Crafty::SkinPtr& skin) const
{
    const auto name = skin->username.isEmpty() ? tr("Unnamed skin") : skin->username;
    item->setIcon(QIcon(iconPixmap(skin)));
    item->setText(name);
    item->setToolTip(skin->username.isEmpty() ? name : tr("%1 · used by %2 players").arg(skin->username).arg(skin->playersCount));
}

void SkinBrowserDialog::fillList()
{
    m_ui->skinList->clear();
    m_current.clear();
    m_previewModel.reset();

    for (const auto& skin : m_skins) {
        auto item = new QListWidgetItem(m_ui->skinList);
        decorateItem(item, skin);
    }
    updateDetails();
}

void SkinBrowserDialog::on_skinList_currentRowChanged(int row)
{
    if (row < 0 || row >= m_skins.size()) {
        m_current.clear();
        m_previewModel.reset();
        updateDetails();
        return;
    }
    m_current = m_skins.at(row);
    updateDetails();

    if (m_current->texture.isNull()) {
        const int request = m_request;
        m_api->fetchTextures(
            { m_current },
            [this, request](const Crafty::SkinPtr& skin) {
                if (request == m_request && skin == m_current)
                    updateDetails();
            },
            [this, request](const QString& error) {
                if (request == m_request && !error.isEmpty())
                    showError(error);
            });
    }
}

void SkinBrowserDialog::updateDetails()
{
    const bool hasSkin = m_current && !m_current->texture.isNull();
    m_ui->useSkinBtn->setEnabled(hasSkin);
    m_ui->siteBtn->setEnabled(m_current && !m_current->hash.isEmpty());

    if (!m_current) {
        m_ui->playerLabel->setText(tr("Select a skin"));
        m_ui->statsLabel->clear();
        m_previewModel.reset();
        if (m_skinPreview)
            m_skinPreview->updateScene(nullptr);
        else if (m_skinPreviewLabel)
            m_skinPreviewLabel->clear();
        return;
    }

    m_ui->playerLabel->setText(m_current->username.isEmpty() ? tr("Unknown player") : m_current->username);

    QStringList stats;
    if (m_current->playersCount > 0)
        stats << tr("%1 players").arg(m_current->playersCount);
    if (m_current->views > 0)
        stats << tr("%1 views").arg(m_current->views);
    if (m_current->upvotes > 0)
        stats << tr("%1 upvotes").arg(m_current->upvotes);
    auto added = QDateTime::fromString(m_current->createdAt, Qt::ISODateWithMs);
    if (added.isValid())
        stats << tr("added %1").arg(added.toLocalTime().toString(QStringLiteral("yyyy-MM-dd")));
    m_ui->statsLabel->setText(stats.join(QStringLiteral(" · ")));

    if (!hasSkin)
        return;
    if (m_current->texturePath.isEmpty())
        Crafty::API::cacheTexture(m_current);
    if (m_current->texturePath.isEmpty())
        return;

    if (!m_previewModel || m_previewModel->getPath() != m_current->texturePath) {
        m_previewModel = std::make_unique<SkinModel>(m_current->texturePath);
        m_previewModel->setModel(m_current->slim ? SkinModel::SLIM : SkinModel::CLASSIC);
    }
    if (m_skinPreview) {
        m_skinPreview->updateScene(m_previewModel.get());
    } else if (m_skinPreviewLabel) {
        m_skinPreviewLabel->setPixmap(QPixmap::fromImage(m_current->texture)
                                          .scaled(256, 256, Qt::KeepAspectRatio, Qt::FastTransformation));
    }
}
