#include <chrono>
#include <iostream>
#include <thread>

// Keepalive test target for attach scenarios. Unlike the launch target, this app
// is started independently and must stay alive long enough for the debugger to
// attach to a live process, enumerate threads, and capture a stack. The test
// harness terminates it once the attach assertions complete.
int main(int argc, char *argv[])
{
    (void)argc; // Unused parameter
    (void)argv; // Unused parameter

    std::cout << "test_attach ready" << std::endl;

    for (;;)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;
}
