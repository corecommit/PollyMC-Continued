package org.pollymc;

import org.pollymc.utils.Parameters;

public final class SystemProperties {
    public static void apply(Parameters params) {
        String launcherBrand = params.getString("launcherBrand", null);
        String launcherVersion = params.getString("launcherVersion", null);
        String name = params.getString("instanceName", null);
        String iconId = params.getString("instanceIconKey", null);
        String iconPath = params.getString("instanceIconPath", null);
        String windowTitle = params.getString("windowTitle", null);
        String windowDimensions = params.getString("windowParams", null);

        if (launcherBrand != null)
            System.setProperty("minecraft.launcher.brand", launcherBrand);
        if (launcherVersion != null)
            System.setProperty("minecraft.launcher.version", launcherVersion);

        // set useful properties for mods
        if (name != null)
            System.setProperty("org.pollymc.instance.name", name);
        if (iconId != null)
            System.setProperty("org.pollymc.instance.icon.id", iconId);
        if (iconPath != null)
            System.setProperty("org.pollymc.instance.icon.path", iconPath);
        if (windowTitle != null)
            System.setProperty("org.pollymc.window.title", windowTitle);
        if (windowDimensions != null)
            System.setProperty("org.pollymc.window.dimensions", windowDimensions);

        // set multimc properties for compatibility
        if (name != null)
            System.setProperty("multimc.instance.title", name);
        if (iconId != null)
            System.setProperty("multimc.instance.icon", iconId);
    }
}
