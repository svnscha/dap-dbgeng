#include <iostream>

// Debuggee for data-breakpoint tests. `watched` is written on a known line
// after the breakpoint line, so a write watchpoint armed at the breakpoint
// fires deterministically. Built unoptimized so `watched` stays in memory.
// The source must stay stable so recorded sessions match.
int main(int argc, char *argv[])
{
    (void)argc; // Unused parameter
    (void)argv; // Unused parameter

    int watched = 1;
    int next = watched + 1;

    std::cout << "data-1 armed" << std::endl; // breakpoint line: arm the watchpoint here

    watched = next * 2; // the watched write: a data breakpoint stops here

    std::cout << "watched is " << watched << std::endl;
    return 0;
}
