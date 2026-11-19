#include "platform/GhostTexturePack.h"

#include <Windows.h>
#include <wincodec.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <format>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace platform {
namespace {

using namespace winrt::Windows::Data::Json;
namespace fs = std::filesystem;
using Pack = GhostTexturePack;

constexpr std::size_t kMaxJsonBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxImageBytes = 8 * 1024 * 1024;
constexpr std::size_t kMaxImages = 256;
constexpr std::size_t kMaxBlocks = 128;

// Remove comments and trailing commas outside string literals. Pack metadata is
// commonly JSON with comments; let Windows.Data.Json validate the remaining JSON.
std::string stripJsonComments(std::string_view input) {
    std::string output;
    output.reserve(input.size());
    bool quoted = false;
    std::size_t last = 0;
    int depth = 0;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];
        if (quoted && c == '\\' && i + 1 < input.size()) {
            output.push_back(c);
            output.push_back(input[++i]);
            continue;
        }
        if (c == '"') quoted = !quoted;
        if (!quoted && c == '/' && i + 1 < input.size() && (input[i + 1] == '/' || input[i + 1] == '*')) {
            const bool line = input[i + 1] == '/';
            const auto end = line ? input.find_first_of("\r\n", i + 2) : input.find("*/", i + 2);
            if (!line && end == input.npos) throw std::runtime_error("Unterminated resource pack JSON comment");
            if (end == input.npos) break;
            i = line ? end - 1 : end + 1;
            output.push_back(' ');
            continue;
        }
        if (!quoted) {
            if ((c == '{' || c == '[') && ++depth > 64)
                throw std::runtime_error("Resource pack JSON nesting exceeds 64 levels");
            if (c == '}' || c == ']') {
                --depth;
                if (!output.empty() && output[last] == ',') output[last] = ' ';
            }
        }
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') last = output.size();
        output.push_back(c);
    }
    return output;
}

JsonObject readJson(const fs::path& file) {
    if (!fs::is_regular_file(file) || fs::file_size(file) > kMaxJsonBytes)
        throw std::runtime_error("Missing or oversized resource pack JSON: " + file.filename().string());
    std::ifstream stream(file, std::ios::binary);
    std::string data(kMaxJsonBytes + 1, '\0');
    stream.read(data.data(), static_cast<std::streamsize>(data.size()));
    data.resize(static_cast<std::size_t>(stream.gcount()));
    if (stream.bad() || data.size() > kMaxJsonBytes)
        throw std::runtime_error("Could not read bounded resource pack JSON");
    std::string_view utf8(data);
    if (utf8.starts_with("\xEF\xBB\xBF")) utf8.remove_prefix(3);
    JsonObject value{nullptr};
    if (!JsonObject::TryParse(winrt::to_hstring(stripJsonComments(utf8)), value))
        throw std::runtime_error("Invalid resource pack JSON: " + file.filename().string());
    return value;
}

IJsonValue field(const IJsonValue& value, winrt::param::hstring const& key) {
    if (!value || value.ValueType() != JsonValueType::Object) return nullptr;
    return value.GetObject().GetNamedValue(key, nullptr);
}

winrt::hstring asString(const IJsonValue& value) {
    return value && value.ValueType() == JsonValueType::String ? value.GetString() : winrt::hstring{};
}

winrt::hstring texturePath(IJsonValue value) {
    // Choose the first path from Bedrock's variant list for this static preview.
    for (int depth = 0; value && depth < 5; ++depth) {
        if (value.ValueType() == JsonValueType::String) return value.GetString();
        if (value.ValueType() == JsonValueType::Array) {
            const auto array = value.GetArray();
            value = array.Size() ? array.GetAt(0) : nullptr;
        } else if (value.ValueType() == JsonValueType::Object) {
            const auto path = asString(field(value, L"path"));
            if (!path.empty()) return path;
            value = field(value, L"variations");
        } else break;
    }
    return {};
}

