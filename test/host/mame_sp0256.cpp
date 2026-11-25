/* mame_sp0256.cpp — MAME's SP0256, from the pinned fetch in reference/mame
 * (tools/mame/fetch-mame.sh), behind a C interface for test_sp0256
 * (design.md §12.9). A tool, linked only into that test (D2, D8). */
#include "emu.h"
#include "sp0256.h"

#include "mame_sp0256.h"

namespace {
struct probe : sp0256_device {
    probe() : sp0256_device(machine_config(), "sp0256", nullptr, 3120000) {}
    void start() { device_start(); device_reset(); }
    void run(int16_t *out, int n) {
        sound_stream s;
        s.out = out;
        s.n = n;
        sound_stream_update(s);
    }
};
probe *g;
uint8_t mem[0x10000];
}

extern "C" void mame_sp0256_init(const uint8_t *al2) {
    std::memset(mem, 0, sizeof mem);
    std::memcpy(mem + 0x1000, al2, 0x800);
    delete g;
    mame_shim_region = mem;
    g = new probe();
    g->start();
}

extern "C" int mame_sp0256_lrq(void) { return g->lrq_r(); }
extern "C" int mame_sp0256_sby(void) { return g->sby_r(); }
extern "C" void mame_sp0256_ald(uint8_t a) { g->ald_w(a); }

/* One sample, in the units the stream was given (the filter's sum
 * limited to 14 bits, times 4). */
extern "C" int16_t mame_sp0256_sample(void) {
    int16_t s = 0;
    g->run(&s, 1);
    return s;
}
