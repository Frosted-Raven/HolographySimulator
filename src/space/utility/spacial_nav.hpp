#pragma once

#include <cstdint>
#include <vector>
namespace space::descriptor{
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

    struct sdf{
        std::vector<double> distance;
        std::vector<vector3> normal;
        point3 origin;
        point3 dimensions;
     };
}