fs::path pngFile(const fs::path& root, std::wstring path) {
    std::ranges::replace(path, L'\\', L'/');
    if (!path.starts_with(L"textures/") || path.find(L':') != path.npos || path.find(L'\0') != path.npos) return {};
    fs::path relative{path};
    for (const auto& part : relative)
        if (part == L"." || part == L"..") return {};
    if (CompareStringOrdinal(relative.extension().c_str(), -1, L".png", -1, TRUE) != CSTR_EQUAL) relative += L".png";
    std::error_code error;
    auto file = fs::canonical(root / relative, error);
    if (error) return {};
    const auto inside = file.lexically_relative(root);
    if (inside.empty() || inside.is_absolute() || *inside.begin() == L"..") return {};
    if (!fs::is_regular_file(file, error) || error || fs::file_size(file, error) > kMaxImageBytes || error) return {};
    return file;
}

auto previewColors() {
    std::array<baritone::Color, Pack::colorCount> colors{};
    for (int i = 0; i < 16; ++i) colors[i] = {i * 17, i * 17, i * 17};
    constexpr std::array<baritone::Color, 32> hues{{
        {101, 63, 45}, {133, 88, 56}, {162, 111, 75}, {190, 144, 106},
        {223, 183, 134}, {246, 218, 170}, {76, 52, 39}, {126, 111, 86},
        {92, 45, 42}, {136, 53, 49}, {180, 68, 58}, {230, 83, 66},
        {240, 141, 47}, {236, 190, 51}, {230, 218, 103}, {255, 233, 128},
        {56, 71, 35}, {77, 103, 46}, {108, 137, 61}, {152, 171, 74},
        {63, 136, 71}, {91, 181, 91}, {40, 93, 83}, {64, 162, 150},
        {52, 100, 146}, {69, 153, 206}, {106, 198, 231}, {62, 62, 135},
        {112, 80, 159}, {159, 103, 201}, {177, 72, 135}, {235, 145, 185},
    }};
    std::ranges::copy(hues, colors.begin() + 16);
    return colors;
}

