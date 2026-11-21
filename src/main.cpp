#include <iostream>
#include <string>

#include "Problem.hpp"

int main(int argc, char *argv[])
{
    if (argc > 2)
    {
        std::cerr << "Usage: " << argv[0] << " [input-file]\n";
        return 1;
    }

    const std::string filename = argc == 2 ? argv[1] : "data/easy.kttsp";
    Problem problem;

    if (!problem.loadFromFile(filename))
    {
        return 1;
    }

    std::cout << "Problem loaded successfully!\n\n";

    std::cout << "Start time: " << problem.startTime << "\n";
    std::cout << "Minimum time of flight: " << problem.minTimeOfFlight << "\n";
    std::cout << "Maximum total time: " << problem.maxTotalTime << "\n";
    std::cout << "Maximum Delta-V: " << problem.maxDeltaV << "\n";
    std::cout << "Exception Delta-V: " << problem.maxExceptionDeltaV << "\n";
    std::cout << "Maximum exceptions: " << problem.maxExceptions << "\n";

    std::cout << "\nNumber of tomatoes: "
              << problem.tomatoes.size()
              << "\n";

    return 0;
}
