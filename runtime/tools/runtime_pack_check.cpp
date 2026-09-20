#include "../src/runtime_data.hpp"

#include <iomanip>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: runtime_pack_check <m8_runtime.snr>\n";
        return 2;
    }
    try {
        const auto pack = socom::LoadRuntimePack(argv[1]);
        std::cout << "SOCOM runtime pack v" << pack.version << "\n";
        std::cout << "render vertices: " << pack.renderVertices.size() << "\n";
        std::cout << "render triangles: " << pack.renderVertices.size()/3 << "\n";
        std::cout << "collision triangles: " << pack.collisionTriangles.size() << "\n";
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "bounds: (" << pack.boundsMin.x << ", " << pack.boundsMin.y << ", " << pack.boundsMin.z
                  << ") -> (" << pack.boundsMax.x << ", " << pack.boundsMax.y << ", " << pack.boundsMax.z << ")\n";
        std::cout << "spawn: (" << pack.spawn.x << ", " << pack.spawn.y << ", " << pack.spawn.z << ")\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
