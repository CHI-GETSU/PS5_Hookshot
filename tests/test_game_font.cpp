// The game's menu font reader (gpu/vk/vk_game_font.h) on files of its own making, in a folder laid out as a romfs: a SARC with
// the dictionary in a zstd frame, a font XOR-ed behind its 8-byte header in another. No game file is read. The good case
// gives the font's bytes back; a truncated frame, a name offset past the archive, a name that only starts like the one asked
// for, a wrong size word, a wrong font tag and a font with no Latin letters (argv[3]: only a Hebrew letter) give nothing.
#include "../gpu/vk/vk_game_font.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#define CHECK(name, condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL %s at line %d\n", name, __LINE__); return 1; } } while (0)

using Bytes = std::vector<unsigned char>;
namespace fs = std::filesystem;
using namespace mkwvk::game_font;

static void put32(Bytes& b, uint32_t v) {
    for (int i = 0; i < 4; i++) b.push_back((unsigned char)(v >> (8 * i)));
}
/// A zstd frame of raw blocks, its content size in its header (single segment)
static Bytes zframe(const Bytes& data) {
    Bytes f;
    put32(f, 0xFD2FB528u);
    if (data.size() < 256) f.push_back(0x20), f.push_back((unsigned char)data.size());   // (single segment, a 1-byte size)
    else {
        f.push_back(0x60);   // (single segment, a 2-byte size less 256)
        const unsigned v = (unsigned)data.size() - 256;
        f.push_back((unsigned char)v), f.push_back((unsigned char)(v >> 8));
    }
    size_t at = 0;
    do {
        const size_t n = std::min<size_t>(data.size() - at, 1000);
        const uint32_t h = (at + n == data.size() ? 1u : 0u) | (0u << 1) | (uint32_t)n << 3;   // (last?, raw, size)
        f.push_back((unsigned char)h), f.push_back((unsigned char)(h >> 8)), f.push_back((unsigned char)(h >> 16));
        f.insert(f.end(), data.begin() + at, data.begin() + at + n);
        at += n;
    } while (at < data.size());
    return f;
}
/// A SARC of one file (name, data); name_offset_words: the name's offset as the node gives it (0: right)
static Bytes sarc(const char* name, const Bytes& data, uint32_t name_offset_words = 0) {
    Bytes names(name, name + strlen(name) + 1);
    while (names.size() % 4) names.push_back(0);
    const uint32_t header = 0x14, sfat = 12 + 16, sfnt = 8, data_at = header + sfat + sfnt + (uint32_t)names.size();
    Bytes a;
    a.insert(a.end(), {'S', 'A', 'R', 'C', 0x14, 0, 0xFF, 0xFE});
    put32(a, data_at + (uint32_t)data.size());
    put32(a, data_at);
    a.insert(a.end(), {0, 1, 0, 0});
    a.insert(a.end(), {'S', 'F', 'A', 'T', 0x0C, 0, 1, 0});
    put32(a, 0x65);
    put32(a, 0);                                    // (the name's hash: not read)
    put32(a, 0x01000000u | name_offset_words);      // (a name, at this many words into the names)
    put32(a, 0);
    put32(a, (uint32_t)data.size());
    a.insert(a.end(), {'S', 'F', 'N', 'T', 0x08, 0, 0, 0});
    a.insert(a.end(), names.begin(), names.end());
    a.insert(a.end(), data.begin(), data.end());
    return a;
}
/// A .bfotf: the font XOR-ed with key behind its header (size_delta: a wrong size word)
static Bytes bfotf(const Bytes& font, uint32_t key, int size_delta = 0) {
    Bytes b;
    put32(b, 0x18029a7fu ^ key);
    const uint32_t size = (uint32_t)font.size() + size_delta;
    const unsigned char be[4] = {(unsigned char)(size >> 24), (unsigned char)(size >> 16), (unsigned char)(size >> 8), (unsigned char)size};
    for (int i = 0; i < 4; i++) b.push_back(be[i] ^ (unsigned char)(key >> (8 * i)));
    for (size_t i = 0; i < font.size(); i++) b.push_back(font[i] ^ (unsigned char)(key >> (8 * (i & 3))));
    return b;
}
static void write(const fs::path& p, const Bytes& b) {
    fs::create_directories(p.parent_path());
    FILE* f = fopen(p.string().c_str(), "wb");
    fwrite(b.data(), 1, b.size(), f);
    fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 4) return 2;
    const fs::path root = argv[1];
    Bytes latin, hebrew;   // (tiny TrueType fonts of test_game_font.py's: 'A' and 'a'; one Hebrew letter only)
    if (!read_file(argv[2], latin) || !read_file(argv[3], hebrew)) return 2;
    Bytes dict(64);
    for (size_t i = 0; i < dict.size(); i++) dict[i] = (unsigned char)(i * 7 + 3);
    const Bytes& font = latin;
    CHECK("the Latin font has Latin letters", has_latin(latin));
    CHECK("the Hebrew font has none", !has_latin(hebrew));
    CHECK("not a font at all", !has_latin(Bytes(64, 7)));
    const uint32_t key = 0xA1B2C3D4u;
    const char* scalable = "Font2/Scalable/";
    auto set_up = [&](const fs::path& dir, const Bytes& pack, const Bytes& raw_font) {
        write(dir / "Pack/ZsDic.pack.zs", pack);
        write(dir / (std::string(scalable) + "Test.Nin_Ounce_NVN2.bfotf.zs"), raw_font);
    };

    // ---- good: the font's own bytes ----
    set_up(root / "good", zframe(sarc("zs.zsdic", dict)), zframe(bfotf(font, key)));
    {
        Reader r;
        CHECK("opens", r.open((root / "good").string()));
        CHECK("the font back", r.read("Test") == font);
        CHECK("a font it does not have", r.read("Other").empty());
    }
    // ---- a TrueType font (its tag 00 01 00 00) too ----
    {
        Bytes ttf = font;
        ttf[0] = 0, ttf[1] = 1, ttf[2] = 0, ttf[3] = 0;
        CHECK("a TrueType font", bfotf_font(bfotf(ttf, key)) == ttf);
    }
    // ---- a font of another script: passed over, so the menu takes Libertinus ----
    {
        set_up(root / "hebrew", zframe(sarc("zs.zsdic", dict)), zframe(bfotf(hebrew, key)));
        Reader r;
        CHECK("opens", r.open((root / "hebrew").string()));
        CHECK("no Latin letters: not used", r.read("Test").empty());
        CHECK("though it is a font", bfotf_font(bfotf(hebrew, key)) == hebrew);
    }
    // ---- the archive: a truncated frame, a name offset past it, a longer name that starts the same ----
    {
        Bytes pack = zframe(sarc("zs.zsdic", dict));
        pack.resize(pack.size() - 10);
        set_up(root / "truncated", pack, zframe(bfotf(font, key)));
        Reader r;
        CHECK("a truncated dictionary pack", !r.open((root / "truncated").string()));
    }
    {
        set_up(root / "offset", zframe(sarc("zs.zsdic", dict, 0x400000)), zframe(bfotf(font, key)));
        Reader r;
        CHECK("a name offset past the archive", !r.open((root / "offset").string()));
    }
    {
        set_up(root / "prefix", zframe(sarc("zs.zsdicX", dict)), zframe(bfotf(font, key)));
        Reader r;
        CHECK("only the exact name", !r.open((root / "prefix").string()));
    }
    {
        Bytes a = sarc("zs.zsdic", dict);
        a[0] = 'X';
        Bytes out;
        CHECK("not a SARC", !sarc_file(a, "zs.zsdic", out));
        CHECK("a SARC too short", !sarc_file(Bytes(a.begin(), a.begin() + 0x10), "zs.zsdic", out));
    }
    // ---- the font: a truncated frame, a wrong size word, a wrong tag ----
    {
        Bytes f = zframe(bfotf(font, key));
        f.resize(f.size() / 2);
        set_up(root / "font-truncated", zframe(sarc("zs.zsdic", dict)), f);
        Reader r;
        CHECK("opens", r.open((root / "font-truncated").string()));
        CHECK("a truncated font frame", r.read("Test").empty());
    }
    CHECK("a wrong size word", bfotf_font(bfotf(font, key, 1)).empty() && bfotf_font(bfotf(font, key, -1)).empty());
    {
        Bytes bad = font;
        bad[0] = 'X';
        CHECK("a wrong tag", bfotf_font(bfotf(bad, key)).empty());
    }
    CHECK("too short for a header", bfotf_font(Bytes(10, 0)).empty());
    CHECK("the header alone", bfotf_font(bfotf(Bytes(), key)).empty());
    {
        Reader r;
        CHECK("no folder", !r.open("") && r.read("Test").empty());
        CHECK("a folder with nothing", !r.open((root / "none").string()));
    }
    std::printf("PASS: the game font reader on synthetic files\n");
    return 0;
}
