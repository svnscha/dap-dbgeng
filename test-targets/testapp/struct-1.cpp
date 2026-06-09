#include <iostream>

// Debuggee for struct-expansion tests. Its locals are aggregates so the Locals
// view must be expandable: `t` nests `point2` and `vector3` (two levels deep).
// Like launch.cpp, the source must stay stable so recorded sessions keep
// matching, and it builds unoptimized (/Od /Zi) so the locals stay live.
struct point2
{
    int x;
    int y;
};

struct vector3
{
    double x;
    double y;
    double z;
};

struct transform
{
    point2 origin;
    vector3 scale;
    int id;
};

int main(int argc, char *argv[])
{
    (void)argc; // Unused parameter
    (void)argv; // Unused parameter

    point2 p{3, 7};
    vector3 v{1.5, 2.5, 3.5};
    transform t{{10, 20}, {0.5, 0.5, 1.0}, 42};

    // Locals (p, v, t) are live here; t nests point2 + vector3 (two levels).
    std::cout << p.x << " " << v.z << " " << t.scale.z << " " << t.id << std::endl;

    return 0;
}
