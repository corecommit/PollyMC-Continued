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
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2026 PollyMC-Continued Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#pragma once

#include <QFrame>

#include <functional>

class QLabel;
class QPushButton;
class QPropertyAnimation;
class QTimer;

/*!
 * A small, unobtrusive slide-in notification card.
 *
 * It is a child widget of whatever it is shown in, so it never steals focus
 * or blocks interaction. Call showToast() with the desired final position
 * (typically computed by the parent); the card slides in from below, stays
 * for displayMs, then slides back out. The parent is responsible for
 * repositioning it (e.g. on resize) via updatePosition().
 */
class ToastNotification : public QFrame {
    Q_OBJECT

   public:
    explicit ToastNotification(QWidget* parent = nullptr);
    ~ToastNotification() override = default;

    /*!
     * Set the card contents. Called before showToast() so the parent can
     * measure the card (via sizeHint()) for its layout.
     */
    void prepareToast(const QString& title,
                      const QString& body,
                      const QString& actionText,
                      std::function<void()> action);

    /*!
     * Slide the card into view at finalPos (parent-relative).
     * Ignored while the card is already visible or animating.
     */
    void showToast(const QPoint& finalPos, int displayMs = 8000);

    /*!
     * Move the visible card instantly (no animation), e.g. after a parent
     * resize or status-bar toggle.
     */
    void updatePosition(const QPoint& finalPos);

    /*!
     * Slide the card back out and hide it. Safe to call when not visible.
     * Emits dismissed().
     */
    void dismissToast();

    bool isToastVisible() const { return m_visible; }

   signals:
    /*! Emitted when the user closes the card or clicks the action button. */
    void dismissed();

   private slots:
    void onCloseClicked();
    void onActionClicked();
    void onAnimationFinished();

   private:
    void slideOut(bool emitDismissed);

    QLabel* m_iconLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_bodyLabel = nullptr;
    QPushButton* m_closeButton = nullptr;
    QPushButton* m_actionButton = nullptr;
    QTimer* m_autoHideTimer = nullptr;
    QPropertyAnimation* m_animation = nullptr;

    QPoint m_finalPos;
    std::function<void()> m_action;
    bool m_visible = false;
};