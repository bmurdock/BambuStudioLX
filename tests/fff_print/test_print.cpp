#include <catch2/catch.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Print.hpp"
#include "libslic3r/Layer.hpp"

#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

SCENARIO("PrintObject: Perimeter generation", "[PrintObject]") {
    GIVEN("20mm cube with explicit layer height and wall loops") {
        WHEN("make_perimeters() is called")  {
            Slic3r::Print print;
            Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, {
                { "sparse_infill_density", 0 },
                { "layer_height", 0.25 },
                { "initial_layer_print_height", 0.25 },
                { "wall_loops", 3 },
                { "top_one_wall_type", "not apply" }
            });
			const PrintObject &object = *print.objects().front();
			THEN("80 layers exist in the model") {
                REQUIRE(object.layers().size() == 80);
            }
            THEN("Every layer in region 0 has 1 island of perimeters") {
                for (const Layer *layer : object.layers())
                    REQUIRE(layer->regions().front()->perimeters.entities.size() == 1);
            }
            THEN("Every layer in region 0 has 3 paths in its perimeters list.") {
                for (const Layer *layer : object.layers())
                    REQUIRE(layer->regions().front()->perimeters.items_count() == 3);
            }
        }
    }
}

SCENARIO("Print: Skirt generation", "[Print]") {
    GIVEN("20mm cube and default config") {
        WHEN("Skirts is set to 2 loops")  {
            Slic3r::Print print;
            Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, {
            	{ "skirt_height", 	1 },
        		{ "skirt_distance", 1 },
                { "skirt_loops", 		2 }
            });
            THEN("Skirt Extrusion collection has 2 loops in it") {
                REQUIRE(print.skirt().items_count() == 2);
                REQUIRE(print.skirt().flatten().entities.size() == 2);
            }
        }
    }
}

SCENARIO("Print: Changing number of solid surfaces does not cause all surfaces to become internal.", "[Print]") {
    GIVEN("sliced 20mm cube and config with top shell layers = 2 and bottom shell layers = 1") {
        Slic3r::DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
		config.set_deserialize_strict({
			{ "top_shell_layers",		2 },
			{ "bottom_shell_layers",	1 },
			{ "layer_height",			0.25 }, // get a known number of layers
			{ "initial_layer_print_height",		0.25 }
			});
        Slic3r::Print print;
        Slic3r::Model model;
        Slic3r::Test::init_print({TestMesh::cube_20x20x20}, print, model, config);
        // Precondition: Ensure that the model has 2 solid top layers (39, 38)
        // and one solid bottom layer (0).
		auto test_is_solid_infill = [&print](size_t obj_id, size_t layer_id) {
		    const Layer &layer = *(print.objects().at(obj_id)->get_layer((int)layer_id));
		    // iterate over all of the regions in the layer
		    for (const LayerRegion *region : layer.regions()) {
		        // for each region, iterate over the fill surfaces
		        for (const Surface &surface : region->fill_surfaces.surfaces)
		            CHECK(surface.is_solid());
		    }
		};
        print.process();
        test_is_solid_infill(0,  0); // should be solid
        test_is_solid_infill(0, 79); // should be solid
        test_is_solid_infill(0, 78); // should be solid
        WHEN("Model is re-sliced with top_shell_layers == 3") {
			config.set("top_shell_layers", 3);
			print.apply(model, config);
            print.process();
            THEN("Print object does not have 0 solid bottom layers.") {
                test_is_solid_infill(0, 0);
            }
            AND_THEN("Print object has 3 top solid layers") {
                test_is_solid_infill(0, 79);
                test_is_solid_infill(0, 78);
                test_is_solid_infill(0, 77);
            }
        }
    }
}

SCENARIO("Print: Brim generation", "[Print]") {

    auto check_brim_width = [](Print &print, double requested_width) {
        REQUIRE(!print.get_brimMap().empty());
        Polygons coverage;
        for (const auto &entry : print.get_brimMap())
            entry.second.polygons_covered_by_spacing(coverage, float(SCALED_EPSILON));
        REQUIRE(!coverage.empty());
        const BoundingBox brim = get_extents(coverage);
        BoundingBox object;
        for (const PrintObject *printed : print.objects())
            for (const PrintInstance &instance : printed->instances())
                for (const ExPolygon &island : printed->get_layer(0)->lslices) {
                    Polygon contour = island.contour;
                    contour.translate(instance.shift_without_plate_offset());
                    object.merge(get_extents(contour));
                }
        REQUIRE(object.defined);
        // Current brims connect neighboring rings in pairs. Their outer edge
        // can fall short by less than two spacings when the width is quantized.
        const double spacing = print.brim_flow().spacing();
        CAPTURE(brim.min, brim.max, object.min, object.max, spacing, requested_width);
        for (const coord_t extension : {object.min.x() - brim.min.x(), brim.max.x() - object.max.x(),
                 object.min.y() - brim.min.y(), brim.max.y() - object.max.y()}) {
            const double extent = unscale<double>(extension);
            REQUIRE(extent > 0.);
            REQUIRE(extent >= requested_width - 2. * spacing);
            REQUIRE(extent <= requested_width + spacing);
        }
    };
    GIVEN("20mm cube with forced outer brim, 1mm first layer width") {
        WHEN("Brim is set to 3mm")  {
	        Slic3r::Print print;
	        Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, {
                { "initial_layer_line_width", 	1 },
                { "brim_type", 					"outer_only" },
                { "brim_object_gap", 				0 },
	        	{ "brim_width", 					3 }
	        });
            THEN("Brim covers the requested 3mm extent") {
                REQUIRE(!print.get_brimMap().empty());
                check_brim_width(print, 3.);
            }
        }
        WHEN("Brim is set to 6mm")  {
	        Slic3r::Print print;
	        Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, {
                { "initial_layer_line_width", 	1 },
                { "brim_type", 					"outer_only" },
                { "brim_object_gap", 				0 },
	        	{ "brim_width", 					6 }
	        });
            THEN("Brim covers the requested 6mm extent") {
                REQUIRE(!print.get_brimMap().empty());
                check_brim_width(print, 6.);
            }
        }
        WHEN("Brim is set to 6mm, extrusion width 0.5mm")  {
	        Slic3r::Print print;
	        Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, {
                { "initial_layer_line_width", 	1 },
                { "brim_type", 					"outer_only" },
                { "brim_object_gap", 				0 },
	        	{ "brim_width", 					6 },
                { "initial_layer_line_width", 	0.5 }
	        });
			print.process();
            THEN("Narrow-line brim covers the requested 6mm extent") {
                REQUIRE(!print.get_brimMap().empty());
                check_brim_width(print, 6.);
            }
        }
    }
}
