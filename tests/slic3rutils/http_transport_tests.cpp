#include <catch2/catch.hpp>
#include "slic3r/Utils/Http.hpp"

TEST_CASE("HTTPS-only downloads reject other protocols before connecting", "[http_transport]")
{
    for (const std::string url : {"http://127.0.0.1:1/plugin.zip", "file:///plugin.zip"}) {
        bool completed = false;
        bool failed = false;
        unsigned response_status = 999;
        auto request = Slic3r::Http::get(url);
        request.https_only().timeout_connect(1).timeout_max(1)
            .on_complete([&](std::string, unsigned) { completed = true; })
            .on_error([&](std::string, std::string error, unsigned status) {
                failed = !error.empty();
                response_status = status;
            });
        request.perform_sync();
        REQUIRE_FALSE(completed);
        REQUIRE(failed);
        REQUIRE(response_status == 0);
    }
}
