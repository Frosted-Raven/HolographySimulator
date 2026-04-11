#pragma once

#include <lager/effect.hpp>

#include <string>
#include <cstdint>
#include <variant>

namespace space::medium{

    double reflection_coefficient(double mat_z, double med_z);
    double transmission_coefficient(double mat_z, double med_z);

    struct medium_model{
        std::string name;
        uint8_t priority;

        double sound_speed;
        double acoustic_impedance;

        double density;
        double absorption;
        double temperature;
        double stiffness;

        bool is_rigid = false;
    };

    namespace action{

        struct medium{
            std::string new_name;
            std::uint8_t priority;

            double new_density;
            double new_absorption;
            double new_temperature;

            bool new_rigid;
        };

        struct name{
            std::string new_name;
        };

        struct priority{
            std::uint8_t new_priority;
        }

        struct density{
            double new_density;
        };

        struct absorption{
            double new_absorption;
        };

        struct temperature{
            double new_temperature;
        };

        struct stiffness{
            double new_stiffness;
        };

        struct rigid{
            bool new_rigid;
        };
    }

    using actions = std::variant<action::medium,
                                 action::name,
                                 action::priority,
                                 action::density,
                                 action::absorption,
                                 action::temperature,
                                 action::stiffness>;

    double speed_calc(double stiffness, double density);
    double impedance_calc(double density, double speed);
    double reflection_coefficient(double mat_z, double med_z);
    double transmission_coefficient(double mat_z, double med_z);

    medium_model update(medium_model m, actions a);
}
