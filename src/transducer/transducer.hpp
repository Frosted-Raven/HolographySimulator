#pragma once

#include"space/utility/spacial_nav.hpp"
#include<lager/effect.hpp>

#include<string>
#include<optional>
#include<variant>

namespace transducer::single{
    struct single_model{
        std::optional<std::string> name;

        space::utility::point3 position;
        double frequency;
        double amplitude;
        double phase;

        bool is_active;
    };

    namespace action{
        struct new_transducer{};

        struct toggle_active{};

        struct new_name{
            std::string new_name;
        };

        struct new_position{
            std::optional<double> new_x,
                                  new_y,
                                  new_z;
        };

        struct new_frequency{
            double new_frequency;
        };

        struct new_amplitude{
            double new_amplitude;
        };

        struct new_phase{
            double new_phase;
        };

        struct mod_position{
            std::optional<double> mod_x,
                                  mod_y,
                                  mod_z;
        };

        struct mod_frequency{
            double mod_frequency;
        };

        struct mod_amplitude{
            double mod_amplitude;
        };

        struct mod_phase{
            double mod_phase;
        };
    }

    using actions = std::variant<action::new_transducer,
                                 action::new_name,
                                 action::new_position,
                                 action::new_frequency,
                                 action::new_amplitude,
                                 action::new_phase,
                                 action::mod_position,
                                 action::mod_frequency,
                                 action::mod_amplitude,
                                 action::mod_phase,
                                 action::toggle_active>;

    single_model update(single_model m, actions a);
}
