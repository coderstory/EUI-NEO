// Verifies the open-file bridge contract (registration + pending queue) without
// touching the native event source, so it runs without a window server.
#include "core/platform/open_file_bridge.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> g_received;
int g_userdataHits = 0;

void recordPath(const char* path, void* userdata) {
    if (userdata != nullptr) {
        ++g_userdataHits;
    }
    g_received.push_back(path != nullptr ? std::string(path) : std::string());
}

void resetRecorder() {
    g_received.clear();
    g_userdataHits = 0;
}

bool drainAll() {
    char buffer[128];
    int drained = 0;
    while (eui_platform_consume_pending_open_file(buffer, static_cast<int>(sizeof(buffer))) != 0) {
        ++drained;
        if (drained > 64) {
            return false;
        }
    }
    return true;
}

bool pushIgnoresEmptyPaths() {
    eui_open_file_push_pending(nullptr);
    eui_open_file_push_pending("");
    char buffer[8];
    return eui_platform_consume_pending_open_file(buffer, static_cast<int>(sizeof(buffer))) == 0;
}

bool consumeReturnsPathsInFifoOrder() {
    eui_open_file_push_pending("/tmp/first.md");
    eui_open_file_push_pending("/tmp/second.md");

    char buffer[128];
    bool ok = eui_platform_consume_pending_open_file(buffer, static_cast<int>(sizeof(buffer))) == 1 &&
              std::string(buffer) == "/tmp/first.md";
    ok = ok && eui_platform_consume_pending_open_file(buffer, static_cast<int>(sizeof(buffer))) == 1 &&
              std::string(buffer) == "/tmp/second.md";
    // The queue must be empty afterwards.
    ok = ok && eui_platform_consume_pending_open_file(buffer, static_cast<int>(sizeof(buffer))) == 0;
    return ok;
}

bool consumeTruncatesButKeepsQueueUsable() {
    eui_open_file_push_pending("/tmp/a-very-long-file-name-that-does-not-fit.md");

    char buffer[8];
    const int consumed = eui_platform_consume_pending_open_file(buffer, static_cast<int>(sizeof(buffer)));
    const bool terminated = buffer[sizeof(buffer) - 1] == '\0' || std::string(buffer).size() <= sizeof(buffer) - 1;
    return consumed == 1 && terminated && drainAll();
}

bool dispatchInvokesRegisteredCallback() {
    resetRecorder();
    eui_platform_set_open_file_callback(&recordPath, &g_userdataHits);

    eui_open_file_push_pending("/tmp/one.md");
    eui_open_file_push_pending("/tmp/two.txt");
    const int dispatched = eui_open_file_dispatch_pending();

    const bool ok = dispatched == 2 && g_received.size() == 2 &&
                    g_received[0] == "/tmp/one.md" && g_received[1] == "/tmp/two.txt" &&
                    g_userdataHits == 2;
    // Dispatch drains the queue: a second call must be a no-op.
    return ok && eui_open_file_dispatch_pending() == 0;
}

bool detachedCallbackDropsPendingPaths() {
    resetRecorder();
    eui_platform_set_open_file_callback(&recordPath, nullptr);
    eui_platform_set_open_file_callback(nullptr, nullptr);

    eui_open_file_push_pending("/tmp/ignored.md");
    const int dispatched = eui_open_file_dispatch_pending();

    const bool ok = dispatched == 1 && g_received.empty();
    return ok && g_userdataHits == 0;
}

} // namespace

int main() {
    struct Check {
        const char* name;
        bool (*run)();
    };

    const Check checks[] = {
        {"push ignores empty paths", &pushIgnoresEmptyPaths},
        {"consume returns paths in FIFO order", &consumeReturnsPathsInFifoOrder},
        {"consume truncates oversized paths", &consumeTruncatesButKeepsQueueUsable},
        {"dispatch invokes the registered callback", &dispatchInvokesRegisteredCallback},
        {"detached callback drops pending paths", &detachedCallbackDropsPendingPaths},
    };

    int failures = 0;
    for (const Check& check : checks) {
        eui_platform_set_open_file_callback(nullptr, nullptr);
        drainAll();
        if (!check.run()) {
            std::cerr << "open_file_events: FAILED " << check.name << "\n";
            ++failures;
        }
    }

    eui_platform_set_open_file_callback(nullptr, nullptr);
    drainAll();
    return failures == 0 ? 0 : 1;
}
