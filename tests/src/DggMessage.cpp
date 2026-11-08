#include "providers/dgg/DggMessage.hpp"

#include "Test.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

using namespace chatterino;

namespace {

QJsonObject obj(const char *json)
{
    return QJsonDocument::fromJson(json).object();
}

constexpr qint64 NOW = 1000;

}  // namespace

TEST(DggMessage, HashMatchesChatGuiRecipe)
{
    // md5("<timestamp ms><user id><text>")
    EXPECT_EQ(dggMessageHash(1794165468400, 167779, QStringLiteral("OOOO")),
              QStringLiteral("12b94495cda14e88bc2760a8caa82b45"));

    // an unknown user id counts as empty
    EXPECT_EQ(dggMessageHash(1794165468400, 0, QStringLiteral("hello")),
              QStringLiteral("8e390221629e2a9cec1f1c502244a3bd"));
}

TEST(DggMessage, ParsesCapturedMsg)
{
    auto msg = parseDggChatMessage(
        obj(R"({"id":167779,"nick":"frozenuser14","roles":[],"features":["flair1","subscriber"],"createdDate":"2022-10-02T22:11:55Z","watching":{"platform":"angelthump","id":"kiloss"},"subscription":{"tier":2,"source":"destiny.gg"},"patreonMember":false,"photo":"alien","timestamp":1794165468400,"data":"OOOO"})"),
        NOW);
    ASSERT_TRUE(msg.has_value());

    EXPECT_EQ(msg->user.id, 167779);
    EXPECT_EQ(msg->user.nick, QStringLiteral("frozenuser14"));
    EXPECT_EQ(msg->user.subTier, 2);
    EXPECT_TRUE(msg->user.isSubscriber());
    EXPECT_FALSE(msg->user.isPrivileged());
    EXPECT_EQ(msg->user.features,
              (QStringList{QStringLiteral("flair1"),
                           QStringLiteral("subscriber")}));
    EXPECT_EQ(msg->timestampMs, 1794165468400);
    EXPECT_EQ(msg->time().toMSecsSinceEpoch(), 1794165468400);
    EXPECT_EQ(msg->text, QStringLiteral("OOOO"));
    EXPECT_EQ(msg->hash, QStringLiteral("12b94495cda14e88bc2760a8caa82b45"));
    EXPECT_FALSE(msg->isAction);
    EXPECT_FALSE(msg->greentext);
}

TEST(DggMessage, NullSubscriptionIsTierZero)
{
    auto msg = parseDggChatMessage(
        obj(R"({"id":5,"nick":"a","roles":[],"features":[],"subscription":null,"timestamp":1,"data":"x"})"),
        NOW);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->user.subTier, 0);
    EXPECT_FALSE(msg->user.isSubscriber());
}

TEST(DggMessage, RejectsMissingNickOrText)
{
    EXPECT_FALSE(
        parseDggChatMessage(obj(R"({"id":5,"data":"x"})"), NOW).has_value());
    EXPECT_FALSE(
        parseDggChatMessage(obj(R"({"id":5,"nick":"a"})"), NOW).has_value());
    EXPECT_FALSE(
        parseDggChatMessage(obj(R"({"id":5,"nick":"a","data":""})"), NOW)
            .has_value());
}

TEST(DggMessage, MissingTimestampUsesNow)
{
    auto msg =
        parseDggChatMessage(obj(R"({"id":5,"nick":"a","data":"x"})"), 4242);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->timestampMs, 4242);
}

TEST(DggMessage, SlashMeIsAnActionInAnyCase)
{
    auto lower = parseDggChatMessage(
        obj(R"({"id":5,"nick":"a","timestamp":1,"data":"/me waves"})"), NOW);
    ASSERT_TRUE(lower.has_value());
    EXPECT_TRUE(lower->isAction);
    EXPECT_EQ(lower->text, QStringLiteral("waves"));
    EXPECT_EQ(lower->rawText, QStringLiteral("/me waves"));

    auto upper = parseDggChatMessage(
        obj(R"({"id":5,"nick":"a","timestamp":1,"data":"/ME waves"})"), NOW);
    ASSERT_TRUE(upper.has_value());
    EXPECT_TRUE(upper->isAction);
    EXPECT_EQ(upper->text, QStringLiteral("waves"));

    // needs the space, like chat-gui
    auto notAction = parseDggChatMessage(
        obj(R"({"id":5,"nick":"a","timestamp":1,"data":"/mex"})"), NOW);
    ASSERT_TRUE(notAction.has_value());
    EXPECT_FALSE(notAction->isAction);
    EXPECT_EQ(notAction->text, QStringLiteral("/mex"));
}

