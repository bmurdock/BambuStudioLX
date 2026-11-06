#include <catch2/catch.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "test_data.hpp"

#include <algorithm>
using namespace Slic3r;
using namespace Slic3r::Test;

namespace {

DynamicPrintConfig multi_filament_config(unsigned int count)
{
    PresetBundle presets;
    presets.set_num_filaments(count);
    return presets.full_config();
}

bool has_feature(const std::string &gcode, const char *feature)
{
    GCodeReader reader;
    GCodeConfig mode;
    bool active = false;
    bool emitted = false;
    reader.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.cmd_is("M82") || line.cmd_is("M83")) {
            mode.use_relative_e_distances.value = line.cmd_is("M83");
            self.apply_config(mode);
        }
        std::string_view comment = line.comment();
        const auto first = comment.find_first_not_of(" \t");
        if (first != std::string_view::npos) comment.remove_prefix(first);
        constexpr std::string_view prefix = "FEATURE: ";
        if (comment.substr(0, prefix.size()) == prefix) {
            auto role = comment.substr(prefix.size());
            const auto last = role.find_last_not_of(" \t\r\n");
            role = last == std::string_view::npos ? std::string_view{} : role.substr(0, last + 1);
            active = role == feature;
        }
        const bool planar_move = line.dist_XY(self) > 0 ||
            ((line.cmd_is("G2") || line.cmd_is("G3")) &&
                ((line.has_i() && line.i() != 0) || (line.has_j() && line.j() != 0)));
        if (active && planar_move && line.extruding(self)) emitted = true;
    });
    return emitted;
}

struct SpiralRaftGCodeResult
{
    double first_model_extrusion_z { -1. };
    double highest_z_before_model  { 0. };
    double nominal_model_layer_z   { 0. };
    size_t layer_z_restores        { 0 };
    bool   model_z_is_continuous   { true };
};

SpiralRaftGCodeResult spiral_raft_gcode_result(const std::string &change_filament_gcode)
{
    DynamicPrintConfig config = multi_filament_config(2);
    config.set_key_value("filament_colour", new ConfigOptionStrings({ "#00AE42", "#AE4200" }));
    config.set_key_value("filament_diameter", new ConfigOptionFloats({ 1.75, 1.75 }));
    config.set_key_value("filament_ids", new ConfigOptionStrings({ "logical-1", "logical-2" }));
    config.set_key_value("filament_map", new ConfigOptionInts({ 1, 1 }));
    config.set_key_value("filament_nozzle_map", new ConfigOptionInts({ 1, 1 }));
    config.set_key_value("filament_start_gcode", new ConfigOptionStrings({ "", "" }));
    config.set_key_value("filament_end_gcode", new ConfigOptionStrings({ "", "" }));
    REQUIRE(config.option<ConfigOptionStrings>("filament_colour")->size() == 2);
    config.set_deserialize_strict({
        { "spiral_mode",                       true },
        { "wall_loops",                        1 },
        { "top_shell_layers",                  0 },
        { "bottom_shell_layers",               1 },
        { "sparse_infill_density",             "0%" },
        { "raft_layers",                       2 },
        { "support_filament",         2 },
        { "support_interface_filament", 2 },
        { "wall_filament",                1 },
        { "sparse_infill_filament",                   1 },
        { "solid_infill_filament",             1 },
        { "retraction_length",                 0 },
        { "skirt_loops",                        0 },
        { "single_extruder_multi_material",    true },
        { "filament_map_mode",                "Manual" },
        { "flush_volumes_matrix",             "0,280,280,0" },
        { "enable_prime_tower",                false },
        { "machine_start_gcode",                       "T[initial_tool]\n" },
        { "change_filament_gcode",             change_filament_gcode }
    });

    Slic3r::Print print;
    Slic3r::Model model;
    Slic3r::Test::init_print({ TestMesh::cube_20x20x20 }, print, model, config);
    std::string gcode;
    REQUIRE_NOTHROW(gcode = Slic3r::Test::gcode(print));

    SpiralRaftGCodeResult result;
    result.nominal_model_layer_z = print.objects().front()->layers().front()->print_z;
    bool                  raft_seen = false;
    bool                  model_role_seen = false;
    double                last_model_extrusion_z = -1.;
    GCodeReader           reader;
    reader.apply_config(config);
    reader.parse_buffer(gcode, [&result, &raft_seen, &model_role_seen, &last_model_extrusion_z](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        const std::string_view comment = line.comment();
        raft_seen |= comment.find("FEATURE: Support") != std::string_view::npos;
        if (comment.find("FEATURE: ") != std::string_view::npos)
            model_role_seen = comment.find("FEATURE: Support") == std::string_view::npos &&
                              comment.find("FEATURE: Skirt") == std::string_view::npos &&
                              comment.find("FEATURE: Brim") == std::string_view::npos &&
                              comment.find("FEATURE: Prime tower") == std::string_view::npos;

        if (result.first_model_extrusion_z < 0.) {
            result.highest_z_before_model = std::max(result.highest_z_before_model, static_cast<double>(self.z()));
            if ((line.cmd_is("G0") || line.cmd_is("G1")) && line.has_z() &&
                self.z() > result.nominal_model_layer_z + EPSILON &&
                std::abs(line.z() - result.nominal_model_layer_z) < EPSILON)
                ++result.layer_z_restores;
            if (raft_seen && model_role_seen && line.extruding(self) && line.dist_XY(self) > 0.)
                result.first_model_extrusion_z = self.z();
        }

        if (raft_seen && model_role_seen && line.extruding(self) && line.dist_XY(self) > 0.) {
            if (last_model_extrusion_z >= 0.) {
                double z_step = self.z() - last_model_extrusion_z;
                result.model_z_is_continuous &= z_step >= -EPSILON && z_step <= 0.21;
            }
            last_model_extrusion_z = self.z();
        }
    });
    return result;
}

} // namespace

