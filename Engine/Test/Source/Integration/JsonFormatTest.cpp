#include "Core/Common/JsonFormat.h"

#include <gtest/gtest.h>

#include <string>

namespace ya
{
namespace
{

void expectRoundTrip(const nlohmann::json& value, int indent = 2, int wrapColumn = 100)
{
    const std::string text = dumpJsonCompactLeaves(value, indent, wrapColumn);
    const auto        back = nlohmann::json::parse(text);
    EXPECT_EQ(back, value) << text;
}

TEST(JsonFormatTest, EmptyContainersAndScalars)
{
    EXPECT_EQ(dumpJsonCompactLeaves(nlohmann::json::object()), "{}");
    EXPECT_EQ(dumpJsonCompactLeaves(nlohmann::json::array()), "[]");
    EXPECT_EQ(dumpJsonCompactLeaves(nlohmann::json(true)), "true");
    EXPECT_EQ(dumpJsonCompactLeaves(nlohmann::json(nullptr)), "null");
    EXPECT_EQ(dumpJsonCompactLeaves(nlohmann::json("中文\"\\\n")), nlohmann::json("中文\"\\\n").dump());

    nlohmann::json object = nlohmann::json::object();
    object["b"]           = nlohmann::json::array();
    object["a"]           = nlohmann::json::object();
    EXPECT_EQ(dumpJsonCompactLeaves(object), "{\n  \"a\": {},\n  \"b\": []\n}");
}

TEST(JsonFormatTest, KeyOrderFollowsNlohmann)
{
    nlohmann::json object = nlohmann::json::object();
    object["z"]           = 1;
    object["a"]           = 2;
    object["m"]           = 3;
    EXPECT_EQ(dumpJsonCompactLeaves(object), "{\n  \"a\": 2,\n  \"m\": 3,\n  \"z\": 1\n}");
}

TEST(JsonFormatTest, ScalarArrayStaysOnOneLineUntilTheColumn)
{
    nlohmann::json object = nlohmann::json::object();
    object["n"]           = nlohmann::json::array({1, 2, 3, 4, 5});

    // `  "n": [1, 2, 3, 4, 5]` is 22 bytes. 22 fits; 21 wraps before the tail.
    EXPECT_EQ(dumpJsonCompactLeaves(object, 2, 22), "{\n  \"n\": [1, 2, 3, 4, 5]\n}");
    EXPECT_EQ(dumpJsonCompactLeaves(object, 2, 21), "{\n  \"n\": [1, 2, 3, 4,\n    5]\n}");
}

TEST(JsonFormatTest, WrappedLinesFillUpToTheColumn)
{
    nlohmann::json values = nlohmann::json::array();
    for (int index = 0; index < 12; ++index) {
        values.push_back(index);
    }
    const std::string text = dumpJsonCompactLeaves(values, 2, 16);
    expectRoundTrip(values, 2, 16);
    EXPECT_EQ(text,
              "[0, 1, 2, 3, 4,\n"
              "  5, 6, 7, 8, 9,\n"
              "  10, 11]");
}

TEST(JsonFormatTest, NestedArraysAndObjectsExpand)
{
    nlohmann::json object = {
        {"clips", nlohmann::json::array({nlohmann::json{{"frames", nlohmann::json::array({0, 1, 2, 1})}, {"name", "walk"}}})},
        {"empty", nlohmann::json::array({nlohmann::json::object(), nlohmann::json::array()})},
    };
    const std::string text = dumpJsonCompactLeaves(object);
    EXPECT_EQ(text,
              "{\n"
              "  \"clips\": [\n"
              "    {\n"
              "      \"frames\": [0, 1, 2, 1],\n"
              "      \"name\": \"walk\"\n"
              "    }\n"
              "  ],\n"
              "  \"empty\": [\n"
              "    {},\n"
              "    []\n"
              "  ]\n"
              "}");
    expectRoundTrip(object);
}

TEST(JsonFormatTest, RoundTripLargeArrayUnicodeAndFloats)
{
    nlohmann::json value = nlohmann::json::object();
    nlohmann::json cells = nlohmann::json::array();
    for (int index = 0; index < 80; ++index) {
        cells.push_back(index % 5);
    }
    value["cells"] = std::move(cells);
    value["label"] = "栅栏 \"north\"";
    value["mix"]   = nlohmann::json::array({true, false, nullptr, 1.5, "a\nb"});
    value["grid"]  = nlohmann::json::array({nlohmann::json::array({1, 2}), nlohmann::json::array({3})});
    expectRoundTrip(value, 2, 40);

    const std::string text = dumpJsonCompactLeaves(value, 2, 40);
    EXPECT_NE(text.find("栅栏"), std::string::npos);
    EXPECT_EQ(text.find("\\u"), std::string::npos);
    for (size_t start = 0; start < text.size();) {
        const size_t end  = text.find('\n', start);
        const size_t stop = end == std::string::npos ? text.size() : end;
        const size_t width = stop - start;
        // The break inserts a comma, so a packed line may be one byte past the column.
        EXPECT_LE(width, 41u) << text.substr(start, width);
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
}

} // namespace
} // namespace ya
