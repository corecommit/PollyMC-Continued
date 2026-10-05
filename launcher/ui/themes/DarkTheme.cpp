// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PollyMC-Continued - Minecraft Launcher
 *  Copyright (C) 2024 Tayou <git@tayou.org>
 *  Copyright (C) 2024 TheKodeToad <TheKodeToad@proton.me>
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
#include "DarkTheme.h"

#include <QObject>

#include "Application.h"
#include "ui/themes/ThemeManager.h"

QString DarkTheme::id()
{
    return "dark";
}

QString DarkTheme::name()
{
    return QObject::tr("Dark");
}

QPalette DarkTheme::colorScheme()
{
    // Palette now lives in ThemeManager::paletteFor(); both Bright and
    // Dark render according to AppearanceMode (see B3). The faded
    // disabled-state blending moved with it.
    return ThemeManager::paletteFor(APPLICATION->settings()->get("AppearanceMode").toString());
}

double DarkTheme::fadeAmount()
{
    // Post theme-split, the palette comes from
    // ThemeManager::paletteFor(), which inlines this value.
    // Changing it here has no effect; edit paletteFor() instead.
    return 0.5;
}

QColor DarkTheme::fadeColor()
{
    // Post theme-split, the palette comes from
    // ThemeManager::paletteFor(), which inlines this value.
    // Changing it here has no effect; edit paletteFor() instead.
    return QColor(49, 49, 49);
}

bool DarkTheme::hasStyleSheet()
{
    return true;
}

QString DarkTheme::appStyleSheet()
{
    return "QToolTip { color: #ffffff; background-color: #2a82da; border: 1px solid white; }";
}

QString DarkTheme::tooltip()
{
    return "";
}
