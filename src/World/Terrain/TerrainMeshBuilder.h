// ============================================================================
// Dionite — World: Triangulated terrain mesh builder with normals, tangents,
// UVs (world-scale) and per-vertex slope (for splatting).
// Supports tile streaming: build a `TerrainTile` for any (chunkX, chunkY).
// ============================================================================
#pragma once
#include "Heightmap.h"
#include "Rendering/MeshData.h"
#include "Core/Math/Vector.h"
#include <vector>

namespace dionite::world {

struct TerrainTile {
    int chunkX, chunkZ;
    render::Mesh mesh;
    std::vector<float> slope;   // per-vertex (0..1)
    std::vector<float> altitude;// per-vertex world Y
};

class TerrainMeshBuilder {
public:
    // Build a triangulated mesh for the entire heightmap. For streamed worlds,
    // call `buildTile()` for each chunk instead (returns mesh per region).
    static render::Mesh buildFullMesh(const Heightmap& h) {
        render::Mesh m;
        const int N = h.resolution;
        m.vertices.reserve((size_t)N * N);
        m.indices.reserve((size_t)(N - 1) * (N - 1) * 6);

        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                float wx, wz; h.worldXY(x, y, wx, wz);
                float wy = h.at(x, y);
                math::Vec3 normal = computeNormal(h, x, y);
                math::Vec3 tangent = math::Vec3(1, 0, 0); // simple; refined by shader
                render::Vertex v;
                v.position = { wx, wy, wz };
                v.normal   = normal;
                v.tangent  = tangent;
                v.u = wx / 8.f;   // world-scale UV — tiles every 8m
                v.v = wz / 8.f;
                m.vertices.push_back(v);
            }
        }
        for (int y = 0; y < N - 1; ++y) {
            for (int x = 0; x < N - 1; ++x) {
                uint32_t a = y * N + x;
                uint32_t b = a + 1;
                uint32_t c = a + N;
                uint32_t d = c + 1;
                m.indices.push_back(a); m.indices.push_back(c); m.indices.push_back(b);
                m.indices.push_back(b); m.indices.push_back(c); m.indices.push_back(d);
            }
        }
        return m;
    }

    static TerrainTile buildTile(const Heightmap& h, int cx, int cz, int chunkSize = 64) {
        TerrainTile t;
        t.chunkX = cx; t.chunkZ = cz;
        const int N = h.resolution;
        const int x0 = std::max(0, cx * chunkSize);
        const int z0 = std::max(0, cz * chunkSize);
        const int x1 = std::min(N - 1, x0 + chunkSize);
        const int z1 = std::min(N - 1, z0 + chunkSize);

        for (int y = z0; y <= z1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                float wx, wz; h.worldXY(x, y, wx, wz);
                float wy = h.at(x, y);
                math::Vec3 n = computeNormal(h, x, y);
                render::Vertex v;
                v.position = { wx, wy, wz };
                v.normal   = n;
                v.tangent  = { 1, 0, 0 };
                v.u = wx / 8.f; v.v = wz / 8.f;
                t.mesh.vertices.push_back(v);
                t.slope.push_back(1.f - n.y);     // 0 flat, 1 vertical wall
                t.altitude.push_back(wy);
            }
        }
        int W = (x1 - x0) + 1;
        int H = (z1 - z0) + 1;
        for (int y = 0; y < H - 1; ++y) {
            for (int x = 0; x < W - 1; ++x) {
                uint32_t a = y * W + x;
                uint32_t b = a + 1;
                uint32_t c = a + W;
                uint32_t d = c + 1;
                t.mesh.indices.push_back(a); t.mesh.indices.push_back(c); t.mesh.indices.push_back(b);
                t.mesh.indices.push_back(b); t.mesh.indices.push_back(c); t.mesh.indices.push_back(d);
            }
        }
        return t;
    }

private:
    static math::Vec3 computeNormal(const Heightmap& h, int x, int y) {
        float l = h.at(x - 1, y), r = h.at(x + 1, y);
        float d = h.at(x, y - 1), u = h.at(x, y + 1);
        float scale = h.worldSize / (h.resolution - 1);
        math::Vec3 dx{ 2 * scale, r - l, 0 };
        math::Vec3 dz{ 0,         u - d, 2 * scale };
        return dx.cross(dz).normalized();
    }
};

} // namespace dionite::world
