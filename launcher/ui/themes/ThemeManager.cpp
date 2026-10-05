// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PollyMC-Continued - Minecraft Launcher
 *  Copyright (C) 2024 Tayou <git@tayou.org>
 *  Copyright (C) 2023 TheKodeToad <TheKodeToad@proton.me>
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
#include "ThemeManager.h"

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QFontDatabase>
#include <QIcon>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include "Exception.h"
#include "ui/themes/BrightTheme.h"
#include "ui/themes/CustomTheme.h"
#include "ui/themes/DarkTheme.h"
#include "ui/themes/SystemTheme.h"

#include "Application.h"
#include "settings/SettingsObject.h"

ThemeManager::ThemeManager()
{
    QIcon::setFallbackThemeName(QIcon::themeName());
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << m_iconThemeFolder.path());

    themeDebugLog() << "Determining System Widget Theme...";
    const auto& style = QApplication::style();
    m_defaultStyle = style->objectName();
    themeDebugLog() << "System theme seems to be:" << m_defaultStyle;

    m_defaultPalette = QApplication::palette();

    initializeThemes();
}

ThemeManager::~ThemeManager()
{
    stopSettingNewWindowColorsOnMac();
}

/// @brief Adds the Theme to the list of themes
/// @param theme The Theme to add
/// @return Theme ID
QString ThemeManager::addTheme(std::unique_ptr<ITheme> theme)
{
    QString id = theme->id();
    if (m_themes.find(id) == m_themes.end())
        m_themes.emplace(id, std::move(theme));
    else
        themeWarningLog() << "Theme(" << id << ") not added to prevent id duplication";
    return id;
}

/// @brief Gets the Theme from the List via ID
/// @param themeId Theme ID of theme to fetch
/// @return Theme at themeId
ITheme* ThemeManager::getTheme(QString themeId)
{
    return m_themes[themeId].get();
}

QString ThemeManager::addIconTheme(IconTheme theme)
{
    QString id = theme.id();
    if (m_icons.find(id) == m_icons.end())
        m_icons.emplace(id, std::move(theme));
    else
        themeWarningLog() << "IconTheme(" << id << ") not added to prevent id duplication";
    return id;
}

void ThemeManager::initializeThemes()
{
    // Icon themes
    initializeIcons();

    // Initialize widget themes
    initializeWidgets();
}

void ThemeManager::initializeIcons()
{
    // TODO: icon themes and instance icons do not mesh well together. Rearrange and fix discrepancies!
    // set icon theme search path!
    themeDebugLog() << "<> Initializing Icon Themes";

    for (const QString& id : builtinIcons) {
        IconTheme theme(id, QString(":/icons/%1").arg(id));
        if (!theme.load()) {
            themeWarningLog() << "Couldn't load built-in icon theme" << id;
            continue;
        }

        addIconTheme(std::move(theme));
        themeDebugLog() << "Loaded Built-In Icon Theme" << id;
    }

    if (!m_iconThemeFolder.mkpath("."))
        themeWarningLog() << "Couldn't create icon theme folder";
    themeDebugLog() << "Icon Theme Folder Path:" << m_iconThemeFolder.absolutePath();

    QDirIterator directoryIterator(m_iconThemeFolder.path(), QDir::Dirs | QDir::NoDotAndDotDot);
    while (directoryIterator.hasNext()) {
        QDir dir(directoryIterator.next());
        IconTheme theme(dir.dirName(), dir.path());
        if (!theme.load())
            continue;

        addIconTheme(std::move(theme));
        themeDebugLog() << "Loaded Custom Icon Theme from" << dir.path();
    }

    themeDebugLog() << "<> Icon themes initialized.";
}

bool ThemeManager::windows11StyleIsUsable()
{
    // Cached: queried from drawPrimitive, which runs many times per frame.
#ifdef Q_OS_WIN
    static const bool usable = QFontDatabase::families().contains("Segoe Fluent Icons", Qt::CaseInsensitive);
    static const bool logged = []() {
        qDebug() << "windows11StyleIsUsable:"
                 << QFontDatabase::families().contains("Segoe Fluent Icons", Qt::CaseInsensitive);
        return true;
    }();
    (void)logged;
    return usable;
#else
    return true;  // non-Windows: style is not offered anyway
#endif
}

