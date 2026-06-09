#include <iostream>
#include <stdexcept>

// Debuggee for exception-filter tests. It throws a C++ exception that is
// caught locally: with the "cpp" first-chance filter enabled the debugger
// stops on the throw; without it the program runs to a clean exit. Like the
// other debuggees, the source must stay stable so recorded sessions match.
int main(int argc, char *argv[])
{
    (void)argc; // Unused parameter
    (void)argv; // Unused parameter

    std::cout << "exception-1 starting" << std::endl;

    try
    {
        throw std::runtime_error("expected test exception");
    }
    catch (const std::exception &exception)
    {
        std::cout << "caught: " << exception.what() << std::endl;
    }

    std::cout << "exception-1 done" << std::endl;
    return 0;
}
