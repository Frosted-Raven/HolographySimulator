#pragma once

#include "transducer/transducer.hpp"
#include <lager/effect.hpp>
#include <immer/vector.hpp>

#include <string>
#include <optional>
#include <variant>

namespace transducer::tran_array{
    struct tran_array_model{
        std::optional<std::string> name;
        immer::vector<transducer::single::single_model> tran_array;
    };

    namespace action{
        struct new_name{
            std::string new_name;
        };

        struct add_tran{};
        struct remove_tran{};

        struct group_adjust{
            transducer::single::actions a;
        };

        struct single_adjust{
            int index;
            transducer::single::actions a;
        };
    }

    using actions = std::variant<action::new_name,
                                 action::add_tran,
                                 action::remove_tran,
                                 action::group_adjust,
                                 action::single_adjust>;

    tran_array_model update(tran_array_model m, actions a);
}
