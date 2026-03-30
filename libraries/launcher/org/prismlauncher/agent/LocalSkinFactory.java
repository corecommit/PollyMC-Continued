// SPDX-License-Identifier: GPL-3.0-only
package org.prismlauncher.agent;

import java.io.BufferedReader;
import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.Field;
import java.net.HttpURLConnection;
import java.net.Proxy;
import java.net.URL;
import java.net.URLConnection;
import java.net.URLStreamHandler;
import java.net.URLStreamHandlerFactory;
import java.nio.charset.StandardCharsets;
import java.util.Base64;

class LocalSkinFactory implements URLStreamHandlerFactory {

    private final URLStreamHandler originalHttpsHandler;

    LocalSkinFactory() {
        this.originalHttpsHandler = captureOriginalHandler("https");
    }

    LocalSkinFactory(URLStreamHandler originalHandler) {
        this.originalHttpsHandler = originalHandler;
    }

    private static URLStreamHandler captureOriginalHandler(String protocol) {
        try {
            Field handlersField = URL.class.getDeclaredField("handlers");
            handlersField.setAccessible(true);
            @SuppressWarnings("unchecked")
            java.util.Hashtable<String, URLStreamHandler> handlers =
                (java.util.Hashtable<String, URLStreamHandler>) handlersField.get(null);
            URLStreamHandler handler = handlers.get(protocol);
            if (handler != null) {
                return handler;
            }
        } catch (Exception e) {
            System.err.println("[PrismLauncher] Could not capture original handler: " + e.getMessage());
        }
        // Fallback: create a new URL with null handler to trigger default creation
        try {
            URL dummy = new URL("https://localhost");
            Field handlerField = URL.class.getDeclaredField("handler");
            handlerField.setAccessible(true);
            return (URLStreamHandler) handlerField.get(dummy);
        } catch (Exception e) {
            System.err.println("[PrismLauncher] Could not get default handler: " + e.getMessage());
        }
        return null;
    }

    @Override
    public URLStreamHandler createURLStreamHandler(String protocol) {
        if ("https".equals(protocol)) {
            return new LocalSkinHandler(originalHttpsHandler);
        }
        return null; // use JDK default for other protocols
    }
}

class LocalSkinHandler extends URLStreamHandler {

    private final URLStreamHandler fallback;

    LocalSkinHandler(URLStreamHandler fallback) {
        this.fallback = fallback;
    }

    @Override
    protected URLConnection openConnection(URL url) throws IOException {
        return handleConnection(url, null);
    }

    @Override
    protected URLConnection openConnection(URL url, Proxy proxy) throws IOException {
        return handleConnection(url, proxy);
    }

    private URLConnection handleConnection(URL url, Proxy proxy) throws IOException {
        String host = url.getHost();
        if ("sessionserver.mojang.com".equals(host)) {
            String path = url.getPath();
            if (path != null && path.startsWith("/session/minecraft/profile/")) {
                String uuid = path.substring("/session/minecraft/profile/".length()).replace("-", "");
                String skinDir = findSkinDir(uuid);
                if (skinDir != null) {
                    File skinFile = new File(skinDir, "skin.png");
                    if (skinFile.exists()) {
                        return new LocalSkinURLConnection(url, skinFile, uuid);
                    }
                }
            }
        }

        // Delegate to the original HTTPS handler
        if (fallback != null) {
            try {
                java.lang.reflect.Method m = URLStreamHandler.class.getDeclaredMethod(
                    "openConnection", URL.class, Proxy.class);
                m.setAccessible(true);
                return (URLConnection) m.invoke(fallback, url, proxy);
            } catch (Exception e) {
                // Try single-arg version
                try {
                    java.lang.reflect.Method m = URLStreamHandler.class.getDeclaredMethod(
                        "openConnection", URL.class);
                    m.setAccessible(true);
                    return (URLConnection) m.invoke(fallback, url);
                } catch (Exception e2) {
                    throw new IOException("Cannot connect to " + url, e2);
                }
            }
        }
        // Bypass our handler entirely: create HTTPS connection via internal implementation
        try {
            Class<?> implClass = Class.forName("sun.net.www.protocol.https.HttpsURLConnectionImpl");
            java.lang.reflect.Constructor<?> ctor = implClass.getDeclaredConstructor(URL.class, java.net.Proxy.class);
            ctor.setAccessible(true);
            return (java.net.HttpURLConnection) ctor.newInstance(url, proxy != null ? proxy : java.net.Proxy.NO_PROXY);
        } catch (Exception e) {
            // Fallback: use HttpsURLConnection with default socket factory
            // This creates the connection without going through URL handlers
            javax.net.ssl.HttpsURLConnection.setDefaultHostnameVerifier(
                javax.net.ssl.HttpsURLConnection.getDefaultHostnameVerifier());
            throw new IOException("Cannot create HTTPS connection to " + url + " - " + e.getMessage());
        }
    }

    private static String findSkinDir(String uuid) {
        String[] searchPaths = {
            System.getProperty("prismlauncher.datadir", ""),
            System.getProperty("user.home") + "/.local/share/PrismLauncher",
            System.getProperty("user.home") + "/AppData/Roaming/PrismLauncher"
        };

        for (String basePath : searchPaths) {
            if (basePath == null || basePath.isEmpty()) continue;
            File dir = new File(basePath, "skins/" + uuid);
            if (dir.isDirectory()) {
                return dir.getAbsolutePath();
            }
        }
        return null;
    }
}

class LocalSkinURLConnection extends HttpURLConnection {

    private final File skinFile;
    private final String uuid;

    LocalSkinURLConnection(URL url, File skinFile, String uuid) {
        super(url);
        this.skinFile = skinFile;
        this.uuid = uuid;
    }

    @Override
    public void connect() throws IOException {
        connected = true;
    }

    @Override
    public InputStream getInputStream() throws IOException {
        if (!connected) connect();

        String model = "classic";
        File metaFile = new File(skinFile.getParentFile(), "skin.json");
        if (metaFile.exists()) {
            try {
                BufferedReader reader = new BufferedReader(new FileReader(metaFile));
                String line;
                while ((line = reader.readLine()) != null) {
                    if (line.contains("\"model\"") && line.toUpperCase().contains("SLIM")) {
                        model = "slim";
                    }
                }
                reader.close();
            } catch (Exception ignored) {}
        }

        String skinUrl = "file://" + skinFile.getAbsolutePath().replace("\\", "/");

        String texturesJson = "{\"timestamp\":" + System.currentTimeMillis()
            + ",\"profileId\":\"" + uuid + "\""
            + ",\"profileName\":\"Player\""
            + ",\"textures\":{\"SKIN\":{\"url\":\"" + skinUrl + "\""
            + (model.equals("slim") ? ",\"metadata\":{\"model\":\"slim\"}" : "")
            + "}}}";

        String base64 = Base64.getEncoder().encodeToString(texturesJson.getBytes(StandardCharsets.UTF_8));

        String responseJson = "{\"id\":\"" + uuid + "\""
            + ",\"name\":\"Player\""
            + ",\"properties\":[{\"name\":\"textures\",\"value\":\"" + base64 + "\"}]}";

        responseCode = 200;
        return new ByteArrayInputStream(responseJson.getBytes(StandardCharsets.UTF_8));
    }

    @Override
    public int getResponseCode() {
        return 200;
    }

    @Override
    public String getContentType() {
        return "application/json";
    }

    @Override
    public long getContentLengthLong() {
        return -1;
    }

    @Override
    public boolean usingProxy() {
        return false;
    }

    @Override
    public void disconnect() {
        connected = false;
    }
}
