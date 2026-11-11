// ============================================================================
// Dionite — Geometry data.
//
// The C++ core produces geometry (terrain chunks, dungeon meshes, arena props)
// as plain vertex/index buffers. The iOS host uploads them to Metal. There is no
// renderer abstraction here: Metal is the only backend.
// ============================================================================
#pragma once

#include "Core/Math/Vector.h"

#include <cstdint>
#include <vector>

namespace dionite::render {

struct Vertex {
    math::Vec3 position;
    math::Vec3 normal;
    math::Vec3 tangent;
    float u = 0.f;
    float v = 0.f;
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

} // namespace dionite::render
