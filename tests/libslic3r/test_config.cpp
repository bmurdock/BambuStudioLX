#include <catch2/catch.hpp>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/LocalesUtils.hpp"
#include "libslic3r/BoundingBox.hpp"

#include <cereal/types/polymorphic.hpp>
#include <cereal/types/string.hpp> 
#include <cereal/types/vector.hpp> 
#include <cereal/archives/binary.hpp>

using namespace Slic3r;

TEST_CASE("Enum-vector assignment preserves the value-name mapping", "[Config]") {
    const t_config_enum_values names{{"named", 3}};
    SECTION("non-nullable option") {
        const ConfigOptionEnumsGeneric source(&names, 1, 3);
        ConfigOptionEnumsGeneric target;
        target.set(&source);
        REQUIRE(target.serialize() == "named");
    }
    SECTION("nullable option") {
        const ConfigOptionEnumsGenericNullable source(&names, 1, 3);
        ConfigOptionEnumsGenericNullable target;
        target.set(&source);
        REQUIRE(target.serialize() == "named");
    }
}

TEST_CASE("Static configuration defaults bind enum-vector names", "[Config]") {
    const DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    REQUIRE(config.opt_serialize("extruder_type") == "Direct Drive");
    REQUIRE(config.opt_serialize("z_hop_types") == "Spiral Lift");
}

TEST_CASE("Enum-vector substitution preserves its bound names", "[Config]") {
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    ConfigSubstitutionContext substitutions{ForwardCompatibilitySubstitutionRule::Enable};
    config.set_deserialize("extruder_type", "future-extruder", substitutions);
    REQUIRE_FALSE(substitutions.substitutions.empty());
    REQUIRE(config.opt_serialize("extruder_type") == "Direct Drive");
}

SCENARIO("Generic config validation performs as expected.", "[Config]") {
    GIVEN("A config generated from default options") {
        Slic3r::DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
        WHEN( "outer_wall_line_width is set to 0.8mm, a valid value") {
            config.set_deserialize_strict("outer_wall_line_width", "0.8");
            THEN( "The config is read as valid.") {
                REQUIRE(config.validate().empty());
            }
        }
        WHEN( "outer_wall_line_width is set to -10, an invalid value") {
            config.set("outer_wall_line_width", -10);
            THEN( "Validate returns error") {
                REQUIRE(! config.validate().empty());
            }
        }

        WHEN( "wall_loops is set to -10, an invalid value") {
            config.set("wall_loops", -10);
            THEN( "Validate returns error") {
                REQUIRE(! config.validate().empty());
            }
        }
    }
}

SCENARIO("Config accessor functions perform as expected.", "[Config]") {
    GIVEN("A config generated from default options") {
        Slic3r::DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
        WHEN("A boolean option is set to a boolean value") {
            REQUIRE_NOTHROW(config.set("gcode_add_line_number", true));
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionBool>("gcode_add_line_number")->getBool() == true);
            }
        }
        WHEN("A boolean option is set to a string value representing a 0 or 1") {
            CHECK_NOTHROW(config.set_deserialize_strict("gcode_add_line_number", "1"));
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionBool>("gcode_add_line_number")->getBool() == true);
            }
        }
        WHEN("A boolean option is set to a string value representing something other than 0 or 1") {
            THEN("A BadOptionTypeException exception is thrown.") {
                REQUIRE_THROWS_AS(config.set("gcode_add_line_number", "Z"), BadOptionTypeException);
            }
            AND_THEN("Value is unchanged.") {
                REQUIRE(config.opt<ConfigOptionBool>("gcode_add_line_number")->getBool() == false);
            }
        }
        WHEN("A boolean option is set to an int value") {
            THEN("A BadOptionTypeException exception is thrown.") {
                REQUIRE_THROWS_AS(config.set("gcode_add_line_number", 1), BadOptionTypeException);
            }
        }
        WHEN("A numeric option is set from serialized string") {
            config.set_deserialize_strict("nozzle_temperature", "100");
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionIntsNullable>("nozzle_temperature")->get_at(0) == 100);
            }
        }
#if 0
		//FIXME better design accessors for vector elements.
		WHEN("An integer-based option is set through the integer interface") {
            config.set("nozzle_temperature", 100);
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionIntsNullable>("nozzle_temperature")->get_at(0) == 100);
            }
        }
