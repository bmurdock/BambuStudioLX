#include <catch2/catch.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Print.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Slicing.hpp"

#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// Exercise layer boundary generation without unrelated extrusion-flow checks.
static std::vector<coordf_t> object_layer_tops(double height, double nozzle)
{
    FullPrintConfig config;
    config.initial_layer_print_height.value = 2.;
    config.layer_height.value = height;
    config.nozzle_diameter.values = {nozzle};
    config.max_layer_height.values = {nozzle};
    const auto parameters = SlicingParameters::create_from_config(config, config, 20., {0});
    const auto profile = layer_height_profile_from_ranges(parameters, {});
    const auto boundaries = generate_object_layers(parameters, profile, false);
    std::vector<coordf_t> tops;
    for (size_t index = 1; index < boundaries.size(); index += 2)
        tops.push_back(boundaries[index]);
    return tops;
}

TEST_CASE("Layer limits use the selected nozzle's bounds", "[PrintObject]") {
    FullPrintConfig config;
    config.layer_height.value = 0.15;
    config.initial_layer_print_height.value = 0.15;
    config.nozzle_diameter.values = {0.4, 0.8};
    config.min_layer_height.values = {0.05, 0.1};
    config.max_layer_height.values = {0.2, 0.6};
    const auto first = SlicingParameters::create_from_config(config, config, 20., {0});
    const auto second = SlicingParameters::create_from_config(config, config, 20., {1});
    const auto both = SlicingParameters::create_from_config(config, config, 20., {0, 1});
    REQUIRE(first.min_layer_height == Approx(0.05));
    REQUIRE(first.max_layer_height == Approx(0.2));
    REQUIRE(second.min_layer_height == Approx(0.1));
    REQUIRE(second.max_layer_height == Approx(0.6));
    REQUIRE(both.min_layer_height == Approx(0.1));
    REQUIRE(both.max_layer_height == Approx(0.2));
}

SCENARIO("PrintObject: object layer heights", "[PrintObject]") {
    GIVEN("20mm cube and default initial config, initial layer height of 2mm") {
        WHEN("generate_object_layers() is called for 2mm layer heights and nozzle diameter of 3mm") {
            const auto layers = object_layer_tops(2, 3);
            THEN("The output vector has 10 entries") {
                REQUIRE(layers.size() == 10);
            }
            AND_THEN("Each layer is approximately 2mm above the previous Z") {
                coordf_t last = 0.0;
                for (size_t i = 0; i < layers.size(); ++ i) {
                    REQUIRE((layers[i] - last) == Approx(2.0));
                    last = layers[i];
                }
            }
        }
        WHEN("generate_object_layers() is called for 10mm layer heights and nozzle diameter of 11mm") {
            const auto layers = object_layer_tops(10, 11);
			THEN("The output vector has 3 entries") {
                REQUIRE(layers.size() == 3);
            }
            AND_THEN("Layer 0 is at 2mm") {
                REQUIRE(layers.front() == Approx(2.0));
            }
            AND_THEN("Layer 1 is at 12mm") {
                REQUIRE(layers[1] == Approx(12.0));
            }
        }
        WHEN("generate_object_layers() is called for 15mm layer heights and nozzle diameter of 16mm") {
            const auto layers = object_layer_tops(15, 16);
			THEN("The output vector has 2 entries") {
                REQUIRE(layers.size() == 2);
            }
            AND_THEN("Layer 0 is at 2mm") {
                REQUIRE(layers[0] == Approx(2.0));
            }
            AND_THEN("Layer 1 is at 17mm") {
                REQUIRE(layers[1] == Approx(17.0));
            }
        }
#if 0
        WHEN("generate_object_layers() is called for 15mm layer heights and nozzle diameter of 5mm") {
            Slic3r::Print print;
            Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, {
                { "initial_layer_print_height", 2 },
				{ "layer_height", 		15 },
	            { "nozzle_diameter", 	5 }
	        });
			const std::vector<Slic3r::Layer*> &layers = print.objects().front()->layers();
			THEN("The layer height is limited to 5mm.") {
                CHECK(layers.size() == 5);
                coordf_t last = 2.0;
                for (size_t i = 1; i < layers.size(); i++) {
                    REQUIRE((layers[i]->print_z - last) == Approx(5.0));
                    last = layers[i]->print_z;
                }
            }
        }
#endif
    }
}
