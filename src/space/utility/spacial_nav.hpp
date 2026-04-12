#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include <cmath>
namespace space::utility{
    struct point3{
        double x;
        double y;
        double z;
    };

    struct vector3{
        double i;
        double j;
        double k;
    };

    struct cell_quantity{
        uint16_t x;
        uint16_t y;
        uint16_t z;
    };

    struct cell_point{
        uint16_t x;
        uint16_t y;
        uint16_t z;

        bool operator==(const cell_point& o) const noexcept {
            return x == o.x && y == o.y && z == o.z;
        }
    };

    struct sdf{
        std::vector<double> distance;
        std::vector<vector3> normal;
        point3 origin;
        point3 dimensions;
     };

    namespace funcs{
        point3 index_to_position(point3 origin, cell_point ind, double cell_size);

        cell_point position_to_index(point3 origin, point3 pos, double cell_size);

        bool in_bound(cell_quantity bound, cell_point point);
    }
}

template<>
struct std::hash<space::utility::cell_point> {
    size_t operator()(const space::utility::cell_point& p) const noexcept {
        size_t h = std::hash<uint16_t>{}(p.x);
        h ^= std::hash<uint16_t>{}(p.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint16_t>{}(p.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
