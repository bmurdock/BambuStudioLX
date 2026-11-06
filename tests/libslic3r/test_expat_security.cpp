#include <catch2/catch.hpp>

#define XML_GE 1
#include <expat.h>

#include <string>

namespace {

struct ParserOwner
{
    XML_Parser parser{XML_ParserCreate(nullptr)};

    ~ParserOwner()
    {
        if (parser != nullptr)
            XML_ParserFree(parser);
    }
};

void XMLCALL count_character_data(void *user_data, const XML_Char *, int length)
{
    auto *count = static_cast<std::size_t *>(user_data);
    *count += static_cast<std::size_t>(length);
}

std::string billion_laughs_fixture()
{
    const std::string e0 = "0123456789";
    const std::string e1 = [&] {
        std::string value;
        for (int i = 0; i < 50; ++i) value += "&e0;";
        return value;
    }();
    const std::string e2 = [&] {
        std::string value;
        for (int i = 0; i < 50; ++i) value += "&e1;";
        return value;
    }();
    return "<!DOCTYPE root [<!ENTITY e0 \"" + e0 + "\"><!ENTITY e1 \"" + e1 + "\"><!ENTITY e2 \"" + e2 + "\">]><root>&e2;</root>";
}

} // namespace

TEST_CASE("Expat parser reset clears large attribute accounting", "[expat][security]")
{
    ParserOwner owner;
    REQUIRE(owner.parser != nullptr);

    const std::string large_attribute = "<root value=\"" + std::string(70 * 1024 * 1024, 'x') + "\"/>";
    REQUIRE(XML_Parse(owner.parser, large_attribute.data(), static_cast<int>(large_attribute.size()), XML_TRUE) == XML_STATUS_OK);

    for (int iteration = 0; iteration < 3; ++iteration) {
        REQUIRE(XML_ParserReset(owner.parser, nullptr) == XML_TRUE);
        REQUIRE(XML_Parse(owner.parser, "<root/>", 7, XML_TRUE) == XML_STATUS_OK);
    }
}

TEST_CASE("Expat rejects entity amplification before character callbacks", "[expat][security]")
{
    ParserOwner owner;
    REQUIRE(owner.parser != nullptr);
    REQUIRE(XML_SetBillionLaughsAttackProtectionMaximumAmplification(owner.parser, 2.0) == XML_TRUE);
    REQUIRE(XML_SetBillionLaughsAttackProtectionActivationThreshold(owner.parser, 1024) == XML_TRUE);

    std::size_t character_data_bytes = 0;
    XML_SetCharacterDataHandler(owner.parser, count_character_data);
    XML_SetUserData(owner.parser, &character_data_bytes);
    const std::string input = billion_laughs_fixture();
    REQUIRE(XML_Parse(owner.parser, input.data(), static_cast<int>(input.size()), XML_TRUE) == XML_STATUS_ERROR);
    REQUIRE(XML_GetErrorCode(owner.parser) == XML_ERROR_AMPLIFICATION_LIMIT_BREACH);
    REQUIRE(character_data_bytes == 0);
}
