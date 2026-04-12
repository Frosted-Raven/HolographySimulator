#include "transducer/transducer.hpp"

#include <lager/context.hpp>
#include <lager/util.hpp>

namespace transducer::single{
    single_model update(single_model m, actions a){
        return lager::match(std::move(a))(
            [&](action::new_transducer a){
                single_model new_tran;

                new_tran.position = space::utility::point3{0.0, 0.0, 0.0};
                new_tran.frequency = 0.0;
                new_tran.phase = 0.0;
                new_tran.amplitude = 0.0;
                new_tran.is_active = false;

                m = new_tran;

                return m;
            },
            [&](action::toggle_active a){
                m.is_active = !m.is_active;
                return m;
            },
            [&](action::new_name a){
                m.name = a.new_name;
                return m;
            },
            [&](action::new_position a){
                m.position.x = a.new_x.value_or(m.position.x);
                m.position.y = a.new_y.value_or(m.position.y);
                m.position.z = a.new_z.value_or(m.position.z);

                return m;
            },
            [&](action::new_frequency a){
                m.frequency = a.new_frequency;
                return m;
            },
            [&](action::new_amplitude a){
                m.amplitude = a.new_amplitude;
                return m;
            },
            [&](action::new_phase a){
                m.phase = a.new_phase;
                return m;
            },
            [&](action::mod_position a){
                m.position.x += a.mod_x.value_or(0.0);
                m.position.y += a.mod_y.value_or(0.0);
                m.position.z += a.mod_z.value_or(0.0);

                return m;
            },
            [&](action::mod_frequency a){
                m.frequency += a.mod_frequency;
                return m;
            },
            [&](action::mod_amplitude a){
                m.amplitude += a.mod_amplitude;
                return m;
            },
            [&](action::mod_phase a){
                m.phase += a.mod_phase;
                return m;
            });
    }
}
