// Test helper: decode an OGG with wl::decodeOgg and write raw float32 interleaved PCM to argv[2]; prints "ch rate frames".
#include <cstdio>
#include <fstream>
#include <iterator>
#include "../core/ogg_vorbis.hpp"
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    std::ifstream f(argv[1], std::ios::binary); std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    wl::Pcm p; std::string e;
    if (!wl::decodeOgg(d.data(), d.size(), p, e)) { std::fprintf(stderr, "ERR %s\n", e.c_str()); return 1; }
    FILE* o = std::fopen(argv[2], "wb"); std::fwrite(p.samples.data(), 4, p.samples.size(), o); std::fclose(o);
    std::printf("%d %d %zu\n", p.channels, p.rate, p.frames()); return 0;
}
