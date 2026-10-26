// Test helper: decode a PNG and dump "w h\n" + raw RGBA to stdout (compared against Pillow by tests/run_png_tests.py)
#include <cstdio>
#include <fstream>
#include <iterator>
#include "../core/png_decode.hpp"
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::ifstream f(argv[1], std::ios::binary); std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    wl::Image im; std::string e;
    if (!wl::decodePng(d.data(), d.size(), im, e)) { std::fprintf(stderr, "ERR %s\n", e.c_str()); return 1; }
    std::printf("%d %d\n", im.w, im.h); std::fwrite(im.rgba.data(), 1, im.rgba.size(), stdout); return 0;
}
