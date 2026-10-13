#include "baritone/cache/CachedWorld.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstring>
#include <optional>
#include <string_view>
#include <fstream>
#include <stdexcept>
#include <string>

#include "baritone/Settings.h"

namespace baritone {

namespace {

constexpr char kMagic[4] = {'B', 'B', 'R', 'C'};
constexpr uint32_t kVersion = 1;

class Writer {
public:
    std::vector<uint8_t> out;
    void u8(uint8_t v) { out.push_back(v); }
    void u16(uint16_t v) { put(&v, 2); }
    void u32(uint32_t v) { put(&v, 4); }
    void i16(int16_t v) { put(&v, 2); }
    void i32(int32_t v) { put(&v, 4); }
    void i64(int64_t v) { put(&v, 8); }
    void bytes(const void* p, std::size_t n) { put(p, n); }

private:
    void put(const void* p, std::size_t n) {  // x64 is little-endian
        auto b = static_cast<const uint8_t*>(p);
        out.insert(out.end(), b, b + n);
    }
};

class Reader {
public:
    Reader(const std::vector<uint8_t>& data) : data_(data) {}
    uint8_t u8() { return get<uint8_t>(); }
    uint16_t u16() { return get<uint16_t>(); }
    uint32_t u32() { return get<uint32_t>(); }
    int16_t i16() { return get<int16_t>(); }
    int32_t i32() { return get<int32_t>(); }
    int64_t i64() { return get<int64_t>(); }
    const uint8_t* bytes(std::size_t n) {
        need(n);
        const uint8_t* p = data_.data() + pos_;
        pos_ += n;
        return p;
    }

private:
    template <typename T>
    T get() {
        T v;
        std::memcpy(&v, bytes(sizeof(T)), sizeof(T));
        return v;
    }
    void need(std::size_t n) const {
        if (pos_ + n > data_.size()) throw std::runtime_error("fichier de cache tronque");
    }
    const std::vector<uint8_t>& data_;
    std::size_t pos_ = 0;
};

// (run length 1-255, value) pairs: the 2-bit data is mostly long runs of air and stone.
void rleEncode(Writer& w, const std::vector<uint8_t>& bits) {
    Writer body;
    for (std::size_t i = 0; i < bits.size();) {
        uint8_t v = bits[i];
        std::size_t run = 1;
        while (i + run < bits.size() && bits[i + run] == v && run < 255) ++run;
        body.u8(static_cast<uint8_t>(run));
        body.u8(v);
        i += run;
    }
    w.u32(static_cast<uint32_t>(body.out.size()));
    w.bytes(body.out.data(), body.out.size());
}

void rleDecode(Reader& r, std::vector<uint8_t>& bits) {
    uint32_t size = r.u32();
    if (size % 2) throw std::runtime_error("donnees RLE invalides");
    const uint8_t* p = r.bytes(size);
    std::size_t out = 0;
    for (uint32_t i = 0; i < size; i += 2) {
        uint8_t run = p[i], v = p[i + 1];
        if (out + run > bits.size()) throw std::runtime_error("donnees RLE trop longues");
        std::memset(bits.data() + out, v, run);
        out += run;
    }
    if (out != bits.size()) throw std::runtime_error("donnees RLE trop courtes");
}

std::vector<uint8_t> readAll(const std::filesystem::path& file) {
    std::ifstream f(file, std::ios::binary);
    if (!f) throw std::runtime_error("lecture impossible");
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// "r.<x>.<z>.bbr"
std::optional<std::pair<int, int>> parseRegionName(const std::string& name) {
    if (!name.starts_with("r.") || !name.ends_with(".bbr")) return std::nullopt;
    std::string_view rest(name.data() + 2, name.size() - 6);
    std::size_t dot = rest.find('.');
    if (dot == std::string_view::npos) return std::nullopt;
    int rx, rz;
    auto a = std::from_chars(rest.data(), rest.data() + dot, rx);
    auto b = std::from_chars(rest.data() + dot + 1, rest.data() + rest.size(), rz);
    if (a.ec != std::errc{} || a.ptr != rest.data() + dot || b.ec != std::errc{} || b.ptr != rest.data() + rest.size()) {
        return std::nullopt;
    }
    return std::make_pair(rx, rz);
}

int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

}  // namespace

// ---- CachedRegion ----

void CachedRegion::put(std::shared_ptr<const CachedChunk> chunk) {
    chunks_[index(chunk->cx(), chunk->cz())] = std::move(chunk);
    dirty_ = true;
}

void CachedRegion::write(const std::filesystem::path& file, const std::vector<std::shared_ptr<const CachedChunk>>& chunks,
                         int minY, int height) {
    Writer w;
    w.bytes(kMagic, 4);
    w.u32(kVersion);
    w.i32(minY);
    w.i32(height);
    w.u32(static_cast<uint32_t>(chunks.size()));
    for (const auto& c : chunks) {
        w.u16(static_cast<uint16_t>(index(c->cx(), c->cz())));
        w.i64(c->timestamp());
        rleEncode(w, c->bits());
        w.u16(static_cast<uint16_t>(c->special().size()));
        for (const auto& [name, positions] : c->special()) {
            w.u16(static_cast<uint16_t>(name.size()));
            w.bytes(name.data(), name.size());
            w.u32(static_cast<uint32_t>(positions.size()));
            for (const BlockPos& p : positions) {
                w.u8(static_cast<uint8_t>(p.x & 15));
                w.u8(static_cast<uint8_t>(p.z & 15));
                w.i16(static_cast<int16_t>(p.y));
            }
        }
    }
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("ecriture impossible");
        f.write(reinterpret_cast<const char*>(w.out.data()), static_cast<std::streamsize>(w.out.size()));
        if (!f) throw std::runtime_error("ecriture impossible");
    }
    std::filesystem::rename(tmp, file);  // replaces the old file
}

std::shared_ptr<CachedRegion> CachedRegion::read(const std::filesystem::path& file, int rx, int rz, int minY,
                                                 int height) {
    std::vector<uint8_t> data = readAll(file);
    Reader r(data);
    if (std::memcmp(r.bytes(4), kMagic, 4) != 0 || r.u32() != kVersion) throw std::runtime_error("format inconnu");
    auto region = std::make_shared<CachedRegion>(rx, rz);
    if (r.i32() != minY || r.i32() != height) return region;  // other world height: start over
    uint32_t count = r.u32();
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t idx = r.u16();
        if (idx >= kSize * kSize) throw std::runtime_error("index de chunk invalide");
        int cx = rx * kSize + (idx >> 5), cz = rz * kSize + (idx & 31);
        auto chunk = std::make_shared<CachedChunk>(cx, cz, minY, height);
        chunk->setTimestamp(r.i64());
        rleDecode(r, chunk->bits());
        uint16_t specials = r.u16();
        for (uint16_t s = 0; s < specials; ++s) {
            uint16_t len = r.u16();
            std::string name(reinterpret_cast<const char*>(r.bytes(len)), len);
            uint32_t n = r.u32();
            auto& list = chunk->special()[name];
            list.reserve(n);
            for (uint32_t k = 0; k < n; ++k) {
                int lx = r.u8(), lz = r.u8(), y = r.i16();
                list.push_back({(cx << 4) | lx, y, (cz << 4) | lz});
            }
        }
        region->chunks_[idx] = std::move(chunk);
    }
    // Java removeExpired: chunks cached longer ago than cachedChunksExpirySeconds are forgotten
    int64_t expiry = Settings::get().cachedChunksExpirySeconds;
    if (expiry >= 0) {
        int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        for (auto& c : region->chunks_)
            if (c && c->timestamp() < now - expiry * 1000) c.reset();
    }
    return region;
}

