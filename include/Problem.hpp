#pragma once

#include <string>
#include <vector>

struct Tomato
{
    double a;
    double e;
    double i;
    double Omega;
    double omega;
    double nu;
};

class Problem
{
public:
    bool loadFromFile(const std::string &filename);

    double startTime = 0.0;
    double minTimeOfFlight = 0.0;
    double maxTotalTime = 0.0;
    double maxDeltaV = 0.0;
    double maxExceptionDeltaV = 0.0;
    int maxExceptions = 0;

    std::vector<Tomato> tomatoes;
};
