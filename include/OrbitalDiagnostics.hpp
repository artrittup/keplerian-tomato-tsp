#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>

enum class OrbitalStage { Matrix, Schedule, FinalValidation, Count };
enum class OrbitalMetric { Solver, PlanetSetup, Propagation, Lambert, Cache, Count };

class OrbitalDiagnostics
{
public:
    using Clock = std::chrono::steady_clock;

    OrbitalDiagnostics(bool profile, const std::filesystem::path &trace);
    ~OrbitalDiagnostics();
    OrbitalDiagnostics(const OrbitalDiagnostics &) = delete;
    OrbitalDiagnostics &operator=(const OrbitalDiagnostics &) = delete;

    static thread_local OrbitalDiagnostics *active;

    void stage(OrbitalStage value) { stage_ = value; }
    void query(std::size_t from, std::size_t to, double departure, double duration);
    Clock::duration *timing(OrbitalMetric metric);
    void finish(std::ostream &report);

private:
    struct Stage
    {
        std::uint64_t calls = 0;
        std::array<Clock::duration, static_cast<std::size_t>(OrbitalMetric::Count)> times{};
    };

    const bool profile_;
    OrbitalDiagnostics *const previous_;
    OrbitalStage stage_ = OrbitalStage::Matrix;
    std::array<Stage, static_cast<std::size_t>(OrbitalStage::Count)> stages_{};
    std::ofstream trace_;
};

class OrbitalTimer
{
public:
    OrbitalTimer(OrbitalDiagnostics *diagnostics, OrbitalMetric metric)
        : total_(diagnostics ? diagnostics->timing(metric) : nullptr)
    {
        resume();
    }
    ~OrbitalTimer() { pause(); }
    OrbitalTimer(const OrbitalTimer &) = delete;
    OrbitalTimer &operator=(const OrbitalTimer &) = delete;

    void pause()
    {
        if (running_)
        {
            *total_ += OrbitalDiagnostics::Clock::now() - start_;
            running_ = false;
        }
    }

    void resume()
    {
        if (total_)
        {
            start_ = OrbitalDiagnostics::Clock::now();
            running_ = true;
        }
    }

private:
    OrbitalDiagnostics::Clock::duration *const total_;
    OrbitalDiagnostics::Clock::time_point start_{};
    bool running_ = false;
};
