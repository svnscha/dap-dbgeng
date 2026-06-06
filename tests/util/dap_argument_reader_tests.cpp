// In-process gtests for the DAP argument reader.
#include <gtest/gtest.h>

#include "util/dap_argument_reader.h"
#include "util/dap_message_inspector.h"

namespace
{
namespace reader = dap_dbgeng::util::dap_argument_reader;

TEST(DapArgumentReader, GetArgumentsReturnsObjectOrNull)
{
    const auto with_args = nlohmann::json::parse(R"({"arguments":{"a":1}})");
    EXPECT_TRUE(reader::get_arguments(with_args).is_object());

    const auto without_args = nlohmann::json::parse(R"({"command":"x"})");
    EXPECT_TRUE(reader::get_arguments(without_args).is_null());

    const auto null_args = nlohmann::json::parse(R"({"arguments":null})");
    EXPECT_TRUE(reader::get_arguments(null_args).is_null());
}

TEST(DapArgumentReader, TryGetInt32AcceptsNumberAndNumericString)
{
    const auto args = nlohmann::json::parse(R"({"a":42,"b":"7","c":"nope"})");
    EXPECT_EQ(reader::try_get_int32(args, "a"), 42);
    EXPECT_EQ(reader::try_get_int32(args, "b"), 7);
    EXPECT_FALSE(reader::try_get_int32(args, "c").has_value());
    EXPECT_FALSE(reader::try_get_int32(args, "missing").has_value());
}

TEST(DapArgumentReader, TryGetBooleanAcceptsBoolAndString)
{
    const auto args = nlohmann::json::parse(R"({"a":true,"b":"false","c":"maybe"})");
    EXPECT_EQ(reader::try_get_boolean(args, "a"), true);
    EXPECT_EQ(reader::try_get_boolean(args, "b"), false);
    EXPECT_FALSE(reader::try_get_boolean(args, "c").has_value());
}

TEST(DapArgumentReader, TryGetStringListAcceptsStringOrArray)
{
    const auto single = nlohmann::json::parse(R"({"p":"one"})");
    const auto list = reader::try_get_string_list(single, "p");
    ASSERT_TRUE(list.has_value());
    EXPECT_EQ(*list, (std::vector<std::string>{"one"}));

    const auto array = nlohmann::json::parse(R"({"p":["a",2,"b"]})");
    const auto filtered = reader::try_get_string_list(array, "p");
    ASSERT_TRUE(filtered.has_value());
    EXPECT_EQ(*filtered, (std::vector<std::string>{"a", "b"}));
}

TEST(DapArgumentReader, TryGetCommandLineArgumentsQuotesTokensWithSpaces)
{
    const auto string_form = nlohmann::json::parse(R"({"args":"--flag value"})");
    EXPECT_EQ(reader::try_get_command_line_arguments(string_form), "--flag value");

    const auto array_form = nlohmann::json::parse(R"({"args":["--path","C:\\Program Files\\app","plain"]})");
    EXPECT_EQ(reader::try_get_command_line_arguments(array_form), "--path \"C:\\Program Files\\app\" plain");

    const auto empty = nlohmann::json::parse(R"({"args":[]})");
    EXPECT_FALSE(reader::try_get_command_line_arguments(empty).has_value());
}

TEST(DapArgumentReader, ResolveWorkingDirectory)
{
    const auto set = nlohmann::json::parse(R"({"workingDir":"C:\\work"})");
    EXPECT_EQ(reader::resolve_working_directory(set), "C:\\work");

    const auto blank = nlohmann::json::parse(R"({"workingDir":"   "})");
    EXPECT_EQ(reader::resolve_working_directory(blank), "");

    const auto missing = nlohmann::json::parse(R"({})");
    EXPECT_EQ(reader::resolve_working_directory(missing), "");
}

TEST(DapMessageInspector, ReadsCommandAndSequence)
{
    namespace inspector = dap_dbgeng::util::dap_message_inspector;
    const auto message = nlohmann::json::parse(R"({"command":"initialize","seq":3,"type":"request"})");
    EXPECT_EQ(inspector::try_get_command(message), "initialize");
    EXPECT_EQ(inspector::try_get_sequence(message), 3);

    const auto bad = nlohmann::json::parse(R"({"type":"event"})");
    EXPECT_FALSE(inspector::try_get_command(bad).has_value());
    EXPECT_FALSE(inspector::try_get_sequence(bad).has_value());
}
} // namespace
