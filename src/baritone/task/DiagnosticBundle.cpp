#include "baritone/task/DiagnosticBundle.h"
#include <cstdint>
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>

namespace baritone {
namespace {
void u16(std::ostream& out,uint16_t value) { out.put(char(value)); out.put(char(value>>8)); }
void u32(std::ostream& out,uint32_t value) { u16(out,uint16_t(value)); u16(out,uint16_t(value>>16)); }
uint32_t crc32(const std::string& data) {
    uint32_t crc=0xffffffff;
    for(unsigned char byte:data) { crc^=byte; for(int bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xedb88320:0); }
    return ~crc;
}
}
std::string diagnosticFileTail(const std::filesystem::path& file,std::size_t limit) {
    if(limit>2*1024*1024)throw std::invalid_argument("Diagnostic file limit exceeds two MiB");
    if(!std::filesystem::exists(file))return "File is not present.\n";
    if(std::filesystem::is_symlink(std::filesystem::symlink_status(file)) || !std::filesystem::is_regular_file(file))throw std::invalid_argument("Diagnostic source must be a regular file");
    std::ifstream input(file,std::ios::binary|std::ios::ate);
    if(!input || input.tellg()<0)throw std::runtime_error("Could not read diagnostic source");
    const auto size=static_cast<uint64_t>(input.tellg());
    std::string result(static_cast<std::size_t>(std::min<uint64_t>(size,limit)),'\0');
    input.seekg(static_cast<std::streamoff>(size-result.size()));
    if(!result.empty() && !input.read(result.data(),static_cast<std::streamsize>(result.size())))throw std::runtime_error("Diagnostic source changed or could not be read");
    return result;
}
void writeDiagnosticBundle(const std::filesystem::path& output,const std::map<std::string,std::string>& files) {
    if(files.empty() || files.size()>16)throw std::invalid_argument("Diagnostic archive needs 1..16 entries");
    uint64_t bytes=0;
    for(const auto& [name,data]:files) {
        bytes+=data.size();
        if(name.empty() || name.size()>128 || name=="." || name==".." || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos || data.size()>2*1024*1024 || bytes>8*1024*1024)
            throw std::invalid_argument("Invalid diagnostic entry or archive exceeds size limits");
    }
    if(std::filesystem::exists(output))throw std::invalid_argument("Diagnostic output already exists; choose a new file");
    auto temporary=output; temporary+=".baritone-report.tmp";
    if(std::filesystem::exists(temporary))throw std::invalid_argument("Diagnostic temporary output already exists; choose a new file");
    if(!output.parent_path().empty())std::filesystem::create_directories(output.parent_path());
    try {
        std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
        if(!out)throw std::runtime_error("Could not create diagnostic output");
        struct Entry { std::string name; uint32_t crc,size,offset; };
        std::vector<Entry> entries;
        for(const auto& [name,data]:files) {
            Entry e{name,crc32(data),uint32_t(data.size()),uint32_t(out.tellp())};
            u32(out,0x04034b50);u16(out,20);u16(out,0x800);u16(out,0);u16(out,0);u16(out,33); // UTF-8, stored, 1980-01-01
            u32(out,e.crc);u32(out,e.size);u32(out,e.size);u16(out,uint16_t(name.size()));u16(out,0);
            out.write(name.data(),name.size());out.write(data.data(),data.size());entries.push_back(std::move(e));
        }
        const auto central=uint32_t(out.tellp());
        for(const auto& e:entries) {
            u32(out,0x02014b50);u16(out,20);u16(out,20);u16(out,0x800);u16(out,0);u16(out,0);u16(out,33);
            u32(out,e.crc);u32(out,e.size);u32(out,e.size);u16(out,uint16_t(e.name.size()));u16(out,0);u16(out,0);u16(out,0);u16(out,0);u32(out,0);u32(out,e.offset);
            out.write(e.name.data(),e.name.size());
        }
        const auto end=uint32_t(out.tellp());
        u32(out,0x06054b50);u16(out,0);u16(out,0);u16(out,uint16_t(entries.size()));u16(out,uint16_t(entries.size()));u32(out,end-central);u32(out,central);u16(out,0);
        out.close();if(!out)throw std::runtime_error("Could not finish diagnostic archive");
        std::filesystem::rename(temporary,output);
    } catch(...) { std::error_code ignored;std::filesystem::remove(temporary,ignored);throw; }
}
}
