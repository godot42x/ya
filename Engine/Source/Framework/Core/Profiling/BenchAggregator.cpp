#include "Core/Profiling/BenchAggregator.h"

#include "Core/Log.h"
#include "Core/Profiling/BenchStats.h"

#include <algorithm>
#include <utility>

namespace ya::profiling
{

namespace
{

nlohmann::json statsToJson(const FBenchWindowStats& stats)
{
    return {
        {"count", stats.count},
        {"mean", stats.mean},
        {"min", stats.min},
        {"p50", stats.p50},
        {"p95", stats.p95},
        {"p99", stats.p99},
        {"max", stats.max},
    };
}

} // namespace

BenchSeriesWriter::BenchSeriesWriter(BenchAggregator& aggregator, std::string section, size_t reserveCapacity)
    : _aggregator(&aggregator), _section(std::move(section)), _reserveCapacity(reserveCapacity)
{
}

void BenchSeriesWriter::record(std::string_view key, float value)
{
    _aggregator->appendSample(_section, key, value, _reserveCapacity);
}

void BenchAggregator::appendSample(std::string_view section, std::string_view key, float value, size_t reserveCapacity)
{
    std::lock_guard lock(_mutex);
    auto            sectionIt = _sections.find(section);
    if (sectionIt == _sections.end()) {
        sectionIt = _sections.emplace(std::string(section), BenchSeriesMap{}).first;
    }
    BenchSeriesMap& series = sectionIt->second;
    auto            it     = series.find(key);
    if (it == series.end()) {
        it = series.emplace(std::string(key), std::vector<float>{}).first;
        it->second.reserve(reserveCapacity);
    }
    it->second.push_back(value);
}

BenchAggregator::SourceId BenchAggregator::addSource(std::string section, IBenchSource& source)
{
    std::lock_guard lock(_mutex);
    const SourceId id = _nextId++;
    _sources.push_back(FSourceSlot{.id = id, .section = std::move(section), .source = &source});
    return id;
}

bool BenchAggregator::removeSource(SourceId id)
{
    std::lock_guard lock(_mutex);
    const auto erased = std::erase_if(_sources, [id](const FSourceSlot& slot) { return slot.id == id; });
    return erased > 0;
}

size_t BenchAggregator::getSourceCount() const
{
    std::lock_guard lock(_mutex);
    return _sources.size();
}

void BenchAggregator::beginWindow(size_t expectedFrames)
{
    std::vector<IBenchSource*> sources;
    {
        std::lock_guard lock(_mutex);
        _sections.clear();
        _expectedFrames = expectedFrames;
        _sampledFrames  = 0;
        _bWindowOpen    = true;
        sources.reserve(_sources.size());
        for (const FSourceSlot& slot : _sources) {
            sources.push_back(slot.source);
        }
    }
    for (IBenchSource* source : sources) {
        source->onWindowBegin();
    }
}

void BenchAggregator::sampleFrame()
{
    std::vector<FSourceSlot> sources;
    size_t                   expectedFrames = 0;
    {
        std::lock_guard lock(_mutex);
        if (!_bWindowOpen) {
            return;
        }
        sources        = _sources;
        expectedFrames = _expectedFrames;
        ++_sampledFrames;
    }
    for (const FSourceSlot& slot : sources) {
        BenchSeriesWriter writer(*this, slot.section, expectedFrames);
        slot.source->sampleFrame(writer);
    }
}

void BenchAggregator::endWindow()
{
    std::lock_guard lock(_mutex);
    _bWindowOpen = false;
}

bool BenchAggregator::isWindowOpen() const
{
    std::lock_guard lock(_mutex);
    return _bWindowOpen;
}

uint64_t BenchAggregator::getSampledFrameCount() const
{
    std::lock_guard lock(_mutex);
    return _sampledFrames;
}

nlohmann::json BenchAggregator::buildSections() const
{
    nlohmann::json           sections = nlohmann::json::object();
    std::vector<FSourceSlot> sources;
    {
        std::lock_guard lock(_mutex);
        for (const auto& [sectionName, series] : _sections) {
            nlohmann::json& section = sections[sectionName];
            section                 = nlohmann::json::object();
            for (const auto& [key, samples] : series) {
                section[key] = statsToJson(computeBenchWindowStats(samples));
            }
        }
        sources = _sources;
    }
    for (const FSourceSlot& slot : sources) {
        nlohmann::json& section = sections[slot.section];
        if (section.is_null()) {
            section = nlohmann::json::object();
        }
        nlohmann::json facts = nlohmann::json::object();
        slot.source->writeFacts(facts);
        for (auto& [key, value] : facts.items()) {
            if (section.contains(key)) {
                YA_CORE_WARN("Benchmark section '{}': fact '{}' collides with a recorded series; keeping the series",
                             slot.section,
                             key);
                continue;
            }
            section[key] = value;
        }
    }
    return sections;
}

BenchSourceRegistration::BenchSourceRegistration(BenchAggregator& aggregator, std::string section, IBenchSource& source)
    : _aggregator(&aggregator), _id(aggregator.addSource(std::move(section), source))
{
}

BenchSourceRegistration::~BenchSourceRegistration()
{
    reset();
}

BenchSourceRegistration::BenchSourceRegistration(BenchSourceRegistration&& other) noexcept
    : _aggregator(std::exchange(other._aggregator, nullptr)), _id(std::exchange(other._id, 0))
{
}

BenchSourceRegistration& BenchSourceRegistration::operator=(BenchSourceRegistration&& other) noexcept
{
    if (this != &other) {
        reset();
        _aggregator = std::exchange(other._aggregator, nullptr);
        _id         = std::exchange(other._id, 0);
    }
    return *this;
}

void BenchSourceRegistration::reset()
{
    if (_aggregator) {
        _aggregator->removeSource(_id);
        _aggregator = nullptr;
        _id         = 0;
    }
}

BenchAggregator& benchAggregator()
{
    static BenchAggregator aggregator;
    return aggregator;
}

BenchSourceRegistration registerBenchSource(std::string section, IBenchSource& source)
{
    return BenchSourceRegistration(benchAggregator(), std::move(section), source);
}

} // namespace ya::profiling
