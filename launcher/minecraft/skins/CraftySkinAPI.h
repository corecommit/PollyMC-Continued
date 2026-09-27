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

#include <functional>

#include <QImage>
#include <QJsonArray>
#include <QList>
#include <QSharedPointer>
#include <QString>

#include "net/NetJob.h"
#include "tasks/Task.h"

namespace Crafty {

// one entry of the crafty.gg skin catalog
struct Skin {
    QString hash;
    QString username;
    QString createdAt;
    bool slim = false;
    int playersCount = 0;
    int upvotes = 0;
    int views = 0;
    QString texturePath;
    QImage preview;
    QImage texture;
};
using SkinPtr = QSharedPointer<Skin>;

// one page of catalog results
struct SkinPage {
    QList<SkinPtr> skins;
    QString playerName;
    int page = 1;
    bool hasMore = false;
};

// small async client for the public crafty.gg skin catalog
class API : public QObject {
    Q_OBJECT
   public:
    using PageCallback = std::function<void(const SkinPage&, const QString&)>;
    using EachCallback = std::function<void(const SkinPtr&)>;
    using DoneCallback = std::function<void(const QString&)>;

    explicit API(QObject* parent = nullptr);

    // an empty query browses the catalog, otherwise the text is looked up as a player name
    NetJob::Ptr fetchSkins(int page, const QString& query, PageCallback callback);
    // fills texture/preview/texturePath on every entry, cached textures are not downloaded again
    NetJob::Ptr fetchTextures(const QList<SkinPtr>& skins, EachCallback each, DoneCallback done);

    // stops any request still in flight, used when the dialog goes away
    void abortAll();

    static QString pageUrl(const QString& hash);
    // writes the texture to the local cache so the skin can be previewed and installed later
    static bool cacheTexture(const SkinPtr& skin);

   private:
    NetJob::Ptr catalogPage(int page, const QString& search, PageCallback callback);
    NetJob::Ptr playerPage(int page, const QString& playerId, const QString& playerName, PageCallback callback);
    static bool fillTexture(const SkinPtr& skin, const QByteArray& json);
    static bool loadCached(const SkinPtr& skin);

    NetJob::Ptr m_pageJob;
    NetJob::Ptr m_nestedJob;
    QList<NetJob::Ptr> m_textureJobs;
};

}  // namespace Crafty
