#pragma once
// The game's immediate-mode 3D drawing: Tessellator + MeshHelpers, used from the
// LevelRenderer::renderLevel hook.
#include <cstdint>
#include <memory>

#include "sdk/Math.h"

namespace sdk {

struct Color {
    float r, g, b, a;
};

enum class Primitive : int { Quad = 1, TriangleFan = 2, TriangleStrip = 3, LineList = 4 };

class Tessellator {
public:
    void begin(Primitive primitive, int numVertices);
    void vertex(float x, float y, float z);
    void color(const Color& c);
};

class MaterialPtr;  // shared material handle owned by the game

class ScreenContext {
public:
    Tessellator* tessellator() const;
    Color* shaderColor() const;
};

class LevelRenderer {
public:
    // World position the render origin is at (vertices are relative to it).
    Vec3 origin() const;
};

// Materials by name: "selection_box" (drawn over blocks), "ui_fill_color"...
MaterialPtr* material(const char* name);

void renderMeshImmediately(ScreenContext* screen, Tessellator* tess, MaterialPtr* material);

}  // namespace sdk
