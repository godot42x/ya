#include "Render/Core/Buffer.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

namespace ya
{

namespace
{

/// In-memory IBuffer test double implementing the FG-802 data-access contract.
class SpecBuffer final : public IBuffer
{
  private:
    std::string  _name;
    EBufferUsage _usage = EBufferUsage::None;
    uint32_t     _size  = 0;
    bool         _bHostVisible = false;
    bool         _bCoherent    = true;
    bool         _bMapped      = false;
    std::vector<uint8_t> _bytes{};

  public:
    SpecBuffer(const BufferCreateInfo& desc, bool bCoherent)
        : _name(desc.label),
          _usage(desc.usage),
          _size(desc.size),
          _bHostVisible(desc.memoryUsage == EMemoryUsage::CpuToGpu || desc.memoryUsage == EMemoryUsage::GpuToCpu),
          _bCoherent(bCoherent),
          _bytes(desc.size, 0)
    {}

    bool writeData(const void* data, uint32_t size = 0, uint32_t offset = 0) override
    {
        if (!data || !_bHostVisible) {
            return false;
        }
        const uint32_t writeSize = size == 0 ? _size : size;
        if (size == 0 && offset != 0) {
            return false;
        }
        if (static_cast<uint64_t>(offset) + writeSize > _size) {
            return false;
        }
        std::memcpy(_bytes.data() + offset, data, writeSize);
        return true;
    }

    bool flush(uint32_t size = 0, uint32_t offset = 0) override
    {
        if (!_bHostVisible) {
            return false;
        }
        if (_bCoherent) {
            return true;
        }
        if (!_bMapped) {
            return false;
        }
        const uint32_t flushSize = size == 0 ? _size : size;
        if (size == 0 && offset != 0) {
            return false;
        }
        if (static_cast<uint64_t>(offset) + flushSize > _size) {
            return false;
        }
        return true;
    }

    void unmap() override { _bMapped = false; }
    BufferHandle getHandle() const override
    {
        return BufferHandle{reinterpret_cast<void*>(static_cast<uintptr_t>(_size + 1))};
    }
    uint32_t getSize() const override { return _size; }
    EBufferUsage getUsage() const override { return _usage; }
    bool isHostVisible() const override { return _bHostVisible; }
    const std::string& getName() const override { return _name; }

    const std::vector<uint8_t>& bytes() const { return _bytes; }
    bool isMapped() const { return _bMapped; }

  protected:
    void mapInternal(void** ptr) override
    {
        if (!_bHostVisible || _bMapped) {
            *ptr = nullptr;
            return;
        }
        _bMapped = true;
        *ptr     = _bytes.data();
    }
};

SpecBuffer makeHostBuffer(uint32_t size, EMemoryUsage memoryUsage = EMemoryUsage::CpuToGpu, bool bCoherent = true)
{
    return SpecBuffer(BufferCreateInfo{
        .label       = "Test.Buffer",
        .usage       = EBufferUsage::UniformBuffer,
        .size        = size,
        .memoryUsage = memoryUsage,
    }, bCoherent);
}

} // namespace

TEST(BufferDataContractTest, WriteDataRejectsNullAndNonHostVisible)
{
    SpecBuffer nonHost = makeHostBuffer(64, EMemoryUsage::GpuOnly);
    EXPECT_FALSE(nonHost.writeData(nullptr, 64, 0));
    EXPECT_FALSE(nonHost.writeData("data", 4, 0));
    EXPECT_TRUE(nonHost.bytes().size() == 64);

    SpecBuffer host = makeHostBuffer(64);
    EXPECT_FALSE(host.writeData(nullptr, 4, 0));
}

TEST(BufferDataContractTest, WriteDataRejectsOutOfRange)
{
    SpecBuffer buffer = makeHostBuffer(64);
    const char payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    EXPECT_FALSE(buffer.writeData(payload, 8, 60));
    EXPECT_FALSE(buffer.writeData(payload, 0, 1));
    EXPECT_TRUE(buffer.writeData(payload, 8, 56));
    EXPECT_TRUE(buffer.writeData(payload, 0, 0));
}

TEST(BufferDataContractTest, WriteDataWritesRequestedRange)
{
    SpecBuffer buffer = makeHostBuffer(16);
    const uint8_t payload[4] = {0xAA, 0xBB, 0xCC, 0xDD};

    ASSERT_TRUE(buffer.writeData(payload, sizeof(payload), 4));
    EXPECT_EQ(buffer.bytes()[0], 0u);
    EXPECT_EQ(buffer.bytes()[3], 0u);
    EXPECT_EQ(buffer.bytes()[4], 0xAA);
    EXPECT_EQ(buffer.bytes()[7], 0xDD);
    EXPECT_EQ(buffer.bytes()[8], 0u);
}

TEST(BufferDataContractTest, FlushRejectsNonHostVisibleAndUnmapped)
{
    SpecBuffer nonHost = makeHostBuffer(64, EMemoryUsage::GpuOnly);
    EXPECT_FALSE(nonHost.flush());

    SpecBuffer nonCoherent = makeHostBuffer(64, EMemoryUsage::CpuToGpu, /*bCoherent=*/false);
    EXPECT_FALSE(nonCoherent.flush());

    uint8_t* mapped = nonCoherent.map<uint8_t>();
    EXPECT_NE(mapped, nullptr);
    EXPECT_TRUE(nonCoherent.isMapped());
    EXPECT_TRUE(nonCoherent.flush());
    nonCoherent.unmap();
    EXPECT_FALSE(nonCoherent.isMapped());
    EXPECT_FALSE(nonCoherent.flush());
}

TEST(BufferDataContractTest, FlushRejectsOutOfRangeAndCoherentIsNoOp)
{
    SpecBuffer nonCoherent = makeHostBuffer(64, EMemoryUsage::CpuToGpu, /*bCoherent=*/false);
    ASSERT_NE(nonCoherent.map<uint8_t>(), nullptr);
    EXPECT_FALSE(nonCoherent.flush(8, 60));
    EXPECT_FALSE(nonCoherent.flush(0, 1));
    nonCoherent.unmap();

    SpecBuffer coherent = makeHostBuffer(64);
    EXPECT_TRUE(coherent.flush());
    EXPECT_FALSE(coherent.isMapped());
}

TEST(BufferDataContractTest, ReadbackMapReturnsWrittenDataAndUnmapIsIdempotent)
{
    SpecBuffer buffer = makeHostBuffer(32, EMemoryUsage::GpuToCpu, /*bCoherent=*/false);
    const uint32_t value = 0x12345678u;
    ASSERT_TRUE(buffer.writeData(&value, sizeof(value), 0));
    buffer.unmap();

    const auto* mapped = buffer.map<const uint32_t>();
    ASSERT_NE(mapped, nullptr);
    EXPECT_EQ(*mapped, value);
    EXPECT_TRUE(buffer.isMapped());

    buffer.unmap();
    buffer.unmap();
    EXPECT_FALSE(buffer.isMapped());
}

TEST(BufferDataContractTest, MapRejectsNonHostVisible)
{
    SpecBuffer nonHost = makeHostBuffer(64, EMemoryUsage::GpuOnly);
    EXPECT_EQ(nonHost.map<uint8_t>(), nullptr);
    EXPECT_FALSE(nonHost.isMapped());
}

} // namespace ya
