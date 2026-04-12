#include "spacial_nav.hpp"

#include <cstdint>
#include <vector>
#include <cmath>

namespace space::utility::funcs{
    point3 index_to_position(point3 origin, cell_point ind, double cell_size){
         return point3{
             origin.x + static_cast<double>(ind.x) * cell_size,
             origin.y + static_cast<double>(ind.y) * cell_size,
             origin.z + static_cast<double>(ind.z) * cell_size
         };
     }

     cell_point position_to_index(point3 origin, point3 pos, double cell_size){
         return cell_point{
             static_cast<uint16_t>(std::floor((pos.x - origin.x)/cell_size)),
             static_cast<uint16_t>(std::floor((pos.y - origin.y)/cell_size)),
             static_cast<uint16_t>(std::floor((pos.z - origin.z)/cell_size))
         };
     }

     bool in_bound(cell_quantity bound, cell_point point){
         return point.x > 0 && point.x < bound.x &&
                point.y > 0 && point.y < bound.y &&
                point.z > 0 && point.z < bound.z;
     }
}
