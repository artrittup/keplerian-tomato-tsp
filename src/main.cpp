#include <iostream>
#include <string>

#include "Problem.hpp"

#ifdef KTTSP_HAS_BEAM_SEARCH
int runBeamSearchExperiment(int argc, char *argv[]);
int runGraphSearchExperiment(int argc, char *argv[]);
#endif

int main(int argc, char *argv[])
{
    if (argc > 1 && std::string(argv[1]) == "--beam-search")
    {
#ifdef KTTSP_HAS_BEAM_SEARCH
        return runBeamSearchExperiment(argc - 2, argv + 2);
#else
        std::cerr << "Beam search requires a build with KTTSP_ENABLE_TRANSFER_SOLVER=ON.\n";
        return 1;
#endif
    }

    if (argc > 1 && std::string(argv[1]) == "--graph-search")
    {
#ifdef KTTSP_HAS_BEAM_SEARCH
        return runGraphSearchExperiment(argc - 2, argv + 2);
#else
        std::cerr << "Graph search requires a build with KTTSP_ENABLE_TRANSFER_SOLVER=ON.\n";
        return 1;
#endif
    }

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
