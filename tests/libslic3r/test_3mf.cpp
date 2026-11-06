#include <catch2/catch.hpp>

#include "libslic3r/Model.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"

#include <boost/filesystem/operations.hpp>

using namespace Slic3r;

SCENARIO("Reading 3mf file", "[3mf]") {
    GIVEN("umlauts in the path of the file") {
        Model model;
        WHEN("3mf model is read") {
        	std::string path = std::string(TEST_DATA_DIR) + "/test_3mf/Geräte/Büchse.3mf";
        	DynamicPrintConfig config;
            ConfigSubstitutionContext ctxt{ ForwardCompatibilitySubstitutionRule::Disable };
            bool ret = load_3mf(path.c_str(), config, ctxt, &model, false);
            THEN("load should succeed") {
                REQUIRE(ret);
            }
        }
    }
}

SCENARIO("Export+Import geometry to/from 3mf file cycle", "[3mf]") {
    GIVEN("world vertices coordinates before save") {
        // load a model from stl file
        Model src_model;
        std::string src_file = std::string(TEST_DATA_DIR) + "/test_3mf/Prusa.stl";
        load_stl(src_file.c_str(), &src_model);
        src_model.add_default_instances();

        ModelObject* src_object = src_model.objects.front();

        // apply generic transformation to the 1st volume
        Geometry::Transformation src_volume_transform;
        src_volume_transform.set_offset({ 10.0, 20.0, 0.0 });
        src_volume_transform.set_rotation({ Geometry::deg2rad(25.0), Geometry::deg2rad(35.0), Geometry::deg2rad(45.0) });
        src_volume_transform.set_scaling_factor({ 1.1, 1.2, 1.3 });
        src_volume_transform.set_mirror({ -1.0, 1.0, -1.0 });
        src_object->volumes.front()->set_transformation(src_volume_transform);

        // apply generic transformation to the 1st instance
        Geometry::Transformation src_instance_transform;
        src_instance_transform.set_offset({ 5.0, 10.0, 0.0 });
        src_instance_transform.set_rotation({ Geometry::deg2rad(12.0), Geometry::deg2rad(13.0), Geometry::deg2rad(14.0) });
        src_instance_transform.set_scaling_factor({ 0.9, 0.8, 0.7 });
        src_instance_transform.set_mirror({ 1.0, -1.0, -1.0 });
        src_object->instances.front()->set_transformation(src_instance_transform);

        WHEN("model is saved+loaded to/from 3mf file") {
            // save the model to 3mf file
            std::string test_file = std::string(TEST_DATA_DIR) + "/test_3mf/prusa.3mf";
            store_3mf(test_file.c_str(), &src_model, nullptr, false);

            // load back the model from the 3mf file
            Model dst_model;
            DynamicPrintConfig dst_config;
            {
                ConfigSubstitutionContext ctxt{ ForwardCompatibilitySubstitutionRule::Disable };
                load_3mf(test_file.c_str(), dst_config, ctxt, &dst_model, false);
            }
            boost::filesystem::remove(test_file);

            // compare meshes
            TriangleMesh src_mesh = src_model.mesh();
            TriangleMesh dst_mesh = dst_model.mesh();

            bool res = src_mesh.its.vertices.size() == dst_mesh.its.vertices.size();
            if (res) {
                for (size_t i = 0; i < dst_mesh.its.vertices.size(); ++i) {
                    res &= dst_mesh.its.vertices[i].isApprox(src_mesh.its.vertices[i]);
                }
            }
            THEN("world vertices coordinates after load match") {
                REQUIRE(res);
            }
        }
    }
}

SCENARIO("2D convex hull of sinking object", "[3mf]") {
    GIVEN("model") {
        // load a model
        Model model;
        std::string src_file = std::string(TEST_DATA_DIR) + "/test_3mf/Prusa.stl";
        load_stl(src_file.c_str(), &model);
        model.add_default_instances();

        WHEN("model is rotated, scaled and set as sinking") {
            ModelObject* object = model.objects.front();
            object->center_around_origin(false);

            // set instance's attitude so that it is rotated, scaled and sinking
            ModelInstance* instance = object->instances.front();
            instance->set_rotation(Vec3d(-M_PI / 4.0, 0.0, 0.0));
            instance->set_offset(Vec3d::Zero());
            instance->set_scaling_factor({ 2.0, 2.0, 2.0 });

            // calculate 2D convex hull
            Polygon hull_2d = object->convex_hull_2d(instance->get_transformation().get_matrix());

            // Independently project the raw mesh rather than copying the cached
            // convex-hull implementation or scale-dependent integer fixtures.
            Points projected_vertices;
            bool has_vertex_below_bed = false;
            bool has_vertex_above_bed = false;
            for (const ModelVolume *volume : object->volumes) {
                if (!volume->is_model_part())
                    continue;
                const Transform3d transform = instance->get_transformation().get_matrix() * volume->get_matrix();
                for (const Vec3f &vertex : volume->mesh().its.vertices) {
                    const Vec3d transformed = transform * vertex.cast<double>();
                    has_vertex_below_bed |= transformed.z() < 0.;
                    has_vertex_above_bed |= transformed.z() > 0.;
                    projected_vertices.emplace_back(
                        scaled<coord_t>(transformed.x()), scaled<coord_t>(transformed.y()));
                }
            }
            const Polygon expected = Geometry::convex_hull(std::move(projected_vertices));

            // Polygon start vertex and winding are not part of the projection
            // contract. Allow one coordinate quantum for float-to-integer
            // rounding between the raw mesh and cached 3D hull paths.
            auto same_shape = [](const Polygon &lhs, const Polygon &rhs) {
                if (lhs.points.size() != rhs.points.size())
                    return false;
                const size_t count = lhs.points.size();
                if (count == 0)
                    return true;
                auto point_matches = [](const Point &a, const Point &b) {
                    return std::abs(a.x() - b.x()) <= 1 && std::abs(a.y() - b.y()) <= 1;
                };
                for (size_t start = 0; start < count; ++start) {
                    for (int direction : {1, -1}) {
                        bool matches = true;
                        for (size_t i = 0; i < count; ++i) {
                            const size_t rhs_index = direction > 0 ?
                                (start + i) % count : (start + count - i) % count;
                            if (!point_matches(lhs.points[i], rhs.points[rhs_index])) {
                                matches = false;
                                break;
                            }
                        }
                        if (matches)
                            return true;
                    }
                }
                return false;
            };

            THEN("2D convex hull should match with reference") {
                CAPTURE(hull_2d.points, expected.points);
                REQUIRE(has_vertex_below_bed);
                REQUIRE(has_vertex_above_bed);
                REQUIRE(same_shape(hull_2d, expected));
            }
        }
    }
}

