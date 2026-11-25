/* emu.h — the least of MAME's device framework that its sp0256.cpp needs,
 * so that the file compiles into test_sp0256 as a tool (design.md §12.9;
 * MAME is never source in the tree, D2, D8). Nothing here is MAME's: a
 * stand-in for each name the file uses, doing as little as it can. The
 * stream collects what put_int is given; the callbacks record their
 * last level. */
#ifndef PICO_COCO_MAME_SHIM_EMU_H
#define PICO_COCO_MAME_SHIM_EMU_H

#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>

typedef uint32_t offs_t;
#define ATTR_COLD
#define NAME(x) x, #x
#define FUNC(x) 0
#define DEVICE_SELF ""
#define DECLARE_DEVICE_TYPE(type, cls) extern int type;
#define DEFINE_DEVICE_TYPE(type, cls, short, full) int type;
#define TIMER_CALLBACK_MEMBER(name) void name(int32_t param = 0)

struct machine_config {};
struct attotime {
    double s;
    static const attotime zero;
    static attotime from_hz(double) { return attotime{0}; }
    bool operator>(const attotime &o) const { return s > o.s; }
};
inline const attotime attotime::zero{0};
struct running_machine {
    attotime time() const { return attotime{0}; }
    bool side_effects_disabled() const { return false; }
};
struct emu_timer {
    void adjust(attotime, int = 0, attotime = attotime{0}) {}
};

/* The stream: n samples asked for, each put kept. */
struct sound_stream {
    int16_t *out = nullptr;
    int n = 0;
    void update() {}
    int samples() const { return n; }
    void put_int(int, int i, int v, int) { if (i < n) out[i] = (int16_t)v; }
    void set_sample_rate(int) {}
};

struct device_t {
    running_machine m;
    uint32_t clk;
    device_t(const machine_config &, int, const char *, device_t *, uint32_t clock) : clk(clock) {}
    virtual ~device_t() {}
    uint32_t clock() const { return clk; }
    running_machine &machine() { return m; }
    void set_unscaled_clock(uint32_t c) { clk = c; }
    template <typename T> void save_item(T &, const char *) {}
    template <typename T> void save_pointer(T &, const char *, size_t) {}
    template <typename F, typename O> emu_timer *timer_alloc(F, O) { static emu_timer t; return &t; }
    virtual void device_start() {}
    virtual void device_reset() {}
};
struct device_sound_interface {
    device_sound_interface(const machine_config &, device_t &) {}
    virtual ~device_sound_interface() {}
    sound_stream *stream_alloc(int, int, int) { static sound_stream s; return &s; }
    virtual void sound_stream_update(sound_stream &) = 0;
};
/* The region a device is given: whatever mame_shim_region holds when it
 * is constructed. */
inline uint8_t *mame_shim_region = nullptr;
template <typename T> struct required_region_ptr {
    T *p = nullptr;
    required_region_ptr(device_t &, const char *) : p((T *)mame_shim_region) {}
    T &operator[](size_t i) { return p[i]; }
    operator T *() { return p; }
};
struct devcb_write_line {
    int last = -1;
    explicit devcb_write_line(device_t &) {}
    void operator()(int v) { last = v; }
    bool isunset() const { return false; }
    devcb_write_line &bind() { return *this; }
};

#endif
