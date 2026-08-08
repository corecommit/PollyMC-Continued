// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2026 PollyMC Continued Contributors
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
package org.prismlauncher.agent;

import java.lang.instrument.Instrumentation;

/**
 * Java agent that makes offline accounts use locally stored skins.
 *
 * <p>It deliberately avoids {@code URL.setURLStreamHandlerFactory}: that method
 * can only be called once per JVM and the NeoForge/Forge (securejarhandler) and
 * Quilt loaders need it for their own class-loading protocols. Instead the agent
 * registers an HTTPS protocol handler via {@code java.protocol.handler.pkgs},
 * which {@code java.net.URL} consults after any factory and which does not
 * conflict with the loaders.
 */
public class OfflineSkinAgent {

    public static void premain(String agentArgs, Instrumentation inst) {
        try {
            Skin.install();
            System.out.println("[PrismLauncher] Offline skin agent installed.");
        } catch (Throwable t) {
            System.err.println("[PrismLauncher] Failed to install offline skin agent: " + t.getMessage());
        }
    }
}