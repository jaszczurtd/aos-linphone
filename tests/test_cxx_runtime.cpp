// C++ runtime behaviour the linphone stack relies on:
//  - destructors of thread_local objects run when a thread ends, for
//    pthread_create() threads (bctoolbox) and std::thread;
//  - the destructor of a static object runs at exit in its place among the
//    atexit() handlers: after the ones registered later, before the ones
//    registered earlier.
// The order check happens during exit, so the program runs itself again
// with the argument ORDER; that run writes the outcome to a file, which the
// first run checks.
#include "aos_test.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <thread>

namespace {

constexpr int kThreads = 4;
std::atomic<int> destroyed{0};

struct Counted {
    int value = 0;
    ~Counted() { destroyed++; }
};
thread_local Counted tls_object;

void *touch(void *) {
    tls_object.value = 1;
    return nullptr;
}

// Waits up to a second for the destructors of threads that have ended.
int destroyed_after_wait(int expected) {
    for (int i = 0; i < 100 && destroyed.load() < expected; ++i) {
        aos_test_sleep_ms(10);
    }
    return destroyed.load();
}

bool static_destroyed = false;
bool later_handler_ran = false;
bool static_destroyed_before_later_handler = false;

struct Static {
    ~Static() { static_destroyed = true; }
};

void later_handler() {
    later_handler_ran = true;
    static_destroyed_before_later_handler = static_destroyed;
}

const char *const kOrderFile = "RAM:aos_cxx_order.txt";

// The first atexit() handler of the ORDER run, so it runs last.
void write_order() {
    FILE *f = fopen(kOrderFile, "w");
    if (f != nullptr) {
        fputs(later_handler_ran && !static_destroyed_before_later_handler && static_destroyed ? "ok" : "wrong",
              f);
        fclose(f);
    }
}

int order_run() {
    atexit(write_order);
    static Static object;
    (void)object;
    atexit(later_handler);
    return 0;
}

bool order_kept(const char *self) {
    remove(kOrderFile);
    char command[512];
    snprintf(command, sizeof(command), "%s ORDER", self);
    if (system(command) != 0) {
        return false;
    }
    char text[16] = "";
    FILE *f = fopen(kOrderFile, "r");
    if (f != nullptr) {
        if (fgets(text, sizeof(text), f) == nullptr) {
            text[0] = '\0';
        }
        fclose(f);
    }
    remove(kOrderFile);
    return strcmp(text, "ok") == 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "ORDER") == 0) {
        return order_run();
    }

    destroyed = 0;
    pthread_t threads[kThreads];
    for (auto &t : threads) {
        pthread_create(&t, nullptr, touch, nullptr);
    }
    for (auto &t : threads) {
        pthread_join(t, nullptr);
    }
    AOS_CHECK(destroyed_after_wait(kThreads) == kThreads,
              "C++: thread_local destructors run in pthread_create() threads");

    destroyed = 0;
    std::thread workers[kThreads];
    for (auto &t : workers) {
        t = std::thread([] { tls_object.value = 1; });
    }
    for (auto &t : workers) {
        t.join();
    }
    AOS_CHECK(destroyed_after_wait(kThreads) == kThreads, "C++: thread_local destructors run in std::thread threads");

    AOS_CHECK(order_kept(argv[0]), "C++: a static destructor runs between the atexit() handlers registered around it");
    return aos_test_result();
}
