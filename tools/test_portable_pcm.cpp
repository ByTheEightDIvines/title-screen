#include "../src/pcm_player_portable.h"
#include <chrono>
#include <thread>
#include <vector>
#include <cstdio>
int main() {
    std::vector<char> silence(176400, 0);
    PcmPlayer player;
    if (!player.queue(silence.data(), 17640, 0) || !player.play()) return 1;
    auto deadline = audioClockMilliseconds() + 2000;
    while (player.active() && audioClockMilliseconds() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (player.active()) return 2;
    player.update();
    if (!player.queue(silence.data(), silence.size(), 0) || !player.play()) return 3;
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    double seconds = 0;
    if (!player.positionSeconds(seconds) || seconds < .05 || seconds > .5) return 4;
    player.fade(30); deadline = audioClockMilliseconds() + 2000;
    while (player.active() && audioClockMilliseconds() < deadline) {
        player.update(); std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (player.active()) return 5;
    if (!player.queue(silence.data(), silence.size(), 0) || !player.play()) return 6;
    player.fade(0);
    if (player.active()) return 7;
    std::puts("PASS portable PCM: completion, restart, frame clock, timed fade, immediate stop");
}
