// ============================================================================
// Dionite — World: Water system (lakes, rivers).
//   Lakes are flat quads at `seaLevel`; rivers are spline-based meshes.
//   Animated normal maps applied by the water shader (see WaterShader.metal).
// ============================================================================
#pragma once
#include "Heightmap.h"
#include "Core/Math/Vector.h"
#include "Rendering/MeshData.h"
#include <vector>

namespace dionite::world {

struct WaterPlane {
    math::Vec3 center;
    float width;
    float length;
    float surfaceY;
    std::string materialId = "mat_water";
};

class WaterSystem {
public:
    // Detect lake bounding boxes by flood-filling below seaLevel on the heightmap.
    static std::vector<WaterPlane> findLakes(const Heightmap& h, float seaLevel) {
        std::vector<WaterPlane> out;
        const int N = h.resolution;
        std::vector<bool> visited((size_t)N * N, false);
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                int idx = y * N + x;
                if (visited[idx] || h.at(x, y) > seaLevel) continue;
                // BFS flood
                int minX = x, maxX = x, minY = y, maxY = y, count = 0;
                std::vector<int> stack = { idx };
                visited[idx] = true;
                while (!stack.empty()) {
                    int s = stack.back(); stack.pop_back();
                    int sx = s % N, sy = s / N;
                    minX = std::min(minX, sx); maxX = std::max(maxX, sx);
                    minY = std::min(minY, sy); maxY = std::max(maxY, sy);
                    count++;
                    const int dx[4] = {1,-1,0,0}, dy[4] = {0,0,1,-1};
                    for (int k = 0; k < 4; ++k) {
                        int nx = sx + dx[k], ny = sy + dy[k];
                        if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
                        int ni = ny * N + nx;
                        if (visited[ni]) continue;
                        if (h.at(nx, ny) > seaLevel) continue;
                        visited[ni] = true;
                        stack.push_back(ni);
                    }
                }
                if (count < 30) continue; // too small to bother
                WaterPlane w;
                float wx0, wz0, wx1, wz1;
                h.worldXY(minX, minY, wx0, wz0);
                h.worldXY(maxX, maxY, wx1, wz1);
                w.center  = { (wx0 + wx1) * 0.5f, seaLevel + 0.02f, (wz0 + wz1) * 0.5f };
                w.width   = wx1 - wx0 + 2.f;
                w.length  = wz1 - wz0 + 2.f;
                w.surfaceY = seaLevel + 0.02f;
                out.push_back(w);
            }
        }
        return out;
    }

    static render::Mesh planeMesh(const WaterPlane& w) {
        render::Mesh m;
        math::Vec3 c = w.center;
        float hx = w.width  * 0.5f;
        float hz = w.length * 0.5f;
        m.vertices = {
            { {c.x - hx, c.y, c.z - hz}, {0,1,0}, {1,0,0}, 0.f,   0.f },
            { {c.x + hx, c.y, c.z - hz}, {0,1,0}, {1,0,0}, w.width/8.f, 0.f },
            { {c.x - hx, c.y, c.z + hz}, {0,1,0}, {1,0,0}, 0.f, w.length/8.f },
            { {c.x + hx, c.y, c.z + hz}, {0,1,0}, {1,0,0}, w.width/8.f, w.length/8.f },
        };
        m.indices = { 0, 2, 1, 1, 2, 3 };
        return m;
    }
};

} // namespace dionite::world
