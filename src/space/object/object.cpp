#include "space/object/object.hpp"
#include "space/utility/spacial_nav.hpp"
#include <lager/context.hpp>
#include <lager/util.hpp>

#include <cstdint>

namespace space::object{
    void object_model::generate_volume(double cell_size){
        int entries = static_cast<int>(sdf_data.distance.size());

        int x_cells = static_cast<int>(sdf_data.dimensions.x/cell_size);
        int y_cells = static_cast<int>(sdf_data.dimensions.y/cell_size);
        int z_cells = static_cast<int>(sdf_data.dimensions.z/cell_size);

        immer::map<utility::cell_point, utility::vector3> new_map;

        for(int i = 0; i < entries; i++){
            if(sdf_data.distance[i] < 0.0){
                utility::cell_point pos = {
                    static_cast<uint16_t>(static_cast<uint16_t>(i % x_cells) + transform_mod.x),
                    static_cast<uint16_t>(static_cast<uint16_t>((i % (x_cells * y_cells)) / x_cells) + transform_mod.y),
                    static_cast<uint16_t>(static_cast<uint16_t>(i / (x_cells * y_cells)) + transform_mod.z)
                };

                new_map = new_map.set(pos, sdf_data.normal[i]);
            }
        }

        volume = new_map;
    }

    object_model update(object_model m, actions a){
        return lager::match(std::move(a))(
            [&](action::transform_mod a){
                m.transform_mod = a.new_transform;
                return m;
            },
            [&](action::medium a){
                m.medium = a.new_medium;
                return m;
            },
            [&](action::shape a){
                m.shape = a.new_shape;
                return m;
            },
            [&](action::priority a){
                m.object_prio = a.new_prio;
                return m;
            },
            [&](action::name a){
                m.name = a.new_name;
                return m;
            },
            [&](action::update_sdf a){
                m.sdf_data = std::visit([&](auto& s){
                    return s.generate(a.cell_size);}, m.shape);
                return m;
            },
            [&](action::update_volume a){
                m.generate_volume(a.cell_size);
                return m;
            },
            [&](space::medium::actions a){
                m.medium = space::medium::update(std::move(m.medium), std::move(a));
                return m;
            },
            [&](shapes::actions a){
                m.shape = update(std::move(m.shape), std::move(a));
                return m;
            });
    }
}
