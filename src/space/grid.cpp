#include "space/grid.hpp"

#include "space/medium.hpp"
#include "space/object/object.hpp"

#include "space/utility/spacial_nav.hpp"

#include <immer/map.hpp>
#include <immer/flex_vector.hpp>
#include <lager/util.hpp>
#include <lager/context.hpp>

#include <algorithm>
#include <vector>

namespace space::grid{
    immer::flex_vector<object::object_model> sort_prio(immer::flex_vector<object::object_model> objects){
        std::vector<object::object_model> tmp(objects.begin(), objects.end());

        std::sort(tmp.begin(), tmp.end(), [](const auto& a, const auto& b){
            return a.object_prio < b.object_prio;
        });

        immer::flex_vector<object::object_model> result;
        for(auto& obj : tmp){
            result = result.push_back(std::move(obj));
        }
        return result;
    }

    immer::map<utility::cell_point, voxel_data> grid_model::stamp_in(){

        if(objects.empty()){return {};}

        immer::map<utility::cell_point, voxel_data> new_voxels;

        for(auto m = objects.rbegin(); m != objects.rend(); m++){
            for(const auto& [pos, norm] : m->volume){
                if(utility::funcs::in_bound(grid, pos)){
                    new_voxels = new_voxels.set(pos, voxel_data{
                                                           norm,
                                                           m->medium
                    });
                }
            }
        }

        return new_voxels;
    }

    grid_model update(grid_model m, actions a){
        return lager::match(std::move(a))(
            [&](action::grid_dimensions a){
                m.grid.x = a.new_x.value_or(m.grid.x);
                m.grid.y = a.new_y.value_or(m.grid.y);
                m.grid.z = a.new_z.value_or(m.grid.z);

                return m;
            },
            [&](action::cell_size a){
                m.cell_size = a.new_size;

                return m;
            },
            [&](action::default_medium_action a){
                m.default_medium = update(std::move(m.default_medium), std::move(a.action));
                return m;
            },
            [&](action::object_action a){
                m.objects = m.objects.set(
                    a.index,
                    update(std::move(m.objects[a.index]), std::move(a.action)));

                return m;
            },
            [&](action::update_object_volume a){
                m.objects = m.objects.set(
                    a.index,
                    update(std::move(m.objects[a.index]),
                           std::move(object::action::update_volume{m.cell_size})));

                return m;
            },
            [&](action::update_object_sdf a){
                m.objects = m.objects.set(
                    a.index,
                    update(std::move(m.objects[a.index]),
                           std::move(object::action::update_sdf{m.cell_size})));

                m.objects = m.objects.set(
                    a.index,
                    update(std::move(m.objects[a.index]),
                           std::move(object::action::update_volume{m.cell_size})));

                return m;
            },
            [&](action::new_sphere a){
                object::shapes::sphere_model sphere = {
                    origin,
                    1.0,
                };

                object::object_model sphere_object = {
                    static_cast<int>(m.objects.size()),
                    "",
                    utility::cell_point{0, 0, 0},
                    m.default_medium,
                    sphere
                };

                sphere_object.sdf_data = sphere.generate(m.cell_size);
                sphere_object.generate_volume(m.cell_size);

                m.objects = m.objects.push_back(sphere_object);
                m.objects = sort_prio(m.objects);

                return m;
            },
            [&](action::new_cube a){
                 object::shapes::cube_model cube = {
                     origin,
                     utility::vector3{1.0, 1.0, 1.0},
                     utility::vector3{0.0, 0.0, 0.0},
                 };

                 object::object_model cube_object = {
                     static_cast<int>(m.objects.size()),
                     "",
                     utility::cell_point{0, 0, 0},
                     m.default_medium,
                     cube
                 };

                 cube_object.sdf_data = cube.generate(m.cell_size);
                 cube_object.generate_volume(m.cell_size);

                 m.objects = m.objects.push_back(cube_object);
                 m.objects = sort_prio(m.objects);

                 return m;
            },
            [&](action::update_grid a){
                m.voxels = m.stamp_in();
                return m;
            },
            [&](action::sort_objects a){
                m.objects = sort_prio(m.objects);
                return m;
            }
        );
    }
}
