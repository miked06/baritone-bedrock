#pragma once
// The game's immediate-mode 3D drawing: Tessellator + MeshHelpers, used from the
// LevelRenderer::renderLevel hook.
#include <cstdint>
#include <cstddef>
#include <memory>

#include "sdk/Math.h"

namespace sdk {

struct Color {
    float r, g, b, a;
};

// Native ShaderColor: the renderer only uploads a changed uniform when dirty
// is set. Writing the four floats alone can keep an unrelated previous color.
struct ShaderColor {
    Color color;
    bool dirty;
    void set(Color value) { color = value; dirty = true; }
};
static_assert(offsetof(ShaderColor, dirty) == 0x10);

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
    ShaderColor* shaderColor() const;
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