#endif
        WHEN("An floating-point option is set through the integer interface") {
            config.set("outer_wall_line_width", 10);
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionFloat>("outer_wall_line_width")->getFloat() == 10.0);
            }
        }
        WHEN("A floating-point option is set through the double interface") {
            config.set("outer_wall_line_width", 5.5);
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionFloat>("outer_wall_line_width")->getFloat() == 5.5);
            }
        }
        WHEN("An integer-based option is set through the double interface") {
            THEN("A BadOptionTypeException exception is thrown.") {
                REQUIRE_THROWS_AS(config.set("nozzle_temperature", 5.5), BadOptionTypeException);
            }
        }
        WHEN("A numeric option is set to a non-numeric value.") {
            const double initial_value = config.opt<ConfigOptionFloat>("outer_wall_line_width")->getFloat();
            THEN("A BadOptionTypeException exception is thown.") {
                REQUIRE_THROWS_AS(config.set_deserialize_strict("outer_wall_line_width", "zzzz"), BadOptionValueException);
            }
            THEN("The value does not change.") {
                REQUIRE(config.opt<ConfigOptionFloat>("outer_wall_line_width")->getFloat() == initial_value);
            }
        }
        WHEN("A string option is set through the string interface") {
            config.set("machine_end_gcode", "100");
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionString>("machine_end_gcode")->value == "100");
            }
        }
        WHEN("A string option is set through the integer interface") {
            config.set("machine_end_gcode", 100);
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionString>("machine_end_gcode")->value == "100");
            }
        }
        WHEN("A string option is set through the double interface") {
            config.set("machine_end_gcode", 100.5);
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionString>("machine_end_gcode")->value == float_to_string_decimal_point(100.5));
            }
        }
        WHEN("A float or percent is set as a percent through the string interface.") {
            config.set_deserialize_strict("max_travel_detour_distance", "100%");
            THEN("Value and percent flag are 100/true") {
                auto tmp = config.opt<ConfigOptionFloatOrPercent>("max_travel_detour_distance");
                REQUIRE(tmp->percent == true);
                REQUIRE(tmp->value == 100);
            }
        }
        WHEN("A float or percent is set as a float through the string interface.") {
            config.set_deserialize_strict("max_travel_detour_distance", "100");
            THEN("Value and percent flag are 100/false") {
                auto tmp = config.opt<ConfigOptionFloatOrPercent>("max_travel_detour_distance");
                REQUIRE(tmp->percent == false);
                REQUIRE(tmp->value == 100);
            }
        }
        WHEN("A float or percent is set as a float through the int interface.") {
            config.set("max_travel_detour_distance", 100);
            THEN("Value and percent flag are 100/false") {
                auto tmp = config.opt<ConfigOptionFloatOrPercent>("max_travel_detour_distance");
                REQUIRE(tmp->percent == false);
                REQUIRE(tmp->value == 100);
            }
        }
        WHEN("A float or percent is set as a float through the double interface.") {
            config.set("max_travel_detour_distance", 100.5);
            THEN("Value and percent flag are 100.5/false") {
                auto tmp = config.opt<ConfigOptionFloatOrPercent>("max_travel_detour_distance");
                REQUIRE(tmp->percent == false);
                REQUIRE(tmp->value == 100.5);
            }
        }
        WHEN("An invalid option is requested during set.") {
            THEN("A BadOptionTypeException exception is thrown.") {
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", 1), UnknownOptionException);
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", 1.0), UnknownOptionException);
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", "1"), UnknownOptionException);
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", true), UnknownOptionException);
            }
        }

        WHEN("An invalid option is requested during get.") {
            THEN("A UnknownOptionException exception is thrown.") {
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionString>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionFloat>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionInt>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionBool>("deadbeef_invalid_option", false), UnknownOptionException);
            }
        }
        WHEN("An invalid option is requested during opt.") {
            THEN("A UnknownOptionException exception is thrown.") {
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionString>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionFloat>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionInt>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionBool>("deadbeef_invalid_option", false), UnknownOptionException);
            }
        }

        WHEN("getX called on an unset option.") {
            THEN("The default is returned.") {
                REQUIRE(config.has("layer_height"));
                REQUIRE(config.has("raft_layers"));
                REQUIRE(config.has("enable_support"));
                REQUIRE(config.opt_float("layer_height") == FullPrintConfig::defaults().layer_height.value);
                REQUIRE(config.opt_int("raft_layers") == 0);
                REQUIRE(config.opt_bool("enable_support") == FullPrintConfig::defaults().enable_support.value);
            }
        }

        WHEN("getFloat called on an option that has been set.") {
            config.set("layer_height", 0.5);
            THEN("The set value is returned.") {
                REQUIRE(config.opt_float("layer_height") == 0.5);
            }
        }
    }
}

