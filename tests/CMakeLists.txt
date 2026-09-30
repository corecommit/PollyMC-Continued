find_package(Qt6 REQUIRED COMPONENTS Core Test)

add_executable(TestBotScript
    TestBotScript.cpp
    ../launcher/bot/BotScript.cpp
)

target_include_directories(TestBotScript PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../launcher
)

target_link_libraries(TestBotScript PRIVATE Qt6::Core Qt6::Test)

# moc the test's own Q_OBJECT
set_target_properties(TestBotScript PROPERTIES AUTOMOC ON)

add_test(NAME BotScript COMMAND TestBotScript)
