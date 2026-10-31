// Offline test driver: test_engine in.raw out.raw sampleRate stretch lpfHz key scaleIdx blockSize
// raw = interleaved stereo float32 little endian
#include "../Source/StretchEngine.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
int main (int argc, char** argv)
{
    if (argc < 9) { std::fprintf (stderr, "usage\n"); return 1; }
    const double sr = std::atof (argv[3]);
    const float stretch = (float) std::atof (argv[4]);
    const float lpf = (float) std::atof (argv[5]);
    const int key = std::atoi (argv[6]), scale = std::atoi (argv[7]);
    const int block = std::atoi (argv[8]);
    FILE* f = std::fopen (argv[1], "rb");
    if (! f) return 2;
    std::fseek (f, 0, SEEK_END); const long bytes = std::ftell (f); std::fseek (f, 0, SEEK_SET);
    const long frames = bytes / 8;
    std::vector<float> in ((size_t) frames * 2), out ((size_t) frames * 2);
    if (std::fread (in.data(), 4, (size_t) frames * 2, f) != (size_t) frames * 2) return 3;
    std::fclose (f);

    slowmo::StretchEngine e;
    e.prepare (sr);
    e.setStretch (stretch); e.setLowpass (lpf); e.setScale (key, scale);
    std::vector<float> il ((size_t) block), ir ((size_t) block), ol ((size_t) block), orr ((size_t) block);
    for (long pos = 0; pos < frames; pos += block)
    {
        const int n = (int) std::min<long> (block, frames - pos);
        for (int i = 0; i < n; ++i) { il[(size_t) i] = in[(size_t) (pos + i) * 2]; ir[(size_t) i] = in[(size_t) (pos + i) * 2 + 1]; }
        e.process (il.data(), ir.data(), ol.data(), orr.data(), n);
        for (int i = 0; i < n; ++i) { out[(size_t) (pos + i) * 2] = ol[(size_t) i]; out[(size_t) (pos + i) * 2 + 1] = orr[(size_t) i]; }
    }
    f = std::fopen (argv[2], "wb");
    std::fwrite (out.data(), 4, out.size(), f);
    std::fclose (f);
    std::printf ("window=%d\n", e.windowSize());
    return 0;
}
