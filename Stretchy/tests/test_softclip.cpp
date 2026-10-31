#include "../Source/StretchEngine.h"
#include <cstdio>
int main()
{
    float prev = -1000.0f;
    for (int i = -40000; i <= 40000; ++i)
    {
        const float x = (float) i * 0.0005f;           // -20 .. +20
        const float y = slowmo::softClip (x);
        if (std::fabs (x) <= 0.7f && y != x) { std::printf ("not identity at %f\n", x); return 1; }
        if (std::fabs (y) > 1.0f) { std::printf ("exceeds 1.0 at %f -> %f\n", x, y); return 2; }
        if (y < prev) { std::printf ("not monotonic at %f\n", x); return 3; }
        prev = y;
    }
    std::printf ("ok");
    return 0;
}
