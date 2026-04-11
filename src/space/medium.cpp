#include "medium.hpp"

#include <lager/context.hpp>
#include <lager/util.hpp>

#include <cmath>
#include <string>

namespace space::medium{

    double speed_calc(double stiffness, double density){
        double calc = stiffness/density;
        calc = std::sqrt(calc);
        return calc;
    }

    double impedance_calc(double density, double speed){
        return density * speed;
    }

    double reflection_coefficient(double mat_z, double med_z){
        double r_t = mat_z - med_z;
        double r_b = mat_z + med_z;
        return r_t/r_b;
    }

    double transmission_coefficient(double mat_z, double med_z){
        return 1-reflection_coefficient(mat_z, med_z);
    }

    medium_model update(medium_model m, action a){
        return lager::match(std::move(a))(
            [&](action::medium a){
                m.name = a.new_name;
                m.priority = a.new_priority;

                m.density = a.new_density;
                m.absorption = a.new_absorption;
                m.temperature = a.new_temperature;
                m.stiffness = a.new_stiffness;
                m.is_rigid = a.new_rigid;

                m.sound_speed = speed_calc(a.new_stiffness, a.new_density);
                m.acoustic_impedance = impedance_calc(m.density, m.sound_speed);
                return m;
            },
            [&](action::name a){
                m.name = a.new_name;
                return m;
            },
            [&](action::priority a){
                m.priority = a.new_priority;
                return m;
            },
            [&](action::density a){
                m.density = a.new_density;
                m.sound_speed = speed_calc(m.stiffness, a.new_density);
                m.acoustic_impedance = impedance_calc(m.density, m.sound_speed);
                return m;
            },
            [&](action::absorption a){
                m.absorption = a.new_absorption;
                return m;
            },
            [&](action::temperature a){
                m.temperature = a.new_temperature;
                return m;
            },
            [&](action::stiffness a){
                m.stiffness = a.new_stiffness;
                m.sound_speed = speed_calc(a.new_stiffness, m.density);
                m.acoustic_impedance = impedance_calc(m.density, m.sound_speed);
                return m;
            },
            [&](action::rigid a){
                m.is_rigid = a.new_rigid;
            });
    }
}
