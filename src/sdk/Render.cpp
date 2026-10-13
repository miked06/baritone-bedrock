#include "sdk/Render.h"

#include <map>
#include <memory>
#include <string>

#include "memory/Memory.h"
#include "memory/Signatures.h"
#include "sdk/Game.h"

namespace sdk {

namespace {

constexpr std::ptrdiff_t kScreenContext_shaderColor = 0x30;  // Color*
constexpr std::ptrdiff_t kScreenContext_tessellator = 0xB8;
constexpr std::ptrdiff_t kLevelRenderer_player = 0x468;      // LevelRendererPlayer*
constexpr std::ptrdiff_t kLevelRendererPlayer_origin = 0x660;

// HashedString's hash: FNV offset/prime, multiply then xor (FNV-1, not FNV-1a).
uint64_t hashedStringHash(const std::string& s) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (char c : s) {
        h *= 0x100000001b3ULL;
        h ^= static_cast<uint64_t>(static_cast<int64_t>(c));
    }
    return h;
}

}  // namespace

void Tessellator::begin(Primitive primitive, int numVertices) {
    using Fn = void(__fastcall*)(Tessellator*, int, Primitive, int, bool);
    sigs::Tessellator_begin.as<Fn>()(this, 0, primitive, numVertices, false);
}

void Tessellator::vertex(float x, float y, float z) {
    using Fn = void(__fastcall*)(Tessellator*, float, float, float);
    sigs::Tessellator_vertex.as<Fn>()(this, x, y, z);
}

void Tessellator::color(const Color& c) {
    using Fn = void(__fastcall*)(Tessellator*, const Color&);
    sigs::Tessellator_color.as<Fn>()(this, c);
}

Tessellator* ScreenContext::tessellator() const {
    return mem::member<Tessellator*>(this, kScreenContext_tessellator);
}

Color* ScreenContext::shaderColor() const {
    return mem::member<Color*>(this, kScreenContext_shaderColor);
}

Vec3 LevelRenderer::origin() const {
    void* player = mem::member<void*>(this, kLevelRenderer_player);
    return player ? mem::member<Vec3>(player, kLevelRendererPlayer_origin) : Vec3{};
}

MaterialPtr* material(const char* name) {
    // Materials are created once by the game's common material group and kept forever
    // (the shared_ptr is never released, so the game's allocator owns everything).
    static std::map<std::string, std::shared_ptr<MaterialPtr>>* cache = new std::map<std::string, std::shared_ptr<MaterialPtr>>();
    auto it = cache->find(name);
    if (it != cache->end()) return it->second.get();
    void* group = sigs::RenderMaterialGroup_common.as<void*>();
    HashedString hashed{hashedStringHash(name), name, nullptr};
    std::shared_ptr<MaterialPtr> result;
    mem::callVirtual<void, std::shared_ptr<MaterialPtr>&, const HashedString&>(group, 1, result, hashed);
    MaterialPtr* raw = result.get();
    cache->emplace(name, std::move(result));
    return raw;
}

void renderMeshImmediately(ScreenContext* screen, Tessellator* tess, MaterialPtr* mat) {
    alignas(16) char scratch[0x58]{};
    using Fn = void(__fastcall*)(ScreenContext*, Tessellator*, MaterialPtr*, char*);
    sigs::MeshHelpers_renderImmediately.as<Fn>()(screen, tess, mat, scratch);
}

}  // namespace sdk