// ---- CachedWorld ----

CachedWorld::CachedWorld(std::filesystem::path dir, int minY, int height, std::array<StateId, 4> stateIds)
    : dir_(std::move(dir)), minY_(minY), height_(height), stateIds_(stateIds) {
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    for (const auto& e : std::filesystem::directory_iterator(dir_, ec)) {
        if (auto rxz = parseRegionName(e.path().filename().string())) onDisk_.insert(key(rxz->first, rxz->second));
    }
}

CachedWorld::~CachedWorld() {
    saveNow();
}

std::filesystem::path CachedWorld::fileOf(int rx, int rz) const {
    return dir_ / ("r." + std::to_string(rx) + "." + std::to_string(rz) + ".bbr");
}

std::shared_ptr<CachedRegion> CachedWorld::region(int rx, int rz) {
    int64_t k = key(rx, rz);
    {
        std::shared_lock lock(mutex_);
        if (auto it = regions_.find(k); it != regions_.end()) return it->second;
        if (!onDisk_.contains(k)) return nullptr;
    }
    std::unique_lock lock(mutex_);
    if (auto it = regions_.find(k); it != regions_.end()) return it->second;  // loaded meanwhile
    if (!onDisk_.contains(k)) return nullptr;
    std::shared_ptr<CachedRegion> region;
    try {
        region = CachedRegion::read(fileOf(rx, rz), rx, rz, minY_, height_);
    } catch (const std::exception&) {
        region = std::make_shared<CachedRegion>(rx, rz);  // unreadable: overwritten on next save
    }
    onDisk_.erase(k);
    regions_.emplace(k, region);
    return region;
}

