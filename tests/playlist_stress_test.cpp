//Include of required tools
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>
#include <set>
#include <thread>
#include <vector>

//Include of required files
#include "playlist/playlist.h"

//Stress test for trying out the capabilities of the program

using namespace std::chrono_literals;

static std::atomic<bool> g_stop{false};
static std::atomic<long> g_violations{0};
static std::atomic<long> g_ops{0};

static void violation(const char* what) {
    ++g_violations;
    std::cerr << "VIOLACION: " << what << "\n";
}

static void check_snapshot(const playlist::Playlist::Snapshot& snap) {
    std::set<playlist::TrackId> ids;
    for (const auto& t : snap.tracks) {
        if (!ids.insert(t.id).second) violation("id duplicado en snapshot");
    }
    if (snap.current && !ids.count(*snap.current)) violation("cursor apunta a pista inexistente");
}

int main() {
    playlist::Playlist pl;

    auto pick_id = [&](std::mt19937& rng) -> playlist::TrackId {
        auto snap = pl.snapshot();
        if (snap.tracks.empty()) return 0;
        return snap.tracks[rng() % snap.tracks.size()].id;
    };

    std::vector<std::thread> threads;

    for (int w = 0; w < 4; ++w) {
        threads.emplace_back([&, w] {
            std::mt19937 rng(1234 + w);
            while (!g_stop.load()) {
                switch (rng() % 8) {
                    case 0: case 1: case 2: pl.add("/tmp/x" + std::to_string(rng() % 1000) + ".mp3"); break;
                    case 3: pl.remove(pick_id(rng)); break;
                    case 4: pl.move(pick_id(rng), rng() % 10); break;
                    case 5: pl.select(pick_id(rng)); break;
                    case 6: (rng() % 2) ? (void)pl.advance() : (void)pl.previous(); break;
                    case 7: if (rng() % 10 == 0) pl.clear(); break;
                }
                ++g_ops;
            }
        });
    }

    for (int r = 0; r < 4; ++r) {
        threads.emplace_back([&] {
            std::uint64_t last_epoch = 0;
            while (!g_stop.load()) {
                check_snapshot(pl.snapshot());
                (void)pl.current();
                (void)pl.peek_next();
                (void)pl.size();
                const auto e = pl.selection_epoch();
                if (e < last_epoch) violation("selection_epoch retrocedio");
                last_epoch = e;
                ++g_ops;
            }
        });
    }

    std::atomic<long> finished{0};
    threads.emplace_back([&] {
        while (auto sel = pl.wait_for_current()) {
            std::this_thread::sleep_for(1ms);  
            if (pl.on_track_finished(sel->epoch)) ++finished;
        }
    });

    std::this_thread::sleep_for(3s);
    g_stop = true;
    for (std::size_t i = 0; i + 1 < threads.size(); ++i) threads[i].join();
    pl.close();
    threads.back().join();

    std::cout << "operaciones: " << g_ops.load() << ", pistas terminadas por el motor: "
              << finished.load() << ", violaciones: " << g_violations.load() << "\n";
    return g_violations.load() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
