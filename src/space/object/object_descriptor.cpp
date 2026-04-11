#include "space/object/object_descriptor.hpp"
#include "space/utility/spacial_nav.hpp"
#include <lager/context.hpp>
#include <lager/util.hpp>

#include <vector>
#include <cstdint>
#include <cmath>
#include <optional>
#include <algorithm>

namespace space::object::shapes{
    utility::sdf sphere_model::generate(double cell_size) const {
        utility::point3 origin = {
            world_position.x - scale,
            world_position.y - scale,
            world_position.z - scale
        };

        double diameter = scale * 2;
        uint16_t cell_count = static_cast<uint16_t>(diameter / cell_size);

        utility::sdf sdf_data;
        sdf_data.origin = origin;
        sdf_data.dimensions = utility::point3{diameter, diameter, diameter};

        for(int i = 0; i < cell_count; i++){
            for(int j = 0; j < cell_count; j++){
                for(int k = 0; k < cell_count; k++){

                    utility::point3 cell = {
                        origin.x + (i + 0.5) * cell_size,
                        origin.y + (j + 0.5) * cell_size,
                        origin.z + (k + 0.5) * cell_size
                    };

                    utility::vector3 center = {
                        cell.x - world_position.x,
                        cell.y - world_position.y,
                        cell.z - world_position.z
                    };

                    double length = std::sqrt(center.i * center.i +
                                              center.j * center.j +
                                              center.k * center.k);
                    double distance = length - scale;

                    utility::vector3 normal = {0, 0, 0};
                    if(length > 0) {
                        normal = {
                            center.i / length,
                            center.j / length,
                            center.k / length
                        };
                    }

                    sdf_data.distance.push_back(distance);
                    sdf_data.normal.push_back(normal);
                }
            }
        }
        return sdf_data;
    }

    utility::sdf cube_model::generate(double cell_size) const {
        utility::point3 origin = {
            world_position.x - (scale.i / 2),
            world_position.y - (scale.j / 2),
            world_position.z - (scale.k / 2)
        };

        utility::cell_quantity cell_count = {
            static_cast<uint16_t>(scale.i / cell_size),
            static_cast<uint16_t>(scale.j / cell_size),
            static_cast<uint16_t>(scale.k / cell_size)
        };

        utility::sdf sdf_data;
        sdf_data.origin = origin;
        sdf_data.dimensions = utility::point3{scale.i, scale.j, scale.k};

        for(int i = 0; i < cell_count.x; i++){
            for(int j = 0; j < cell_count.y; j++){
                for(int k = 0; k < cell_count.z; k++){
                    utility::point3 center = {
                        (origin.x + (i + 0.5) * cell_size) - world_position.x,
                        (origin.y + (j + 0.5) * cell_size) - world_position.y,
                        (origin.z + (k + 0.5) * cell_size) - world_position.z
                    };

                    utility::vector3 to_surface = {
                        std::abs(center.x) - (scale.i / 2),
                        std::abs(center.y) - (scale.j / 2),
                        std::abs(center.z) - (scale.k / 2)
                    };

                    double outer_d = std::sqrt(
                        std::pow(std::max(to_surface.i, 0.0), 2) +
                        std::pow(std::max(to_surface.j, 0.0), 2) +
                        std::pow(std::max(to_surface.k, 0.0), 2)
                    );
                    double inner_d = std::min(
                        std::max({to_surface.i, to_surface.j, to_surface.k}),
                        0.0);

                    double distance = outer_d + inner_d;

                    utility::vector3 normal;
                    if(std::abs(center.x) > std::abs(center.y) && std::abs(center.x) > std::abs(center.z)){
                        normal = {center.x > 0 ? 1.0 : -1.0, 0, 0};
                    }
                    else if (std::abs(center.y) > std::abs(center.z)){
                        normal = {0, center.y > 0 ? 1.0 : -1.0, 0};
                    }
                    else{
                        normal = {0, 0, center.z > 0 ? 1.0 : -1.0};
                    }

                    sdf_data.distance.push_back(distance);
                    sdf_data.normal.push_back(normal);
                }
            }
        }
        return sdf_data;
    }

    shape_model update(shape_model m, actions a){
        return lager::match(std::move(a))(
            [&](action::new_sphere a) -> shape_model {
                return sphere_model{a.new_pos, a.scale};
            },
            [&](action::new_cube a) -> shape_model {
                return cube_model{a.new_pos, a.new_scale, a.rotation};
            },
            [&](action::edit_position a) -> shape_model {
                std::visit([&](auto& s){ s.world_position = a.new_pos; }, m);
                return m;
            },
            [&](action::edit_cube a) -> shape_model {
                if(auto* c = std::get_if<cube_model>(&m)){
                    if(a.new_scale.has_value()){ c->scale = a.new_scale.value(); }
                    if(a.new_rotation.has_value()){ c->rotation = a.new_rotation.value(); }
                }
                return m;
            },
            [&](action::edit_sphere a) -> shape_model {
                if(auto* s = std::get_if<sphere_model>(&m)){
                    s->scale = a.new_scale;
                }
                return m;
            }
        );
    }
}
