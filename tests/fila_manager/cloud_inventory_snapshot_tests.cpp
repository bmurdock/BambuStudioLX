#include <catch2/catch.hpp>
#include "slic3r/GUI/fila_manager/CloudInventorySnapshot.h"

using Slic3r::GUI::CloudInventorySnapshot;
using nlohmann::json;

static json page(int first, int count, int total)
{
    json hits = json::array();
    for (int i = first; i < first + count; ++i) hits.push_back({{"id", i}});
    return {{"hits", hits}, {"total", total}};
}

TEST_CASE("Cloud snapshot fetches beyond the first twenty spools", "[fila_manager][paging]")
{
    CloudInventorySnapshot snapshot;
    REQUIRE_FALSE(snapshot.append(page(1, 20, 45)));
    REQUIRE(snapshot.offset() == 20);
    REQUIRE_FALSE(snapshot.append(page(21, 20, 45)));
    REQUIRE(snapshot.offset() == 40);
    REQUIRE(snapshot.append(page(41, 5, 45)));
    REQUIRE(snapshot.items().size() == 45);
    REQUIRE(snapshot.items().back().at("id") == 45);
}

TEST_CASE("Cloud snapshot rejects incomplete and malformed inventory", "[fila_manager][paging]")
{
    CloudInventorySnapshot snapshot;
    SECTION("unrecognized response") { REQUIRE_THROWS(snapshot.append({{"raw", "invalid"}})); }
    SECTION("missing ids") { REQUIRE_THROWS(snapshot.append({{"hits", json::array({{{"name", "spool"}}})}})); }
    SECTION("empty page while more records remain") { REQUIRE_THROWS(snapshot.append(page(1, 0, 25))); }
    SECTION("repeated page") {
        REQUIRE_FALSE(snapshot.append(page(1, 20, 25)));
        REQUIRE_THROWS(snapshot.append(page(1, 20, 25)));
    }
    SECTION("total changes") {
        REQUIRE_FALSE(snapshot.append(page(1, 20, 25)));
        REQUIRE_THROWS(snapshot.append(page(21, 5, 26)));
    }
    SECTION("valid empty snapshot") { REQUIRE(snapshot.append(page(1, 0, 0))); }
    SECTION("legacy arrays need another page when full") {
        REQUIRE_FALSE(snapshot.append(page(1, 20, 20).at("hits")));
        REQUIRE(snapshot.append(json::array()));
        REQUIRE(snapshot.items().size() == 20);
    }
}
