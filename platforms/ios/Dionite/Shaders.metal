// ============================================================================
// Dionite — Metal shaders for the snapshot renderer.
//
// The C++ runtime hands us a flat list of DIInstance records (exactly 64 bytes,
// see src/Game/Snapshot.h) grouped into three passes:
//   [0 .. opaque)                   opaque geometry       — alpha blend, depth write
//   [opaque .. opaque+translucent)  translucent geometry  — alpha blend, depth read only
//   [.. end)                        additive geometry     — additive blend, depth read only
//
// Instances are drawn instanced-per-mesh: the vertex buffer holds one
// primitive (quad / box / capsule / sphere / cone / cylinder) and the instance
// buffer supplies position, yaw, scale, colour and flags.
// ============================================================================
#include <metal_stdlib>
using namespace metal;

// ---------------------------------------------------------------------------
// Must match DIInstance in src/Game/Snapshot.h — 64 bytes, 4-byte alignment.
// ---------------------------------------------------------------------------
struct Instance {
    float x, y, z;
    float rotY;
    float sx, sy, sz;
    float emissive;
    uint  rgba;     // byte order R,G,B,A in memory
    uint  mesh;     // DIMesh
    uint  flags;    // DI_FLAG_BILLBOARD | DI_FLAG_UNLIT
    float phase;
    uint  pad0;
    float pad1, pad2, pad3;
};

// Must match SceneUniforms in GameRenderer.swift — 176 bytes.
struct Scene {
    float4x4 viewProj;
    float4   eye;       // xyz camera position, w time
    float4   sun;       // xyz direction toward the sun, w intensity
    float4   sunColor;  // rgb sun colour, w hemisphere ambient
    float4   fog;       // rgb fog colour, w fog density
    float4   post;      // x exposure, y shake, z/w unused
    float4   camRight;  // xyz camera right axis (billboards)
    float4   camUp;     // xyz camera up axis (billboards)
};

#define FLAG_BILLBOARD 0x1u
#define FLAG_UNLIT     0x2u

struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 uv       [[attribute(2)]];
};

struct VertexOut {
    float4 position [[position]];
    float3 worldPos;
    float3 normal;
    float2 uv;
    float4 colour;
    float  emissive;
    float  phase;
    float  flags;
};

static float3 unpackRGB(uint rgba) {
    return float3(float(rgba & 0xffu),
                  float((rgba >> 8) & 0xffu),
                  float((rgba >> 16) & 0xffu)) * (1.0f / 255.0f);
}

static float unpackAlpha(uint rgba) {
    return float((rgba >> 24) & 0xffu) * (1.0f / 255.0f);
}

// ---------------------------------------------------------------------------
// Vertex stage
// ---------------------------------------------------------------------------
vertex VertexOut vertex_instanced(VertexIn in [[stage_in]],
                                  const device Instance* instances [[buffer(1)]],
                                  constant Scene& scene [[buffer(2)]],
                                  uint iid [[instance_id]]) {
    const Instance inst = instances[iid];
    VertexOut out;

    // Scale in local space first, then yaw, then translate.
    float3 local = float3(in.position.x * inst.sx,
                          in.position.y * inst.sy,
                          in.position.z * inst.sz);
    float c = cos(inst.rotY);
    float s = sin(inst.rotY);
    float3 rotated = float3(local.x * c + local.z * s,
                            local.y,
                           -local.x * s + local.z * c);

    float3 world;
    float3 normal = float3(in.normal.x * c + in.normal.z * s,
                           in.normal.y,
                          -in.normal.x * s + in.normal.z * c);

    if ((inst.flags & FLAG_BILLBOARD) != 0u) {
        // The quad lies in XZ locally; re-anchor it on the camera basis so it
        // always faces the viewer (health bars, ground glows, markers).
        world = float3(inst.x, inst.y, inst.z)
              + scene.camRight.xyz * local.x
              + scene.camUp.xyz * local.z;
        normal = normalize(scene.eye.xyz - world);
    } else {
        world = float3(inst.x, inst.y, inst.z) + rotated;
    }

    // Gentle idle bob/spin driven by the instance phase for living things.
    out.position = scene.viewProj * float4(world, 1.0f);
    out.worldPos = world;
    out.normal = normal;
    out.uv = in.uv;
    out.colour = float4(unpackRGB(inst.rgba), unpackAlpha(inst.rgba));
    out.emissive = inst.emissive;
    out.phase = inst.phase;
    out.flags = float(inst.flags);
    return out;
}

// ---------------------------------------------------------------------------
// Fragment stage
// ---------------------------------------------------------------------------
fragment float4 fragment_instanced(VertexOut in [[stage_in]],
                                   constant Scene& scene [[buffer(2)]]) {
    const bool unlit = (uint(in.flags) & FLAG_UNLIT) != 0u;

    float3 base = in.colour.rgb;
    float3 lit;
    if (unlit) {
        lit = base * (1.0f + in.emissive);
    } else {
        float3 n = normalize(in.normal);
        float ndl = max(dot(n, scene.sun.xyz), 0.0f);
        float hemi = 0.55f + 0.45f * n.y;
        float3 ambient = base * scene.sunColor.w * hemi;
        float3 diffuse = base * scene.sunColor.rgb * scene.sun.w * ndl;

        // Rim light so silhouettes read against a dark dungeon.
        float3 viewDir = normalize(scene.eye.xyz - in.worldPos);
        float rim = pow(1.0f - saturate(dot(viewDir, n)), 3.0f) * 0.22f;

        lit = ambient + diffuse + base * in.emissive + rim * scene.sunColor.rgb;
    }

    // Exponential distance fog toward the biome's atmosphere colour.
    float distanceToEye = distance(scene.eye.xyz, in.worldPos);
    float fogAmount = 1.0f - exp(-scene.fog.w * distanceToEye);
    lit = mix(lit, scene.fog.rgb, saturate(fogAmount));

    lit *= scene.post.x; // exposure (flashes when the player is hurt)

    float alpha = in.colour.a;
    if (unlit) {
        // Additive markers keep their authored alpha; do not fog them to death.
        alpha *= exp(-scene.fog.w * distanceToEye * 0.5f);
    }
    return float4(lit, alpha);
}
