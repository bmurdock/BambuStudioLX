#define CATCH_CONFIG_NO_WINDOWS_SEH
#include <catch_main.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"
#include <boost/filesystem/path.hpp>

namespace {
class ResourceDirectory : public Catch::TestEventListenerBase {
    std::string previous;
public:
    using Catch::TestEventListenerBase::TestEventListenerBase;
    void testRunStarting(const Catch::TestRunInfo &) override {
        previous = Slic3r::resources_dir();
        const auto root = boost::filesystem::path(TEST_DATA_DIR).parent_path().parent_path();
        Slic3r::set_resources_dir((root / "resources").string());
    }
    void testRunEnded(const Catch::TestRunStats &) override {
        Slic3r::set_resources_dir(previous);
    }
};
}
CATCH_REGISTER_LISTENER(ResourceDirectory)