std::shared_ptr<const Pack::Tile> readTile(IWICImagingFactory& factory, const fs::path& file,
                                         const std::array<baritone::Color, Pack::colorCount>& colors) {
    winrt::com_ptr<IWICBitmapDecoder> decoder;
    if (FAILED(factory.CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.put()))) return {};
    GUID format{};
    if (FAILED(decoder->GetContainerFormat(&format)) || format != GUID_ContainerFormatPng) return {};
    winrt::com_ptr<IWICBitmapFrameDecode> image;
    UINT width = 0, height = 0;
    if (FAILED(decoder->GetFrame(0, image.put())) || FAILED(image->GetSize(&width, &height)) ||
        !width || !height || width > 2048 || height > 2048) return {};
    winrt::com_ptr<IWICBitmapScaler> scaled;
    winrt::com_ptr<IWICFormatConverter> converted;
    if (FAILED(factory.CreateBitmapScaler(scaled.put())) ||
        FAILED(scaled->Initialize(image.get(), Pack::tileSize, Pack::tileSize, WICBitmapInterpolationModeFant)) ||
        FAILED(factory.CreateFormatConverter(converted.put())) ||
        FAILED(converted->Initialize(scaled.get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return {};
    std::array<uint8_t, Pack::tileSize * Pack::tileSize * 4> rgba{};
    if (FAILED(converted->CopyPixels(nullptr, Pack::tileSize * 4, static_cast<UINT>(rgba.size()), rgba.data()))) return {};
    auto tile = std::make_shared<Pack::Tile>();
    bool visible = false;
    for (std::size_t i = 0; i < tile->size(); ++i) {
        auto& index = (*tile)[i];
        index = Pack::transparent;
        if (rgba[i * 4 + 3] < 128) continue;
        visible = true;
        int best = std::numeric_limits<int>::max();
        for (std::size_t j = 0; j < colors.size(); ++j) {
            const int r = rgba[i * 4] - colors[j].r, g = rgba[i * 4 + 1] - colors[j].g, b = rgba[i * 4 + 2] - colors[j].b;
            const int error = 2 * r * r + 3 * g * g + b * b;
            if (error >= best) continue;
            best = error;
            index = static_cast<uint8_t>(j);
        }
    }
    return visible ? tile : nullptr;
}

}  // namespace

GhostTexturePack loadGhostTexturePack(const fs::path& folder, const baritone::SchematicWorkspace::Materials& materials) try {
    winrt::init_apartment();
    struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
    const auto root = fs::canonical(folder);
    if (!fs::is_directory(root)) throw std::runtime_error("Select an unpacked Bedrock resource pack folder");
    const auto atlasFile = fs::is_regular_file(root / "textures/terrain_texture.json")
        ? root / "textures/terrain_texture.json" : root / "terrain_texture.json";
    const auto aliases = field(readJson(atlasFile), L"texture_data");
    if (!aliases || aliases.ValueType() != JsonValueType::Object)
        throw std::runtime_error("The resource pack atlas is missing texture_data");
    const IJsonValue definitions = fs::is_regular_file(root / "blocks.json") ? readJson(root / "blocks.json") : nullptr;
    auto decoder = winrt::create_instance<IWICImagingFactory>(CLSID_WICImagingFactory);
    GhostTexturePack pack;
    pack.colors = previewColors();
    std::map<fs::path, std::shared_ptr<const Pack::Tile>> tiles;
    std::vector<const std::pair<const std::string, uint64_t>*> ordered;
    for (const auto& item : materials.blocks) ordered.push_back(&item);
    std::ranges::sort(ordered, [](const auto* a, const auto* b) {
        return a->second != b->second ? a->second > b->second : a->first < b->first;
    });
    if (ordered.size() > kMaxBlocks) ordered.resize(kMaxBlocks);
    constexpr const wchar_t* faces[] = {L"down", L"up", L"north", L"south", L"west", L"east"};
    for (const auto* item : ordered) {
        const auto& name = item->first;
        std::string_view localName(name);
        if (localName.starts_with("minecraft:")) localName.remove_prefix(10);
        const auto id = winrt::to_hstring(localName);
        auto definition = field(definitions, winrt::to_hstring(name));
        if (!definition) definition = field(definitions, id);
        const auto textures = field(definition, L"textures");
        const auto sharedAlias = asString(textures);
        Pack::Block block;
        for (std::size_t face = 0; face < block.size(); ++face) {
            auto alias = sharedAlias;
            if (alias.empty()) alias = asString(field(textures, faces[face]));
            if (alias.empty() && face >= 2) alias = asString(field(textures, L"side"));
            if (alias.empty()) alias = asString(field(textures, L"*"));
            if (alias.empty()) alias = id;
            auto mapped = texturePath(field(field(aliases, alias), L"textures"));
            const auto relative = mapped.empty() ? std::wstring(L"textures/blocks/") + std::wstring(id) : std::wstring(mapped);
            auto file = pngFile(root, relative);
            if (file.empty()) continue;
            auto it = tiles.find(file);
            if (it == tiles.end() && tiles.size() < kMaxImages) it = tiles.emplace(file, readTile(*decoder, file, pack.colors)).first;
            if (it != tiles.end()) block[face] = it->second;
        }
        if (std::ranges::any_of(block, [](const auto& tile) { return static_cast<bool>(tile); }))
            pack.blocks.emplace(name, std::move(block));
    }
    pack.imageCount = static_cast<std::size_t>(std::ranges::count_if(tiles, [](const auto& tile) { return static_cast<bool>(tile.second); }));
    return pack;
} catch (const winrt::hresult_error& error) {
    throw std::runtime_error(std::format("Windows texture import failed (0x{:08X})", static_cast<uint32_t>(error.code().value)));
} catch (const fs::filesystem_error& error) {
    throw std::runtime_error(std::format("Could not access the resource pack folder or files (code {})", error.code().value()));
}

}  // namespace platform