void ThemeManager::initializeWidgets()
{
    themeDebugLog() << "<> Initializing Widget Themes";
    themeDebugLog() << "Loading Built-in Theme:" << addTheme(std::make_unique<SystemTheme>(m_defaultStyle, m_defaultPalette, true));
    auto darkThemeId = addTheme(std::make_unique<DarkTheme>());
    themeDebugLog() << "Loading Built-in Theme:" << darkThemeId;
    themeDebugLog() << "Loading Built-in Theme:" << addTheme(std::make_unique<BrightTheme>());

    themeDebugLog() << "<> Initializing System Widget Themes";
    QStringList styles = QStyleFactory::keys();
    for (auto& st : styles) {
#ifdef Q_OS_WINDOWS
        if (QSysInfo::productVersion() != "11" && st == "windows11") {
            continue;
        }
#endif
        themeDebugLog() << "Loading System Theme:" << addTheme(std::make_unique<SystemTheme>(st, m_defaultPalette, false));
    }

    // TODO: need some way to differentiate same name themes in different subdirectories
    //  (maybe smaller grey text next to theme name in dropdown?)

    if (!m_applicationThemeFolder.mkpath("."))
        themeWarningLog() << "Couldn't create theme folder";
    themeDebugLog() << "Theme Folder Path:" << m_applicationThemeFolder.absolutePath();

    QDirIterator directoryIterator(m_applicationThemeFolder.path(), QDir::Dirs | QDir::NoDotAndDotDot);
    while (directoryIterator.hasNext()) {
        QDir dir(directoryIterator.next());
        QFileInfo themeJson(dir.absoluteFilePath("theme.json"));
        if (themeJson.exists()) {
            // Load "theme.json" based themes
            themeDebugLog() << "Loading JSON Theme from:" << themeJson.absoluteFilePath();
            addTheme(std::make_unique<CustomTheme>(getTheme(darkThemeId), themeJson, true));
        } else {
            // Load pure QSS Themes
            QDirIterator stylesheetFileIterator(dir.absoluteFilePath(""), { "*.qss", "*.css" }, QDir::Files);
            while (stylesheetFileIterator.hasNext()) {
                QFile customThemeFile(stylesheetFileIterator.next());
                QFileInfo customThemeFileInfo(customThemeFile);
                themeDebugLog() << "Loading QSS Theme from:" << customThemeFileInfo.absoluteFilePath();
                addTheme(std::make_unique<CustomTheme>(getTheme(darkThemeId), customThemeFileInfo, false));
            }
        }
    }

    themeDebugLog() << "<> Widget themes initialized.";
}

#ifndef Q_OS_MACOS
void ThemeManager::setTitlebarColorOnMac(WId windowId, QColor color) {}
void ThemeManager::setTitlebarColorOfAllWindowsOnMac(QColor color) {}
void ThemeManager::stopSettingNewWindowColorsOnMac() {}
#endif

QList<IconTheme*> ThemeManager::getValidIconThemes()
{
    QList<IconTheme*> ret;
    ret.reserve(m_icons.size());
    for (auto&& [id, theme] : m_icons) {
        ret.append(&theme);
    }
    return ret;
}

QList<ITheme*> ThemeManager::getValidApplicationThemes()
{
    QList<ITheme*> ret;
    ret.reserve(m_themes.size());
    for (auto&& [id, theme] : m_themes) {
        // Bright/Dark stay registered (CustomTheme uses Dark as its
        // base, and saved bright/dark ids must keep resolving), but
        // they are just Fusion + AppearanceMode now, so they are not
        // offered as separate widget styles.
        if (id == "bright" || id == "dark")
            continue;
        ret.append(theme.get());
    }
    return ret;
}

bool ThemeManager::isValidIconTheme(const QString& id)
{
    return !id.isEmpty() && m_icons.find(id) != m_icons.end();
}

bool ThemeManager::isValidApplicationTheme(const QString& id)
{
    return !id.isEmpty() && m_themes.find(id) != m_themes.end();
}

QDir ThemeManager::getIconThemesFolder()
{
    return m_iconThemeFolder;
}

QDir ThemeManager::getApplicationThemesFolder()
{
    return m_applicationThemeFolder;
}

void ThemeManager::setIconTheme(const QString& name)
{
    if (m_icons.find(name) == m_icons.end()) {
        themeWarningLog() << "Tried to set invalid icon theme:" << name;
        return;
    }

    QIcon::setThemeName(name);
}

