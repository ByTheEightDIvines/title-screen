#include "../src/music_return.h"
#include <cstdio>

int main() {
    MusicReturn gate;
    auto check = [](bool ok, const char* reason) {
        if (!ok) std::printf("FAIL: %s\n", reason);
        return ok;
    };
    if (!check(!gate.ready(false, 9000) && !gate.blocked(), "idle must not restart music")) return 1;
    gate.begin();
    if (!check(gate.blocked() && !gate.ready(false, 10000), "queue delay must remain suppressed")) return 2;
    gate.finish();
    if (!check(!gate.ready(true, 15000) && !gate.ready(true, 30000), "skip fade must not count as silence")) return 3;
    if (!check(!gate.ready(false, 31000) && !gate.ready(false, 35999), "no native music before five full seconds")) return 4;
    if (!check(gate.ready(false, 36000) && gate.blocked(), "deadline reached; retain ownership until native receiver exists")) return 5;
    gate.cancel();
    if (!check(!gate.blocked() && !gate.ready(false, 90000), "title exit must cancel delayed playback")) return 6;
    gate.begin(); gate.finish();
    if (!check(!gate.ready(false, 100000), "replay starts a new silence interval")) return 7;
    if (!check(!gate.ready(true, 104000) && !gate.ready(false, 108000) && !gate.ready(false, 112999) && gate.ready(false, 113000), "resumed custom audio resets the silence clock")) return 8;
    gate.cancel(); gate.begin();
    if (!check(!gate.ready(true, 200000) && !gate.ready(false, 300000), "new intro cannot inherit old deadline")) return 9;
    gate.finish();
    if (!check(!gate.ready(false, 0) && !gate.ready(false, 4999) && gate.ready(false, 5000), "zero-based clock and exact five-second boundary")) return 10;
    std::puts("PASS: no overlap, five seconds after actual stop, fade handling, receiver wait, exit cancellation and replay");
}
