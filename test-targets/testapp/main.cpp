#include <iostream>

int main(int argc, char *argv[])
{
    (void)argc; // Unused parameter
    (void)argv; // Unused parameter

    std::cout << "Hello, World!" << std::endl;

    int a = 5;
    int b = 10;
    int sum = a + b;
    std::cout << "The sum of " << a << " and " << b << " is " << sum << "." << std::endl;

    return 0;
}
