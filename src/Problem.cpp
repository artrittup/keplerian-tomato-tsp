#include "Problem.hpp"

#include <cmath>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <locale>
#include <sstream>
#include <utility>

namespace
{
template <typename Number>
bool parseNumber(const std::string &token, Number &value)
{
    std::istringstream number(token);
    number.imbue(std::locale::classic());
    return (number >> value) && number.eof() && std::isfinite(value);
}

template <typename... Numbers>
bool readNumbers(std::istream &row, Numbers &...values)
{
    const auto readNumber = [&row](auto &value)
    {
        std::string token;
        return (row >> token) && parseNumber(token, value);
    };

    std::string extra;
    return (readNumber(values) && ...) && !(row >> extra);
}
}

bool Problem::loadFromFile(const std::string &filename)
{
    std::ifstream file(filename, std::ios::binary);

    if (!file.is_open())
    {
        std::cerr << filename << ": could not open file\n";
        return false;
    }

    Problem parsed;
    bool hasHeader = false;
    std::size_t lineNumber = 0;
    std::string line;

    const auto lineError = [&](const char *message)
    {
        std::cerr << filename << ':' << lineNumber << ": " << message << '\n';
        return false;
    };

    while (std::getline(file, line))
    {
        ++lineNumber;
        std::istringstream row(line);
        std::string firstToken;

        if (!(row >> firstToken) || firstToken == "c")
        {
            continue;
        }

        if (firstToken == "p")
        {
            if (hasHeader)
            {
                return lineError("duplicate parameter header");
            }

            std::string type;
            if (!(row >> type) || type != "kttsp" ||
                !readNumbers(row, parsed.startTime, parsed.minTimeOfFlight,
                             parsed.maxTotalTime, parsed.maxDeltaV,
                             parsed.maxExceptionDeltaV, parsed.maxExceptions))
            {
                return lineError("malformed parameter header; expected 'p kttsp' followed by five numbers and an integer");
            }

            hasHeader = true;
            continue;
        }

        if (!hasHeader)
        {
            return lineError("missing 'p kttsp' parameter header before orbit data");
        }

        Tomato tomato{};
        if (!parseNumber(firstToken, tomato.a) ||
            !readNumbers(row, tomato.e, tomato.i, tomato.Omega, tomato.omega, tomato.nu))
        {
            return lineError("malformed orbit row; expected six numbers");
        }
        parsed.tomatoes.push_back(tomato);
    }

    if (file.bad() || !file.eof())
    {
        std::cerr << filename << ": could not read file\n";
        return false;
    }

    if (!hasHeader)
    {
        std::cerr << filename << ": missing 'p kttsp' parameter header (empty or comment-only instance)\n";
        return false;
    }

    if (parsed.tomatoes.empty())
    {
        std::cerr << filename << ": instance contains no orbit rows\n";
        return false;
    }

    // A successful load replaces the previous instance all at once.
    *this = std::move(parsed);
    return true;
}
