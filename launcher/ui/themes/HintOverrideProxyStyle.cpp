// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PollyMC-Continued - Minecraft Launcher
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
 */

#include "HintOverrideProxyStyle.h"

#include <QDebug>
#include <QStyleFactory>

#include "ui/themes/ThemeManager.h"

HintOverrideProxyStyle::HintOverrideProxyStyle(QStyle* style) : QProxyStyle(style)
{
    setObjectName(baseStyle()->objectName());
}

namespace {
// Primitives QWindows11Style draws with Segoe Fluent Icons glyphs.
// Without that font they render as .notdef boxes (Win10, Wine,
// Winlator) — these alone fall back to Fusion rendering.
bool usesFluentGlyph(QStyle::PrimitiveElement pe)
{
    switch (pe) {
        case QStyle::PE_IndicatorSpinUp:
        case QStyle::PE_IndicatorSpinDown:
        case QStyle::PE_IndicatorArrowUp:
        case QStyle::PE_IndicatorArrowDown:
        case QStyle::PE_IndicatorArrowLeft:
        case QStyle::PE_IndicatorArrowRight:
        case QStyle::PE_IndicatorCheckBox:
        case QStyle::PE_IndicatorRadioButton:
        case QStyle::PE_IndicatorBranch:
        case QStyle::PE_IndicatorItemViewItemCheck:
            return true;
        default:
            return false;
    }
}
}  // namespace

void HintOverrideProxyStyle::drawPrimitive(PrimitiveElement element,
                                           const QStyleOption* option,
                                           QPainter* painter,
                                           const QWidget* widget) const
{
    qDebug() << "PROXY drawPrimitive" << element
             << "font-usable:" << ThemeManager::windows11StyleIsUsable();
    if (!ThemeManager::windows11StyleIsUsable() && usesFluentGlyph(element)) {
        static QStyle* fallback = QStyleFactory::create("fusion");
        if (fallback) {
            fallback->drawPrimitive(element, option, painter, widget);
            return;
        }
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

int HintOverrideProxyStyle::styleHint(QStyle::StyleHint hint,
                                      const QStyleOption* option,
                                      const QWidget* widget,
                                      QStyleHintReturn* returnData) const
{
    if (hint == QStyle::SH_ItemView_ActivateItemOnSingleClick)
        return 0;

    if (hint == QStyle::SH_Slider_AbsoluteSetButtons)
        return Qt::LeftButton | Qt::MiddleButton;

    if (hint == QStyle::SH_Slider_PageSetButtons)
        return Qt::RightButton;

    return QProxyStyle::styleHint(hint, option, widget, returnData);
}
