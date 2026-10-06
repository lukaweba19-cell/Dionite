// ============================================================================
// Dionite — Rendering: 3D renderer interface with PBR materials,
// cascaded shadow maps, dynamic point lights, post-FX (bloom, vignette).
// Platform backends: Metal (iOS/macOS), Vulkan (desktop), OpenGL ES (Android).
// ============================================================================
#pragma once
#include "Core/Math/Matrix.h"
#include "Core/Math/Vector.h"
#include <string>
#include <vector>

namespace dionite::render {

struct Vertex {
    math::Vec3 position;
    math::Vec3 normal;
    math::Vec3 tangent;
    float u, v;
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

struct PBRMaterial {
    int albedoTex   = -1;
    int normalTex   = -1;
    int mraTex      = -1;     // metallic / roughness / AO packed
    int emissiveTex = -1;
    math::Vec3 albedoTint{1, 1, 1};
    float metallic  = 0.f;
    float roughness = 0.8f;
    float emissiveStrength = 0.f;
    bool castsShadow  = true;
    bool receivesShadow = true;
};

struct PointLight {
    math::Vec3 position;
    float      radius    = 8.f;
    math::Vec3 color{1, 0.8f, 0.6f};
    float      intensity = 1.f;
};

struct DirectionalLight {
    math::Vec3 direction{-0.4f, -1.f, -0.3f};
    math::Vec3 color{1, 0.95f, 0.85f};
    float      intensity = 1.2f;
    bool       castsShadow = true;
};

struct CameraUniforms {
    math::Mat4 view;
    math::Mat4 proj;
    math::Vec3 cameraPos;
    float      _pad = 0.f;
    DirectionalLight sun;
    math::Mat4 sunVP[4];    // 4-cascade shadow VP
    float      cascadeSplits[4]{ 8.f, 24.f, 60.f, 200.f };
};

struct DrawCommand {
    int meshId;
    PBRMaterial material;
    math::Mat4 transform;
    int instanceCount = 1;
};

class Renderer {
public:
    virtual ~Renderer() = default;
    virtual bool init(void* nativeWindow, int width, int height) = 0;
    virtual void resize(int width, int height) = 0;
    virtual int  uploadMesh(const Mesh& m) = 0;
    virtual int  uploadTexture(const std::string& path) = 0;

    virtual void beginFrame() = 0;
    virtual void setCamera(const CameraUniforms& cu) = 0;
    virtual void submitLight(const PointLight& l) = 0;
    virtual void submit(const DrawCommand& d) = 0;

    // 2D overlays (HUD, damage numbers, tooltips) — drawn after 3D scene.
    virtual void drawSprite2D(float x, float y, float w, float h, int textureId,
                              float r=1,float g=1,float b=1,float a=1) = 0;
    virtual void drawText2D(float x, float y, const std::string& text,
                            float scale=1.f, float r=1, float g=1, float b=1) = 0;

    virtual void endFrame() = 0;
    virtual void shutdown() = 0;
};

// Headless renderer used by the desktop validation harness and tests.
class NullRenderer : public Renderer {
public:
    bool init(void*, int, int) override { return true; }
    void resize(int, int) override {}
    int  uploadMesh(const Mesh&) override { return nextId_++; }
    int  uploadTexture(const std::string&) override { return nextId_++; }
    void beginFrame() override {}
    void setCamera(const CameraUniforms&) override {}
    void submitLight(const PointLight&) override {}
    void submit(const DrawCommand&) override {}
    void drawSprite2D(float, float, float, float, int, float, float, float, float) override {}
    void drawText2D(float, float, const std::string&, float, float, float, float) override {}
    void endFrame() override {}
    void shutdown() override {}
private:
    int nextId_ = 1;
};

} // namespace dionite::render