void ThemeManager::setApplicationTheme(const QString& name, bool initial)
{
    auto systemPalette = qApp->palette();
    auto themeIter = m_themes.find(name);
    if (themeIter != m_themes.end()) {
        auto& theme = themeIter->second;
        themeDebugLog() << "applying theme" << theme->name();
        theme->apply(initial);
        setTitlebarColorOfAllWindowsOnMac(qApp->palette().window().color());

        m_logColors = theme->logColorScheme();
    } else {
        themeWarningLog() << "Tried to set invalid theme:" << name;
    }
}

void ThemeManager::applyCurrentlySelectedTheme(bool initial)
{
    auto settings = APPLICATION->settings();
    setIconTheme(settings->get("IconTheme").toString());
    themeDebugLog() << "<> Icon theme set.";
    auto applicationTheme = settings->get("ApplicationTheme").toString();
    if (applicationTheme == "") {
        applicationTheme = m_defaultStyle;
    }
    if (!isValidApplicationTheme(applicationTheme)) {
        // windowsvista (or anything else) missing too, e.g. odd Wine
        // builds: fall back to Fusion, which is always registered.
        // Never crashes, never writes settings.
        themeDebugLog() << "Theme" << applicationTheme << "not available; falling back to Fusion";
        applicationTheme = "fusion";
    }
    setApplicationTheme(applicationTheme, initial);
    themeDebugLog() << "<> Application theme set.";
}

QPalette ThemeManager::paletteFor(const QString& mode)
{
    // Dark branch first: "System" on a dark OS must not fall through
    // to Light when colorScheme() reports Unknown on older Qt.
    QString resolved = mode;
    if (resolved == "System") {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        // Light and Unknown both fall back to Light.
        resolved = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark ? "Dark" : "Light";
#else
        resolved = "Light";
#endif
    }
    if (resolved == "Dark") {
        // Verbatim copy of DarkTheme::colorScheme() (which now routes
        // here); fade amounts inlined because they live on that class.
        QPalette darkPalette;
        darkPalette.setColor(QPalette::Window, QColor(49, 49, 49));
        darkPalette.setColor(QPalette::WindowText, Qt::white);
        darkPalette.setColor(QPalette::Base, QColor(34, 34, 34));
        darkPalette.setColor(QPalette::AlternateBase, QColor(42, 42, 42));
        darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
        darkPalette.setColor(QPalette::ToolTipText, Qt::white);
        darkPalette.setColor(QPalette::Text, Qt::white);
        darkPalette.setColor(QPalette::Button, QColor(48, 48, 48));
        darkPalette.setColor(QPalette::ButtonText, Qt::white);
        darkPalette.setColor(QPalette::BrightText, Qt::red);
        darkPalette.setColor(QPalette::Link, QColor(47, 163, 198));
        darkPalette.setColor(QPalette::Highlight, QColor(150, 219, 89));
        darkPalette.setColor(QPalette::HighlightedText, Qt::black);
        darkPalette.setColor(QPalette::PlaceholderText, Qt::darkGray);
        return ITheme::fadeInactive(darkPalette, 0.5, QColor(49, 49, 49));
    }
    // Light branch (also the fallback): verbatim copy of
    // BrightTheme::colorScheme().
    QPalette brightPalette;
    brightPalette.setColor(QPalette::Window, QColor(255, 255, 255));
    brightPalette.setColor(QPalette::WindowText, QColor(17, 17, 17));
    brightPalette.setColor(QPalette::Base, QColor(250, 250, 250));
    brightPalette.setColor(QPalette::AlternateBase, QColor(240, 240, 240));
    brightPalette.setColor(QPalette::ToolTipBase, QColor(17, 17, 17));
    brightPalette.setColor(QPalette::ToolTipText, QColor(255, 255, 255));
    brightPalette.setColor(QPalette::Text, Qt::black);
    brightPalette.setColor(QPalette::Button, QColor(249, 249, 249));
    brightPalette.setColor(QPalette::ButtonText, Qt::black);
    brightPalette.setColor(QPalette::BrightText, Qt::red);
    brightPalette.setColor(QPalette::Link, QColor(37, 137, 164));
    brightPalette.setColor(QPalette::Highlight, QColor(137, 207, 84));
    brightPalette.setColor(QPalette::HighlightedText, Qt::black);
    return ITheme::fadeInactive(brightPalette, 0.5, QColor(255, 255, 255));
}

void ThemeManager::applyAppearanceMode(const QString& mode)
{
    APPLICATION->settings()->set("AppearanceMode", mode);
    applyCurrentlySelectedTheme();
}

void ThemeManager::refresh()
{
    m_themes.clear();
    m_icons.clear();

    initializeThemes();
}
