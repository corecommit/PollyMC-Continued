// SPDX-License-Identifier: GPL-3.0-only
package org.prismlauncher.agent;

import java.lang.instrument.Instrumentation;
import java.lang.reflect.Field;
import java.net.URL;
import java.net.URLStreamHandler;
import java.util.Hashtable;

public class OfflineSkinAgent {

    public static void premain(String agentArgs, Instrumentation inst) {
        try {
            installURLHandler();
            System.out.println("[PrismLauncher] Offline skin agent installed.");
        } catch (Throwable t) {
            System.err.println("[PrismLauncher] Failed to install offline skin agent: " + t.getMessage());
        }
    }

    @SuppressWarnings("unchecked")
    private static void installURLHandler() throws Exception {
        // Force JDK to create default HTTPS handler by creating a dummy URL
        URL dummyHttps = new URL("https://localhost");
        URLStreamHandler originalHttps = null;

        // Try to capture from the URL instance's handler field
        try {
            Field handlerField = URL.class.getDeclaredField("handler");
            handlerField.setAccessible(true);
            originalHttps = (URLStreamHandler) handlerField.get(dummyHttps);
        } catch (Exception e) {
            System.err.println("[PrismLauncher] Could not capture handler from URL: " + e.getMessage());
        }

        // Also try from the static handlers table
        if (originalHttps == null) {
            try {
                Field handlersField = URL.class.getDeclaredField("handlers");
                handlersField.setAccessible(true);
                Hashtable<String, URLStreamHandler> handlers =
                    (Hashtable<String, URLStreamHandler>) handlersField.get(null);
                originalHttps = handlers.get("https");
            } catch (Exception e) {
                System.err.println("[PrismLauncher] Could not capture handler from table: " + e.getMessage());
            }
        }

        if (originalHttps != null) {
            System.out.println("[PrismLauncher] Captured original HTTPS handler: " + originalHttps.getClass().getName());
        }

        // Install our factory
        LocalSkinFactory factory = new LocalSkinFactory(originalHttps);
        try {
            URL.setURLStreamHandlerFactory(factory);
            System.out.println("[PrismLauncher] URL handler factory installed.");
        } catch (Error e) {
            System.err.println("[PrismLauncher] Could not set URL factory: " + e.getMessage());
        }
    }
}
