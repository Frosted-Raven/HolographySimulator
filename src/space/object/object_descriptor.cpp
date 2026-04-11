#include "space/object/object_descriptor.hpp"
#include "space/utility/spacial_nav.hpp"
#include <lager/context.hpp>
#include <lager/util.hpp>

#include <vector>
#include <cstdint>
#include <cmath>
#include <optional>

namespace space::objects::shapes{
    utility::sdf shape_model::generate(double cell_size){
        return utility::sdf{};
    }

    utility::sdf sphere_model::generate(double cell_size){
        utility::point3 origin = {
            world_position.x - scale,
            world_position.y - scale,
            world_position.z - scale
        };

        double diamater = scale * 2;
        uint16_t cell_count = diameter/cell_size;

        utility::sdf sdf_data;
        sdf_data.origin = origin;
        sdf_data.dimensions = utility::point3{diamater, diamater, diamater};

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


                    double length = sqrt(center.i * center.i +
                                         center.j * center.j +
                                         center.k * center.k);
                    double distance = length - radius;


                    utility::vector3 normal = {
                        center.i/length,
                        center.j/length,
                        center.k/length
                    };

                    sdf_data.distance.push_back(distance);
                    sdf_data.normal.push_back(normal);
                }
            }
        }
        return sdf_data;
    }

    utility::sdf cube_model::generate(double cell_size){
        utility::point3 origin = {
            world_position.x - (scale.i/2),
            world_position.y - (scale.j/2),
            world_position.z - (scale.k/2)
        };

        utility::cell_quantity cell_count = {
            static_cast<uint_16>(scale.i/cell_size),
            static_cast<uint_16>(scale.j/cell_size),
            static_cast<uint_16>(scale.k/cell_size)
        };

        utility::sdf sdf_data;
        sdf_data.origin = origin;
        sdf_data.dimensions = scale;

        for(int i = 0; i < cell_count.x; i++){
            for(int j = 0; j < cell_count.y; i++){
                for(int k = 0; k < cell_count.k; i++){
                    utility::point3 center = {
                        (origin.x + (i + 0.5) * cell_size) - world_position.x,
                        (origin.y + (j + 0.5) * cell_size) - world_position.y,
                        (origin.z + (k + 0.5) * cell_size) - world_positipn.z
                    };

                    utility::vector3 to_surface = {
                        abs(center.x) - (scale.i/2);
                        abs(center.y) - (scale.j/2);
                        abs(center.z) - (scale.k/2);
                    };

                    double outer_d = sqrt(
                        pow(std::max(to_surface.x, 0.0), 2) +
                        pow(std::max(to_surface.y, 0.0), 2) +
                        pow(std::max(to_surface.z, 0.0), 2)
                    );
                    double inner_d = std::min({to_surface.x, to_surface.y, to_surface.z}, 0.0);

                    double distance = outer + inner;

                    utility::vector3 normal;
                    if(abs(center.x) > abs(center.y) && abs(center.x) > abs(center.z)){
                        normal = {center.x > 0 ? 1.0 : -1.0, 0, 0};
                    }
                    else if (abs(center.y) > abs(center.z)){
                        normal = {0, ly > 0 ? 1.0 : -1.0, 0};
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
            [&](action::new_sphere a){
                sphere_model sphere;
                sphere.world_position = a.new_pos;
                sphere.scale = a.new_scale;
                return sphere;
            },
            [&](action::new_cube a){
                cube_model cube;
                cube_model.world_position = a.new_pos;
                cube_model.scale = a.new_scale;
                cube_model.rotation = a.rotation;
                return cube;
            },
            [&](actions::edit_position a){
                m.world_position = a.new_pos;
                return m;
            },
            [&](actions::edit_cube a){
                if(typeid(m) == typeid(cube_model)){
                    if(a.new_scale.has_value()){m.scale = a.new_scale.value();}
                    if(a.new_rotation.has_value()){m.rotation = a.new_rotation.value();}
                }
                return m;
            },
            [&](actions::edit_sphere a){
                if(typeid(m) == typeid(sphere_model)){
                    m.scale = a.new_scale;
                }
                return m;
            }
        );
    }
}
