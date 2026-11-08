#include "providers/dgg/DggFrame.hpp"

#include "Test.hpp"

#include <QJsonArray>
#include <QString>

using namespace chatterino;

TEST(DggFrame, Msg)
{
    auto frame = parseDggFrame(
        R"(MSG {"id":167779,"nick":"frozenuser14","roles":[],"features":["flair1","subscriber"],"subscription":{"tier":2,"source":"destiny.gg"},"timestamp":1794165468400,"data":"OOOO"})");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->name, QStringLiteral("MSG"));
    EXPECT_FALSE(frame->backlog);

    auto obj = frame->payload.toObject();
    EXPECT_EQ(obj.value("nick").toString(), QStringLiteral("frozenuser14"));
    EXPECT_EQ(obj.value("data").toString(), QStringLiteral("OOOO"));
    EXPECT_EQ(obj.value("subscription").toObject().value("tier").toInt(), 2);
    EXPECT_EQ(obj.value("features").toArray().size(), 2);
}

TEST(DggFrame, JoinHasNoData)
{
    auto frame = parseDggFrame(
        R"(JOIN {"id":231286,"nick":"justisabell","roles":[],"features":["flair58"],"watching":null,"subscription":null,"timestamp":1794165462697})");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->name, QStringLiteral("JOIN"));

    auto obj = frame->payload.toObject();
    EXPECT_EQ(obj.value("nick").toString(), QStringLiteral("justisabell"));
    EXPECT_FALSE(obj.contains("data"));
    EXPECT_TRUE(obj.value("subscription").isNull());
}

TEST(DggFrame, EventNameIsUppercased)
{
    auto frame = parseDggFrame(R"(msg {"nick":"a","data":"b"})");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->name, QStringLiteral("MSG"));
}

TEST(DggFrame, GreentextEscape)
{
    auto frame = parseDggFrame(
        R"(MSG {"nick":"a","data":"\u003e implying"})");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->payload.toObject().value("data").toString(),
              QStringLiteral("> implying"));
}

TEST(DggFrame, NonJsonPayloadIsKeptAsString)
{
    auto frame = parseDggFrame("ERR notconnected");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->name, QStringLiteral("ERR"));
    EXPECT_TRUE(frame->payload.isString());
    EXPECT_EQ(frame->payload.toString(), QStringLiteral("notconnected"));
}

TEST(DggFrame, NoPayload)
{
    auto frame = parseDggFrame("RELOAD");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->name, QStringLiteral("RELOAD"));
    EXPECT_TRUE(frame->payload.isString());
    EXPECT_TRUE(frame->payload.toString().isEmpty());
}

TEST(DggFrame, Empty)
{
    EXPECT_FALSE(parseDggFrame("").has_value());
    EXPECT_FALSE(parseDggFrame(" {}").has_value());
}

TEST(DggFrame, HistoryExpandsIntoBacklogFrames)
{
    auto history = parseDggFrame(
        R"(HISTORY ["MSG {\"nick\":\"bwaat\",\"data\":\"hi\"}","MSG {\"nick\":\"Kyati\",\"data\":\"there\"}","not a frame {"])");
    ASSERT_TRUE(history.has_value());
    EXPECT_EQ(history->name, QStringLiteral("HISTORY"));
    ASSERT_TRUE(history->payload.isArray());

    std::vector<DggFrame> frames;
    expandDggHistory(history->payload, frames);

    // The third entry has a name but an invalid payload, so it is kept as a
    // string payload frame.
    ASSERT_EQ(frames.size(), size_t{3});
    EXPECT_EQ(frames[0].name, QStringLiteral("MSG"));
    EXPECT_TRUE(frames[0].backlog);
    EXPECT_EQ(frames[0].payload.toObject().value("nick").toString(),
              QStringLiteral("bwaat"));
    EXPECT_EQ(frames[1].payload.toObject().value("data").toString(),
              QStringLiteral("there"));
    EXPECT_EQ(frames[2].name, QStringLiteral("NOT"));
    EXPECT_TRUE(frames[2].payload.isString());
}