TEST_CASE("Feature detection requires extrusion under the exact role", "[PrintGCode]")
{
    CHECK_FALSE(has_feature("; FEATURE: Support\n", "Support"));
    CHECK_FALSE(has_feature("M83\n; FEATURE: Support interface\nG1 X10 E1\n", "Support"));
    CHECK_FALSE(has_feature("M83\n; FEATURE: Support\nG1 E1\nG1 X10 E-1\n", "Support"));
    CHECK(has_feature("M83\n; FEATURE: Support\nG1 X10 E1\n", "Support"));
    CHECK(has_feature("M82\nG92 E5\n; FEATURE: Support\nG1 X10 E6\n", "Support"));
    CHECK_FALSE(has_feature("M82\nG92 E5\n; FEATURE: Support\nG1 X10 E4\n", "Support"));
    CHECK(has_feature("M83\n; FEATURE: Support\nG2 I5 J0 E1\n", "Support"));
}

SCENARIO( "PrintGCode basic functionality", "[PrintGCode]") {
    GIVEN("A default configuration and a print test object") {
        WHEN("the output is executed with no support material") {
            Slic3r::Print print;
            Slic3r::Model model;
            Slic3r::Test::init_print({TestMesh::cube_20x20x20}, print, model, {
                { "layer_height",					0.2 },
                { "initial_layer_print_height",				0.2 },
                { "initial_layer_line_width",	0 },
                { "machine_start_gcode",					"" }
                });
            std::string gcode = Slic3r::Test::gcode(print);
            THEN("Some text output is generated.") {
                REQUIRE(gcode.size() > 0);
            }
            THEN("Exported text contains slic3r version") {
                REQUIRE(gcode.find(SLIC3R_VERSION) != std::string::npos);
            }
            //THEN("Exported text contains git commit id") {
            //    REQUIRE(gcode.find("; Git Commit") != std::string::npos);
            //    REQUIRE(gcode.find(SLIC3R_BUILD_ID) != std::string::npos);
            //}
            THEN("Exported text contains extrusion statistics.") {
                REQUIRE(has_feature(gcode, "Outer wall"));
                REQUIRE(has_feature(gcode, "Inner wall"));
                REQUIRE(has_feature(gcode, "Sparse infill"));
                REQUIRE(has_feature(gcode, "Internal solid infill"));
                REQUIRE(has_feature(gcode, "Top surface"));
                REQUIRE_FALSE(has_feature(gcode, "Support"));
            }
            THEN("Exported text does not contain cooling markers (they were consumed)") {
                REQUIRE(gcode.find(";_EXTRUDE_SET_SPEED") == std::string::npos);
            }

            THEN("GCode preamble is emitted.") {
                REQUIRE(gcode.find("G21\n") != std::string::npos);
            }

            THEN("Config options emitted for print config, default region config, default object config") {
                REQUIRE(gcode.find("; nozzle_temperature_initial_layer") != std::string::npos);
                REQUIRE(gcode.find("; layer_height") != std::string::npos);
                REQUIRE(gcode.find("; sparse_infill_density") != std::string::npos);
            }
            THEN("Infill is emitted.") {
                REQUIRE(has_feature(gcode, "Sparse infill"));
            }
            THEN("Perimeters are emitted.") {
                REQUIRE(has_feature(gcode, "Inner wall"));
            }
            THEN("Skirt is emitted.") {
                REQUIRE(has_feature(gcode, "Skirt"));
            }
            THEN("final Z height is 20mm") {
                double final_z = 0.0;
                GCodeReader reader;
                reader.apply_config(print.config());
                reader.parse_buffer(gcode, [&final_z] (GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    final_z = std::max<double>(final_z, static_cast<double>(self.z())); // record the highest Z point we reach
                });
                REQUIRE(final_z == Approx(20.));
            }
        }
        WHEN("output is executed with complete objects and two differently-sized meshes") {
            Slic3r::Print print;
            Slic3r::Model model;
            Slic3r::Test::init_print({TestMesh::cube_20x20x20,TestMesh::cube_20x20x20}, print, model, {
                { "initial_layer_line_width",       0 },
                { "initial_layer_print_height",             0.3 },
                { "layer_height",                   0.2 },
                { "enable_support",               false },
                { "raft_layers",                    0 },
                { "z_hop",                          0 },
                { "print_sequence",                 "by object" },
                { "printing_by_object_gcode",          "; between-object-gcode" }
                });
            std::string gcode = Slic3r::Test::gcode(print);
            THEN("Some text output is generated.") {
                REQUIRE(gcode.size() > 0);
            }
            THEN("Infill is emitted.") {
                REQUIRE(has_feature(gcode, "Sparse infill"));
            }
            THEN("Perimeters are emitted.") {
                REQUIRE(has_feature(gcode, "Inner wall"));
            }
            THEN("Skirt is emitted.") {
                REQUIRE(has_feature(gcode, "Skirt"));
            }
            THEN("Between-object-gcode is emitted.") {
                REQUIRE(gcode.find("; between-object-gcode") != std::string::npos);
            }
            THEN("final Z height is 20mm") {
                double final_z = 0.0;
                GCodeReader reader;
                reader.apply_config(print.config());
                reader.parse_buffer(gcode, [&final_z] (GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    final_z = std::max(final_z, static_cast<double>(self.z())); // record the highest Z point we reach
                });
                REQUIRE(final_z == Approx(20.1));
            }
            THEN("Z height resets on object change") {
                double final_z = 0.0;
                bool reset = false;
                GCodeReader reader;
                reader.apply_config(print.config());
                reader.parse_buffer(gcode, [&final_z, &reset] (GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    if (final_z > 0 && std::abs(self.z() - 0.3) < 0.01 ) { // saw higher Z before this, now it's lower
                        reset = true;
                    } else {
                        final_z = std::max(final_z, static_cast<double>(self.z())); // record the highest Z point we reach
                    }
                });
                REQUIRE(reset == true);
            }
            THEN("Shorter object is printed before taller object.") {
                double final_z = 0.0;
                bool reset = false;
                GCodeReader reader;
                reader.apply_config(print.config());
                reader.parse_buffer(gcode, [&final_z, &reset] (GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    if (final_z > 0 && std::abs(self.z() - 0.3) < 0.01 ) { 
                        reset = (final_z > 20.0);
                    } else {
                        final_z = std::max(final_z, static_cast<double>(self.z())); // record the highest Z point we reach
                    }
                });
                REQUIRE(reset == true);
            }
        }
        WHEN("the output is executed with support material") {
            std::string gcode = ::Test::slice({TestMesh::cube_20x20x20}, {
                { "initial_layer_line_width",       0 },
                { "enable_support",               true },
                { "raft_layers",                    3 },
                });
            THEN("Some text output is generated.") {
                REQUIRE(gcode.size() > 0);
            }
            THEN("Exported text contains extrusion statistics.") {
                REQUIRE(has_feature(gcode, "Outer wall"));
                REQUIRE(has_feature(gcode, "Inner wall"));
                REQUIRE(has_feature(gcode, "Sparse infill"));
                REQUIRE(has_feature(gcode, "Internal solid infill"));
                REQUIRE(has_feature(gcode, "Top surface"));
                REQUIRE(has_feature(gcode, "Support"));
            }
            THEN("Raft is emitted.") {
                REQUIRE(has_feature(gcode, "Support"));
            }
        }
        WHEN("the output is executed with a separate first layer extrusion width") {
			std::string gcode = ::Test::slice({ TestMesh::cube_20x20x20 }, {
                { "initial_layer_line_width", "0.5" }
                });
            THEN("Some text output is generated.") {
                REQUIRE(gcode.size() > 0);
            }
            THEN("Exported text contains extrusion statistics.") {
                REQUIRE(has_feature(gcode, "Outer wall"));
                REQUIRE(has_feature(gcode, "Inner wall"));
                REQUIRE(has_feature(gcode, "Sparse infill"));
                REQUIRE(has_feature(gcode, "Internal solid infill"));
                REQUIRE(has_feature(gcode, "Top surface"));
                REQUIRE(gcode.find("; initial_layer_line_width") != std::string::npos);
            }
        }
        WHEN("Cooling is enabled and the fan is disabled.") {
			std::string gcode = ::Test::slice({ TestMesh::cube_20x20x20 }, {
				{ "cooling",                    true },
                { "disable_fan_first_layers",   5 }
                });
            THEN("GCode to disable fan is emitted."){
                REQUIRE(gcode.find("M106 S0") != std::string::npos);
            }
        }
        WHEN("end_gcode exists with layer_num and layer_z") {
			std::string gcode = ::Test::slice({ TestMesh::cube_20x20x20 }, {
				{ "machine_end_gcode",              "; Layer_num [layer_num]\n; Layer_z [layer_z]" },
                { "layer_height",           0.1 },
                { "initial_layer_print_height",     0.1 }
                });
            THEN("layer_num and layer_z are processed in the end gcode") {
                REQUIRE(gcode.find("; Layer_num 199") != std::string::npos);
                REQUIRE(gcode.find("; Layer_z 20") != std::string::npos);
            }
        }
        WHEN("current_extruder exists in start_gcode") {
            {
				std::string gcode = ::Test::slice({ TestMesh::cube_20x20x20 }, {
					{ "machine_start_gcode", "; Extruder [current_extruder]" }
                });
                THEN("current_extruder is processed in the start gcode and set for first extruder") {
                    REQUIRE(gcode.find("; Extruder 0") != std::string::npos);
                }
            }
			{
                DynamicPrintConfig config = multi_filament_config(4);
                config.set_key_value("filament_colour", new ConfigOptionStrings({ "#00AE42", "#AE4200", "#4200AE", "#AE0042" }));
                config.set_key_value("filament_diameter", new ConfigOptionFloats({ 1.75, 1.75, 1.75, 1.75 }));
                config.set_key_value("filament_ids", new ConfigOptionStrings({ "logical-1", "logical-2", "logical-3", "logical-4" }));
                config.set_key_value("filament_map", new ConfigOptionInts({ 1, 1, 1, 1 }));
                config.set_key_value("filament_nozzle_map", new ConfigOptionInts({ 1, 1, 1, 1 }));
                config.set_key_value("filament_start_gcode", new ConfigOptionStrings({ "", "", "", "" }));
                config.set_key_value("filament_end_gcode", new ConfigOptionStrings({ "", "", "", "" }));
                REQUIRE(config.option<ConfigOptionStrings>("filament_colour")->size() == 4);
                config.set_deserialize_strict({
                    { "machine_start_gcode",                    "; Extruder [current_extruder]" },
                    { "single_extruder_multi_material",    true },
                    { "filament_map_mode",                "Manual" },
                    { "enable_prime_tower",                false },
                    { "skirt_loops",                        0 },
                    { "sparse_infill_filament",                2 },
                    { "solid_infill_filament",          2 },
                    { "wall_filament",             2 },
                    { "support_filament",      2 },
                    { "support_interface_filament", 2 }
                });
                Slic3r::Model model;
                Slic3r::Print print;
                Slic3r::Test::init_print({TestMesh::cube_20x20x20}, print, model, config);
                model.objects.front()->volumes.front()->config.set_key_value("extruder", new ConfigOptionInt(2));
                print.apply(model, config);
                print.validate();
                const std::vector<unsigned int> used_extruders = print.objects().front()->object_extruders();
                REQUIRE(used_extruders.size() == 1);
                REQUIRE(used_extruders.front() == 1);
                CAPTURE(config.opt<ConfigOptionStrings>("filament_colour")->values,
                    print.config().filament_colour.values, print.config().filament_diameter.values,
                    print.config().filament_map.values, print.config().filament_nozzle_map.values);
                std::string gcode;
                REQUIRE_NOTHROW(gcode = Slic3r::Test::gcode(print));
                THEN("current_extruder is processed in the start gcode and set for second extruder") {
                    const auto marker = gcode.find("; Extruder ");
                    INFO("actual current-extruder marker: " << (marker == std::string::npos ?
                        "missing" : gcode.substr(marker, gcode.find('\n', marker) - marker)));
                    REQUIRE(gcode.find("; Extruder 1") != std::string::npos);
                }
            }
        }

        WHEN("layer_num represents the layer's index from z=0") {
			std::string gcode = ::Test::slice({ TestMesh::cube_20x20x20, TestMesh::cube_20x20x20 }, {
				{ "print_sequence",                 "by object" },
                { "layer_change_gcode",                    ";Layer:[layer_num] ([layer_z] mm)" },
                { "layer_height",                   0.1 },
                { "initial_layer_print_height",             0.1 }
                });
			// End of the 1st object.
            std::string token = ";Layer:199 ";
			size_t pos = gcode.find(token);
			THEN("First and second object last layer is emitted") {
				// First object
				REQUIRE(pos != std::string::npos);
				pos += token.size();
				REQUIRE(pos < gcode.size());
				double z = 0;
				REQUIRE((sscanf(gcode.data() + pos, "(%lf mm)", &z) == 1));
				REQUIRE(z == Approx(20.));
				// Second object
				pos = gcode.find(";Layer:399 ", pos);
				REQUIRE(pos != std::string::npos);
				pos += token.size();
				REQUIRE(pos < gcode.size());
				REQUIRE((sscanf(gcode.data() + pos, "(%lf mm)", &z) == 1));
				REQUIRE(z == Approx(20.));
			}
        }
    }
}

