#!/bin/bash
# Builds and runs every host-side test. Needs: g++ (C++17), python3 + Pillow + numpy, ffmpeg with libvorbis (only to make OGG/PNG references).
set -e; cd "$(dirname "$0")"
g++ -std=c++17 -O1 -Wall -Wextra -fsanitize=address,undefined test_core.cpp -o /tmp/wl_test_core && /tmp/wl_test_core ../pack/weaponloader
g++ -std=c++17 -O1 -fsanitize=address,undefined png_cli.cpp -o /tmp/wl_png_cli && python3 run_png_tests.py /tmp/wl_png_cli
g++ -std=c++17 -O2 -fsanitize=address,undefined ogg_cli.cpp -o /tmp/wl_ogg_cli && python3 run_ogg_tests.py /tmp/wl_ogg_cli
echo "ALL HOST TESTS PASSED"
