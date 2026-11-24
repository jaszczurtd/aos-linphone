// C++ code next to aos_compat.h, as in liblinphone: methods named like
// socket calls (send, connect, close, select...) must compile, and the
// global calls must reach libaosnet's C functions from std::thread workers.
#include "aos_test.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace {

class Channel {
public:
    explicit Channel(int fd)
        : mFd(fd) {}
    ~Channel() { close(); }

    int bind(const sockaddr_in &addr) { return ::bind(mFd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)); }
    int connect(const sockaddr_in &addr) {
        return ::connect(mFd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
    }
    ssize_t send(const std::string &text) { return ::send(mFd, text.data(), text.size(), 0); }
    std::string recv() {
        char buf[64];
        ssize_t n = ::recv(mFd, buf, sizeof(buf), 0);
        return n > 0 ? std::string(buf, static_cast<size_t>(n)) : std::string();
    }
    bool select(int timeoutMs) {
        pollfd p = {mFd, POLLIN, 0};
        return ::poll(&p, 1, timeoutMs) == 1;
    }
    void close() {
        if (mFd >= 0) {
            ::close(mFd);
            mFd = -1;
        }
    }
    sockaddr_in name() const {
        sockaddr_in addr;
        socklen_t len = sizeof(addr);
        ::getsockname(mFd, reinterpret_cast<sockaddr *>(&addr), &len);
        return addr;
    }

private:
    int mFd;
};

sockaddr_in loopback() {
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return addr;
}

} // namespace

int main() {
    Channel a(::socket(AF_INET, SOCK_DGRAM, 0));
    Channel b(::socket(AF_INET, SOCK_DGRAM, 0));
    sockaddr_in any = loopback();
    AOS_CHECK(a.bind(any) == 0 && b.bind(any) == 0, "C++: bind() through a method of the same name");
    AOS_CHECK(a.connect(b.name()) == 0 && b.connect(a.name()) == 0, "C++: connect() of two UDP sockets");

    std::atomic<bool> echoed{false};
    std::thread worker([&] {
        if (b.select(2000)) {
            std::string text = b.recv();
            echoed = b.send(text + "!") == static_cast<ssize_t>(text.size() + 1);
        }
    });
    AOS_CHECK(a.send("ping") == 4, "C++: send() in the main thread");
    worker.join();
    AOS_CHECK(echoed.load(), "C++: a std::thread worker receives and answers");
    AOS_CHECK(a.select(2000) && a.recv() == "ping!", "C++: the answer arrives");

    std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    char text[INET_ADDRSTRLEN];
    in_addr lo = {htonl(INADDR_LOOPBACK)};
    AOS_CHECK(::inet_ntop(AF_INET, &lo, text, sizeof(text)) != nullptr && std::strcmp(text, "127.0.0.1") == 0,
              "C++: inet_ntop()");
    return aos_test_result();
}