TEST_CASE("Spiral vase restores object layer Z after a raft tool change", "[PrintGCode][SpiralVase]")
{
    SECTION("tool change ends at an elevated clearance Z") {
        SpiralRaftGCodeResult result = spiral_raft_gcode_result("G1 X5 F12000\nG1 Z{max_layer_z + 3.0} F1200\nT[next_extruder]\n");

        REQUIRE(result.first_model_extrusion_z == Approx(result.nominal_model_layer_z));
        REQUIRE(result.highest_z_before_model >= result.nominal_model_layer_z + 2.);
        REQUIRE(result.layer_z_restores == 1);
    }

    SECTION("tool change does not alter Z") {
        SpiralRaftGCodeResult result = spiral_raft_gcode_result("T[next_extruder]\n");

        REQUIRE(result.first_model_extrusion_z == Approx(result.nominal_model_layer_z));
        REQUIRE(result.layer_z_restores == 0);
        REQUIRE(result.model_z_is_continuous);
    }

    SECTION("tool change leaves the XY position unknown at clearance Z") {
        SpiralRaftGCodeResult result = spiral_raft_gcode_result("G1 Z{max_layer_z + 3.0} F1200\nT[next_extruder]\n");

        REQUIRE(result.first_model_extrusion_z == Approx(result.nominal_model_layer_z));
        REQUIRE(result.highest_z_before_model >= result.nominal_model_layer_z + 2.);
        REQUIRE(result.layer_z_restores == 1);
    }
}