SCENARIO("Config ini load/save interface", "[Config]") {
    WHEN("new_from_ini is called") {
		Slic3r::DynamicPrintConfig config;
		std::string path = std::string(TEST_DATA_DIR) + "/test_config/new_from_ini.ini";
		config.load_from_ini(path, ForwardCompatibilitySubstitutionRule::Disable);
        THEN("Config object contains ini file options.") {
			REQUIRE(config.option_throw<ConfigOptionStrings>("filament_colour", false)->values.size() == 1);
			REQUIRE(config.option_throw<ConfigOptionStrings>("filament_colour", false)->values.front() == "#ABCD");
        }
    }
}

SCENARIO("DynamicPrintConfig serialization", "[Config]") {
    WHEN("DynamicPrintConfig is serialized and deserialized") {
        FullPrintConfig full_print_config;
        DynamicPrintConfig cfg;
        cfg.apply(full_print_config, false);

        std::string serialized;
        REQUIRE_NOTHROW([&] {
            std::ostringstream ss;
            cereal::BinaryOutputArchive oarchive(ss);
            oarchive(cfg);
            serialized = ss.str();
        }());

        THEN("Config object contains ini file options.") {
            DynamicPrintConfig cfg2;
            REQUIRE_NOTHROW([&] {
                std::stringstream ss(serialized);
                cereal::BinaryInputArchive iarchive(ss);
                iarchive(cfg2);
            }());
            REQUIRE(cfg == cfg2);
        }
    }
}

SCENARIO("get_real_skirt_dist calculates the correct boundary including loop width", "[Config]") {
    GIVEN("A DynamicPrintConfig with skirt loops and spacing") {
        Slic3r::DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();

        config.set("skirt_distance", 2.0);
        config.set("skirt_loops", 3);
        config.set("initial_layer_line_width", 0.4);
        config.set("initial_layer_print_height", 0.2);
        config.set_deserialize_strict("nozzle_diameter", "0.4");
        config.set_deserialize_strict("draft_shield", "disabled");
        config.set("skirt_per_object", true);

        WHEN("get_real_skirt_dist is called") {
            float dist = Slic3r::get_real_skirt_dist(config);

            THEN("The distance includes the width of the skirt loops") {
                // Spacing is width - height * (1 - pi / 4), then the outer edge
                // adds half a spacing before the first loop and half a width.
                const float spacing = 0.4f - 0.2f * (1.f - 0.25f * float(PI));
                const float expected = 2.f + (3.f - 0.5f) * spacing + 0.5f * 0.4f;
                REQUIRE(dist == Approx(expected));
            }
        }

        WHEN("skirt_per_object is disabled") {
            config.set("skirt_per_object", false);
            float dist = Slic3r::get_real_skirt_dist(config);
            THEN("The distance is not applied") {
                REQUIRE(dist == 0.0f);
            }
        }

        WHEN("skirt_loops is 0") {
            config.set("skirt_loops", 0);
            float dist = Slic3r::get_real_skirt_dist(config);
            THEN("The distance is exactly 0 because has_skirt() should return false") {
                REQUIRE(dist == 0.0f);
            }
        }
    }
}

TEST_CASE("Bed shape falls back when extruder areas do not overlap", "[Config]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.opt<ConfigOptionPoints>("printable_area")->values =
        {{0., 0.}, {30., 0.}, {30., 10.}, {0., 10.}};
    config.opt<ConfigOptionPointsGroups>("extruder_printable_area")->values = {
        {{0., 0.}, {10., 0.}, {10., 10.}, {0., 10.}},
        {{20., 0.}, {30., 0.}, {30., 10.}, {20., 10.}}
    };

    const Points bed_shape = get_bed_shape(config, true);
    REQUIRE(bed_shape.size() == 4);
    REQUIRE(Polygon(bed_shape).bounding_box().defined);
}
