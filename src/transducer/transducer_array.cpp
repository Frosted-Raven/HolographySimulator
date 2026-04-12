#include "transducer/transducer_array.hpp"

#include <lager/util.hpp>
#include <lager/context.hpp>

namespace transducer::tran_array{
    tran_array_model update(tran_array_model m, actions a){
        return lager::match(std::move(a))(
            [&](action::new_name a){
                m.name = a.new_name;
                return m;
            },
            [&](action::add_tran a){
                m.tran_array = m.tran_array.push_back(
                    single::update(transducer::single::single_model {},
                    transducer::single::action::new_transducer {}));
                return m;
            },
            [&](action::remove_tran a){
                if(!m.tran_array.empty()){m.tran_array = m.tran_array.take(m.tran_array.size()-1);}
                return m;
            },
            [&](action::group_adjust a){
                immer::vector<single::single_model> new_tran_array;

                for(const auto& t : m.tran_array){
                    new_tran_array = new_tran_array.push_back(single::update(t, a.a));
                }
                m.tran_array = new_tran_array;

                return m;
            },
            [&](action::single_adjust a){
                m.tran_array = m.tran_array.set(
                    a.index,
                    single::update(std::move(m.tran_array[a.index]), std::move(a.a)));

                return m;
            }
        );
    }
}
