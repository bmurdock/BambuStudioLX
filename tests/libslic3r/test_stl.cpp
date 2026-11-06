#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <csignal>
#include <sys/resource.h>
#endif

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Format/STL.hpp"

using namespace Slic3r;

static inline std::string stl_path(const char* path)
{
	return std::string(TEST_DATA_DIR) + "/test_stl/" + path;
}

#if defined(__APPLE__) || defined(__linux__)
struct ScopedFileSizeLimit
{
	struct rlimit previous_limit{};
	struct sigaction previous_action{};
	bool active{false};

	bool enable()
	{
		if (getrlimit(RLIMIT_FSIZE, &previous_limit) != 0 ||
			sigaction(SIGXFSZ, nullptr, &previous_action) != 0)
			return false;

		struct sigaction ignored{};
		sigemptyset(&ignored.sa_mask);
		ignored.sa_handler = SIG_IGN;
		if (sigaction(SIGXFSZ, &ignored, nullptr) != 0)
			return false;

		struct rlimit limited = previous_limit;
		limited.rlim_cur = 0;
		if (setrlimit(RLIMIT_FSIZE, &limited) != 0) {
			sigaction(SIGXFSZ, &previous_action, nullptr);
			return false;
		}
		active = true;
		return true;
	}

	~ScopedFileSizeLimit()
	{
		if (!active)
			return;
		setrlimit(RLIMIT_FSIZE, &previous_limit);
		sigaction(SIGXFSZ, &previous_action, nullptr);
	}
};

struct ScopedTempFiles
{
	std::vector<boost::filesystem::path> paths;

	~ScopedTempFiles()
	{
		boost::system::error_code ec;
		for (const auto &path : paths)
			boost::filesystem::remove(path, ec);
	}
};
#endif

SCENARIO("Reading an STL file", "[stl]") {
	GIVEN("umlauts in the path of a binary STL file, Czech characters in the file name") {
        WHEN("STL file is read") {
			Slic3r::Model model;
			THEN("load should succeed") {
                REQUIRE(Slic3r::load_stl(stl_path("Geräte/20mmbox-čřšřěá.stl").c_str(), &model));
				REQUIRE(is_approx(model.objects.front()->volumes.front()->mesh().size(), Vec3d(20, 20, 20)));
            }
        }
    }
	GIVEN("in ASCII format") {
		WHEN("line endings LF") {
			Slic3r::Model model;
			THEN("load should succeed") {
				REQUIRE(Slic3r::load_stl(stl_path("ASCII/20mmbox-LF.stl").c_str(), &model));
				REQUIRE(is_approx(model.objects.front()->volumes.front()->mesh().size(), Vec3d(20, 20, 20)));
			}
		}
		WHEN("line endings CRLF") {
			Slic3r::Model model;
			THEN("load should succeed") {
				REQUIRE(Slic3r::load_stl(stl_path("ASCII/20mmbox-CRLF.stl").c_str(), &model));
				REQUIRE(is_approx(model.objects.front()->volumes.front()->mesh().size(), Vec3d(20, 20, 20)));
			}
		}
#if 0
		// ASCII STLs ending with just carriage returns are not supported. These were used by the old Macs, while the Unix based MacOS uses LFs as any other Unix.
		WHEN("line endings CR") {
			Slic3r::Model model;
			THEN("load should succeed") {
				REQUIRE(Slic3r::load_stl(stl_path("ASCII/20mmbox-CR.stl").c_str(), &model));
				REQUIRE(is_approx(model.objects.front()->volumes.front()->mesh().size(), Vec3d(20, 20, 20)));
			}
		}

#endif
		WHEN("nonstandard STL file (text after ending tags, invalid normals, for example infinities)") {
			Slic3r::Model model;
			THEN("load should succeed") {
				REQUIRE(Slic3r::load_stl(stl_path("ASCII/20mmbox-nonstandard.stl").c_str(), &model));
				REQUIRE(is_approx(model.objects.front()->volumes.front()->mesh().size(), Vec3d(20, 20, 20)));
			}
		}
	}
}

SCENARIO("Writing an STL file reports output failures", "[stl]") {
	Slic3r::TriangleMesh mesh;

	GIVEN("a path whose parent directory does not exist") {
		const boost::filesystem::path output = boost::filesystem::temp_directory_path() /
			boost::filesystem::unique_path("stl-write-failure-%%%%%%") / "output.stl";
		THEN("both STL formats report the open failure") {
			REQUIRE_FALSE(Slic3r::store_stl(output.string().c_str(), &mesh, false));
			REQUIRE_FALSE(Slic3r::store_stl(output.string().c_str(), &mesh, true));
		}
	}

#if defined(__APPLE__) || defined(__linux__)
	GIVEN("a zero POSIX file-size limit") {
		ScopedFileSizeLimit file_size_limit;
		if (!file_size_limit.enable()) {
			WARN("RLIMIT_FSIZE or SIGXFSZ is unavailable");
			return;
		}

		ScopedTempFiles temp_files;
		const boost::filesystem::path ascii_output = boost::filesystem::temp_directory_path() /
			boost::filesystem::unique_path("stl-write-limit-%%%%%%.stl");
		const boost::filesystem::path binary_output = boost::filesystem::temp_directory_path() /
			boost::filesystem::unique_path("stl-write-limit-%%%%%%.stl");
		temp_files.paths = { ascii_output, binary_output };

		THEN("both STL formats report the write failure") {
			REQUIRE_FALSE(Slic3r::store_stl(ascii_output.string().c_str(), &mesh, false));
			REQUIRE_FALSE(Slic3r::store_stl(binary_output.string().c_str(), &mesh, true));
		}
	}
#endif
}
