#include <QtTest>

#include "bot/BotScript.h"

#include <QJsonArray>
#include <QJsonDocument>

static QJsonObject parseJson(const char* text)
{
    return QJsonDocument::fromJson(QByteArray(text)).object();
}

class TestBotScript : public QObject {
    Q_OBJECT

   private slots:
    void validFullScript()
    {
        BotScript script;
        QString error;
        const bool ok = BotScriptParser::parse(
            parseJson(R"({"version":1,"name":"farm-1","steps":[
                {"type":"say","text":"hello"},
                {"type":"command","text":"/time query daytime"},
                {"type":"wait","seconds":30},
                {"type":"wait_for_chat","pattern":"day","case_sensitive":true,"timeout_seconds":60},
                {"type":"wait_for_player","player":"Alice","timeout_seconds":0},
                {"type":"log","text":"started"},
                {"type":"loop","times":-1,"steps":[{"type":"say","text":"hi"}]}
            ]})"),
            script, error);
        QVERIFY2(ok, qPrintable(error));
        QCOMPARE(script.steps.size(), 7);
        QCOMPARE(script.steps[1].text, QString("/time query daytime"));
        QCOMPARE(script.steps[6].steps.size(), 1);
    }

    void commandGetsSlash()
    {
        BotScript script;
        QString error;
        QVERIFY(BotScriptParser::parse(parseJson(R"({"version":1,"name":"a","steps":[{"type":"command","text":"time query"}]})"),
                                       script, error));
        QCOMPARE(script.steps[0].text, QString("/time query"));
    }

    void nestedLoopThreeLevels()
    {
        BotScript script;
        QString error;
        const bool ok = BotScriptParser::parse(
            parseJson(R"({"version":1,"name":"n","steps":[
                {"type":"loop","times":2,"steps":[
                    {"type":"loop","times":2,"steps":[
                        {"type":"loop","times":2,"steps":[{"type":"say","text":"deep"}]}
                    ]}
                ]}
            ]})"),
            script, error);
        QVERIFY2(ok, qPrintable(error));
    }

    void rejectBadVersion()
    {
        BotScript script;
        QString error;
        QVERIFY(!BotScriptParser::parse(parseJson(R"({"version":2,"name":"a","steps":[{"type":"say","text":"x"}]})"),
                                        script, error));
    }

    void rejectBadName()
    {
        BotScript script;
        QString error;
        QVERIFY(!BotScriptParser::parse(parseJson(R"({"version":1,"name":"has space","steps":[{"type":"say","text":"x"}]})"),
                                        script, error));
    }

    void rejectEmptySteps()
    {
        BotScript script;
        QString error;
        QVERIFY(!BotScriptParser::parse(parseJson(R"({"version":1,"name":"a","steps":[]})"), script, error));
    }

    void rejectUnknownType()
    {
        BotScript script;
        QString error;
        QVERIFY(!BotScriptParser::parse(parseJson(R"({"version":1,"name":"a","steps":[{"type":"dance","text":"x"}]})"),
                                        script, error));
    }

    void rejectZeroWait()
    {
        BotScript script;
        QString error;
        QVERIFY(!BotScriptParser::parse(parseJson(R"({"version":1,"name":"a","steps":[{"type":"wait","seconds":0}]})"),
                                        script, error));
    }

    void rejectZeroLoop()
    {
        BotScript script;
        QString error;
        QVERIFY(!BotScriptParser::parse(
            parseJson(R"({"version":1,"name":"a","steps":[{"type":"loop","times":0,"steps":[{"type":"say","text":"x"}]}]})"),
            script, error));
    }

    void rejectSixLevelLoop()
    {
        const char* inner = R"({"type":"say","text":"x"})";
        QString nested(inner);
        for (int i = 0; i < 6; i++)
            nested = QString(R"({"type":"loop","times":1,"steps":[%1]})").arg(nested);
        BotScript script;
        QString error;
        QVERIFY(!BotScriptParser::parse(parseJson(QString(R"({"version":1,"name":"a","steps":[%1]})").arg(nested).toUtf8().constData()),
                                        script, error));
    }

    void rejectEmptyCommand()
    {
        BotScript script;
        QString error;
        QVERIFY(!BotScriptParser::parse(parseJson(R"({"version":1,"name":"a","steps":[{"type":"command","text":""}]})"),
                                        script, error));
    }

    void roundTrip()
    {
        const QByteArray raw = R"({"name":"rt","steps":[{"text":"hi","type":"say"},{"steps":[{"seconds":5,"type":"wait"}],"times":3,"type":"loop"}],"version":1})";
        BotScript script;
        QString error;
        QVERIFY2(BotScriptParser::parse(QJsonDocument::fromJson(raw).object(), script, error), qPrintable(error));
        const QByteArray out = QJsonDocument(BotScriptParser::serialize(script)).toJson(QJsonDocument::Compact);
        QCOMPARE(out, raw);
    }

    void validSteps()
    {
        QString error;
        BotScriptStep s;
        s.type = "say";
        s.text = "hi";
        QVERIFY(BotScriptParser::validateStep(s, error));
        s.type = "command";
        QVERIFY(BotScriptParser::validateStep(s, error));
        s.type = "log";
        QVERIFY(BotScriptParser::validateStep(s, error));
        s = BotScriptStep();
        s.type = "wait";
        s.seconds = 5;
        QVERIFY(BotScriptParser::validateStep(s, error));
        s = BotScriptStep();
        s.type = "wait_for_chat";
        s.pattern = "hi";
        QVERIFY(BotScriptParser::validateStep(s, error));
        s = BotScriptStep();
        s.type = "wait_for_player";
        s.player = "Alice";
        QVERIFY(BotScriptParser::validateStep(s, error));
        s = BotScriptStep();
        s.type = "loop";
        s.times = 2;
        s.steps.append(defaultSay());
        QVERIFY(BotScriptParser::validateStep(s, error));
    }

    void invalidSayEmpty()
    {
        QString error;
        BotScriptStep s;
        s.type = "say";
        QVERIFY(!BotScriptParser::validateStep(s, error));
    }

    void invalidWaitZero()
    {
        QString error;
        BotScriptStep s;
        s.type = "wait";
        QVERIFY(!BotScriptParser::validateStep(s, error));
    }

    void invalidLoopEmpty()
    {
        QString error;
        BotScriptStep s;
        s.type = "loop";
        s.times = 2;
        QVERIFY(!BotScriptParser::validateStep(s, error));
    }

    void invalidChatEmptyPattern()
    {
        QString error;
        BotScriptStep s;
        s.type = "wait_for_chat";
        QVERIFY(!BotScriptParser::validateStep(s, error));
    }

    void commandNormalized()
    {
        BotScript script;
        QString error;
        QVERIFY(BotScriptParser::parse(parseJson(R"({"version":1,"name":"a","steps":[{"type":"command","text":"time query"}]})"),
                                       script, error));
        QVERIFY(script.steps[0].text.startsWith('/'));
    }

   private:
    static BotScriptStep defaultSay()
    {
        BotScriptStep s;
        s.type = "say";
        s.text = "hi";
        return s;
    }
};

QTEST_MAIN(TestBotScript)
#include "TestBotScript.moc"
