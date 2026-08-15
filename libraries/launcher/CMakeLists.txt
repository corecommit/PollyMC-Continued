cmake_minimum_required(VERSION 3.15)
project(launcher Java)
find_package(Java 1.7 REQUIRED COMPONENTS Development)

include(UseJava)
set(CMAKE_JAVA_JAR_ENTRY_POINT org.pollymc.EntryPoint)
set(CMAKE_JAVA_COMPILE_FLAGS -target 8 -source 8)

set(SRC
    org/pollymc/EntryPoint.java
    org/pollymc/launcher/Launcher.java
    org/pollymc/launcher/impl/AbstractLauncher.java
    org/pollymc/launcher/impl/StandardLauncher.java
    org/pollymc/exception/ParameterNotFoundException.java
    org/pollymc/exception/ParseException.java
    org/pollymc/utils/Parameters.java
    org/pollymc/utils/ReflectionUtils.java
    org/pollymc/utils/logging/Level.java
    org/pollymc/utils/logging/Log.java
    org/pollymc/legacy/LegacyProxy.java
)

set(LEGACY_SRC
    legacy/org/pollymc/legacy/LegacyFrame.java
    legacy/org/pollymc/legacy/LegacyLauncher.java
    legacy/org/pollymc/legacy/fix/online/Handler.java
    legacy/org/pollymc/legacy/fix/online/OnlineFixes.java
    legacy/org/pollymc/legacy/fix/online/OnlineModeFix.java
    legacy/org/pollymc/legacy/fix/online/SkinFix.java
    legacy/org/pollymc/legacy/utils/Base64.java
    legacy/org/pollymc/legacy/utils/api/MojangApi.java
    legacy/org/pollymc/legacy/utils/api/Texture.java
    legacy/org/pollymc/legacy/utils/json/JsonParseException.java
    legacy/org/pollymc/legacy/utils/json/JsonParser.java
    legacy/org/pollymc/legacy/utils/url/ByteArrayUrlConnection.java
    legacy/org/pollymc/legacy/utils/url/UrlUtils.java
    legacy/net/minecraft/Launcher.java
    legacy/org/pollymc/legacy/LegacyProxy.java
)

set(SKIN_AGENT_SRC
    org/pollymc/agent/OfflineSkinAgent.java
    org/pollymc/agent/Skin.java
    org/pollymc/agent/protocol/https/Handler.java
)

add_jar(NewLaunch ${SRC})
add_jar(NewLaunchLegacy ${LEGACY_SRC} INCLUDE_JARS NewLaunch)
add_jar(SkinAgent ${SKIN_AGENT_SRC}
    MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/skin-agent/META-INF/MANIFEST.MF"
)
install_jar(NewLaunch "${JARS_DEST_DIR}")
install_jar(NewLaunchLegacy "${JARS_DEST_DIR}")
install_jar(SkinAgent "${JARS_DEST_DIR}")