TEST(DggMessage, GreentextNeedsSubscriberOrPrivilegedRole)
{
    auto plain = parseDggChatMessage(
        obj(R"({"id":5,"nick":"a","roles":[],"subscription":null,"timestamp":1,"data":">be me"})"),
        NOW);
    ASSERT_TRUE(plain.has_value());
    EXPECT_FALSE(plain->greentext);

    auto sub = parseDggChatMessage(
        obj(R"({"id":5,"nick":"a","roles":[],"subscription":{"tier":1},"timestamp":1,"data":">be me"})"),
        NOW);
    ASSERT_TRUE(sub.has_value());
    EXPECT_TRUE(sub->greentext);

    for (const char *role : {"MODERATOR", "PROTECTED", "ADMIN"})
    {
        auto json = QStringLiteral(
                        R"({"id":5,"nick":"a","roles":["%1"],"subscription":null,"timestamp":1,"data":">be me"})")
                        .arg(QString::fromLatin1(role))
                        .toUtf8();
        auto msg = parseDggChatMessage(
            QJsonDocument::fromJson(json).object(), NOW);
        ASSERT_TRUE(msg.has_value());
        EXPECT_TRUE(msg->user.isPrivileged()) << role;
        EXPECT_TRUE(msg->greentext) << role;
    }

    // roles that aren't privileged don't count (BOT, HOST, POLLS)
    auto bot = parseDggChatMessage(
        obj(R"({"id":5,"nick":"a","roles":["BOT"],"subscription":null,"timestamp":1,"data":">be me"})"),
        NOW);
    ASSERT_TRUE(bot.has_value());
    EXPECT_FALSE(bot->greentext);

    // `>` has to be the first character
    auto later = parseDggChatMessage(
        obj(R"({"id":5,"nick":"a","roles":[],"subscription":{"tier":1},"timestamp":1,"data":"hi >be me"})"),
        NOW);
    ASSERT_TRUE(later.has_value());
    EXPECT_FALSE(later->greentext);
}

TEST(DggMessage, GreentextAfterSlashMe)
{
    auto msg = parseDggChatMessage(
        obj(R"({"id":5,"nick":"a","roles":["ADMIN"],"timestamp":1,"data":"/me >be me"})"),
        NOW);
    ASSERT_TRUE(msg.has_value());
    EXPECT_TRUE(msg->isAction);
    EXPECT_TRUE(msg->greentext);
}

TEST(DggMessage, ModerationText)
{
    auto mute = obj(
        R"({"id":9,"nick":"modguy","roles":["MODERATOR"],"features":[],"timestamp":1,"data":"spammer","duration":600})");
    EXPECT_EQ(dggModerationText(u"MUTE", mute),
              QStringLiteral("spammer muted by modguy for 10m."));

    auto muteNoDuration = obj(
        R"({"id":9,"nick":"modguy","timestamp":1,"data":"spammer"})");
    EXPECT_EQ(dggModerationText(u"MUTE", muteNoDuration),
              QStringLiteral("spammer muted by modguy."));

    auto ban = obj(R"({"id":9,"nick":"modguy","timestamp":1,"data":"troll"})");
    EXPECT_EQ(dggModerationText(u"BAN", ban),
              QStringLiteral("troll banned by modguy."));
    EXPECT_EQ(dggModerationText(u"UNBAN", ban),
              QStringLiteral("troll unbanned by modguy."));
    EXPECT_EQ(dggModerationText(u"UNMUTE", ban),
              QStringLiteral("troll unmuted by modguy."));

    // the website lifting a mute is sent as user -1 and has no moderator
    auto system = obj(
        R"({"id":-1,"nick":"System","timestamp":1,"data":"subscriber"})");
    EXPECT_EQ(dggModerationText(u"UNMUTE", system),
              QStringLiteral("subscriber has been unmuted."));
}

TEST(DggMessage, ModerationTextIgnoresOtherEventsAndEmptyTargets)
{
    EXPECT_TRUE(dggModerationText(u"MSG", obj(R"({"nick":"a","data":"b"})"))
                    .isEmpty());
    EXPECT_TRUE(dggModerationText(u"BAN", obj(R"({"nick":"a"})")).isEmpty());
    EXPECT_TRUE(
        dggModerationText(u"BAN", obj(R"({"nick":"a","data":""})")).isEmpty());
}

TEST(DggMessage, SubOnlyText)
{
    EXPECT_EQ(dggSubOnlyText(obj(
                  R"({"data":"on","tier":1,"nick":"modguy","timestamp":1})")),
              QStringLiteral("Subscriber only mode enabled by modguy."));
    EXPECT_EQ(dggSubOnlyText(obj(
                  R"({"data":"on","tier":3,"nick":"modguy","timestamp":1})")),
              QStringLiteral(
                  "Subscriber only mode (Tier 3+) enabled by modguy."));
    EXPECT_EQ(dggSubOnlyText(obj(R"({"data":"off","timestamp":1})")),
              QStringLiteral("Subscriber only mode disabled."));
    // no tier means tier 1
    EXPECT_EQ(dggSubOnlyText(obj(R"({"data":"on","nick":"m"})")),
              QStringLiteral("Subscriber only mode enabled by m."));
}

TEST(DggMessage, ErrorText)
{
    EXPECT_TRUE(dggErrorText(obj(R"({"description":"banned"})"))
                    .startsWith(QStringLiteral("You have been banned!")));

    EXPECT_EQ(dggErrorText(obj(R"({"description":"toomanyconnections"})")),
              QStringLiteral("Only 5 concurrent connections allowed"));

    EXPECT_EQ(dggErrorText(obj(R"({"description":"submode","tier":2})")),
              QStringLiteral(
                  "The channel is currently in subscriber only mode (Tier "
                  "2+)"));

    EXPECT_EQ(
        dggErrorText(obj(R"({"description":"bannedphrase","filtered":"x"})")),
        QStringLiteral("Your message was blocked because it contained this "
                       "banned phrase: \"x\"."));

    EXPECT_TRUE(
        dggErrorText(obj(R"({"description":"muted","muteTimeLeft":90})"))
            .contains(QStringLiteral("1m 30s")));

    // unknown codes are shown as they are
    EXPECT_EQ(dggErrorText(obj(R"({"description":"somethingnew"})")),
              QStringLiteral("somethingnew"));
    EXPECT_EQ(dggErrorText(obj(R"({})")), QStringLiteral("Unknown error."));
}
