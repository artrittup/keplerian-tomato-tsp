#include "OrbitalDiagnostics.hpp"

#include "TransferKey.hpp"

#include <limits>
#include <stdexcept>

namespace
{
template <class UInt>
void littleEndian(unsigned char *bytes, UInt value)
{
    for (std::size_t i = 0; i < sizeof(UInt); ++i)
    {
        bytes[i] = static_cast<unsigned char>(value >> (8 * i));
    }
}
}

thread_local OrbitalDiagnostics *OrbitalDiagnostics::active = nullptr;

OrbitalDiagnostics::OrbitalDiagnostics(bool profile, const std::filesystem::path &trace)
    : profile_(profile), previous_(active)
{
    if (!trace.empty())
    {
        if (std::filesystem::exists(std::filesystem::symlink_status(trace)))
        {
            throw std::runtime_error("Trace already exists. Use a fresh path");
        }
        trace_.exceptions(std::ios::failbit | std::ios::badbit);
        trace_.open(trace, std::ios::binary);
        trace_.write("KTTSPQ1\n", 8);
    }
    if (profile_ || trace_.is_open()) { active = this; }
}

OrbitalDiagnostics::~OrbitalDiagnostics()
{
    active = previous_;
}

void OrbitalDiagnostics::query(std::size_t from, std::size_t to, double departure, double duration)
{
    ++stages_[static_cast<std::size_t>(stage_)].calls;
    if (!trace_.is_open()) { return; }
    if (from > std::numeric_limits<std::uint32_t>::max() || to > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::overflow_error("Orbital trace index exceeds uint32 range");
    }
    unsigned char record[24];
    littleEndian(record, static_cast<std::uint32_t>(from));
    littleEndian(record + 4, static_cast<std::uint32_t>(to));
    littleEndian(record + 8, transfer_detail::timeBits(departure));
    littleEndian(record + 16, transfer_detail::timeBits(duration));
    trace_.write(reinterpret_cast<const char *>(record), sizeof(record));
}

OrbitalDiagnostics::Clock::duration *OrbitalDiagnostics::timing(OrbitalMetric metric)
{
    return profile_ ? &stages_[static_cast<std::size_t>(stage_)].times[static_cast<std::size_t>(metric)] : nullptr;
}

void OrbitalDiagnostics::finish(std::ostream &report)
{
    if (trace_.is_open())
    {
        trace_.close();
        report << "trace.records=" << stages_[0].calls + stages_[1].calls + stages_[2].calls << '\n';
    }
    if (!profile_) { return; }
    constexpr const char *labels[] = {"matrix", "schedule", "final_validation"};
    constexpr const char *metrics[] = {"solver", "planet_setup", "propagation", "lambert", "cache"};
    for (std::size_t i = 0; i < stages_.size(); ++i)
    {
        report << "profile." << labels[i] << ".calls=" << stages_[i].calls << '\n';
        for (std::size_t j = 0; j < stages_[i].times.size(); ++j)
        {
            report << "profile." << labels[i] << '.' << metrics[j] << "_seconds="
                   << std::chrono::duration<double>(stages_[i].times[j]).count() << '\n';
        }
    }
}