std::shared_ptr<const CachedChunk> CachedWorld::chunk(int cx, int cz) {
    auto r = region(cx >> 5, cz >> 5);
    if (!r) return nullptr;
    std::shared_lock lock(mutex_);
    return r->chunk(cx, cz);
}

void CachedWorld::put(std::shared_ptr<const CachedChunk> chunk) {
    int rx = chunk->cx() >> 5, rz = chunk->cz() >> 5;
    region(rx, rz);  // reads the existing file first, if any
    std::unique_lock lock(mutex_);
    auto& r = regions_[key(rx, rz)];
    if (!r) r = std::make_shared<CachedRegion>(rx, rz);
    r->put(std::move(chunk));
}

std::vector<CachedWorld::Job> CachedWorld::collectDirty() {
    std::vector<Job> jobs;
    std::unique_lock lock(mutex_);
    for (auto& [k, r] : regions_) {
        if (!r->dirty()) continue;
        Job job{fileOf(r->rx(), r->rz()), {}};
        for (const auto& c : r->chunks())
            if (c) job.chunks.push_back(c);
        r->setDirty(false);
        jobs.push_back(std::move(job));
    }
    return jobs;
}

void CachedWorld::writeJobs(const std::vector<Job>& jobs) const {
    for (const Job& job : jobs) {
        try {
            CachedRegion::write(job.file, job.chunks, minY_, height_);
        } catch (const std::exception&) {
            // disk full / file locked: the region stays in memory and is retried next time it changes
        }
    }
}

void CachedWorld::saveAsync() {
    if (saver_.joinable()) saver_.join();
    std::vector<Job> jobs = collectDirty();
    if (jobs.empty()) return;
    saver_ = std::thread([this, jobs = std::move(jobs)] { writeJobs(jobs); });
}

void CachedWorld::saveNow() {
    if (saver_.joinable()) saver_.join();
    writeJobs(collectDirty());
}

void CachedWorld::prune(int playerCx, int playerCz, int distance) {
    if (saver_.joinable()) saver_.join();  // a pruned region is re-read from its file: it must be written
    int prx = playerCx >> 5, prz = playerCz >> 5;
    std::unique_lock lock(mutex_);
    std::erase_if(regions_, [&](const auto& entry) {
        const auto& r = entry.second;
        if (r->dirty() || std::max(std::abs(r->rx() - prx), std::abs(r->rz() - prz)) <= distance) return false;
        onDisk_.insert(entry.first);  // saved: reload from disk when needed
        return true;
    });
}

std::vector<BlockPos> CachedWorld::locationsOf(const std::function<bool(const std::string&)>& match,
                                               const BlockPos& center, std::size_t max, int maxRegionDistance) {
    std::vector<BlockPos> out;
    int crx = floorDiv(center.x, 512), crz = floorDiv(center.z, 512);
    for (int rx = crx - maxRegionDistance; rx <= crx + maxRegionDistance; ++rx) {
        for (int rz = crz - maxRegionDistance; rz <= crz + maxRegionDistance; ++rz) {
            auto r = region(rx, rz);
            if (!r) continue;
            std::shared_lock lock(mutex_);
            for (const auto& c : r->chunks()) {
                if (!c) continue;
                for (const auto& [name, positions] : c->special())
                    if (match(name)) out.insert(out.end(), positions.begin(), positions.end());
            }
        }
    }
    std::ranges::sort(out, [&](const BlockPos& a, const BlockPos& b) { return a.distSq(center) < b.distSq(center); });
    if (out.size() > max) out.resize(max);
    return out;
}

std::size_t CachedWorld::loadedRegions() const {
    std::shared_lock lock(mutex_);
    return regions_.size();
}

std::size_t CachedWorld::cachedChunks() const {
    std::shared_lock lock(mutex_);
    std::size_t n = 0;
    for (const auto& [k, r] : regions_)
        for (const auto& c : r->chunks()) n += c != nullptr;
    return n;
}

}  // namespace baritone
