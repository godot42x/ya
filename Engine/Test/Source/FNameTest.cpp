#include "Core/FName.h"
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>


// 测试基本功能
class FNameTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // 每个测试前的设置
    }

    void TearDown() override
    {
        // 每个测试后的清理
    }
};

// 测试 1: 基本构造和相等性
TEST_F(FNameTest, BasicConstruction)
{
    FName name1("test");
    FName name2("test");
    FName name3("other");

    // 相同字符串应该有相同的 index
    EXPECT_EQ(name1._index, name2._index);
    EXPECT_NE(name1._index, name3._index);

    // 字符串内容应该相同
    EXPECT_STREQ(name1.c_str(), "test");
    EXPECT_STREQ(name2.c_str(), "test");
}

// 测试 2: 拷贝构造
TEST_F(FNameTest, CopyConstruction)
{
    FName name1("test");
    FName name2(name1);

    EXPECT_EQ(name1._index, name2._index);
    EXPECT_STREQ(name1.c_str(), name2.c_str());
}

// 测试 3: 移动构造
TEST_F(FNameTest, MoveConstruction)
{
    FName   name1("test");
    index_t originalIndex = name1._index;

    FName name2(std::move(name1));

    EXPECT_EQ(name2._index, originalIndex);
    EXPECT_EQ(name1._index, 0); // 移动后源对象应该被重置
}

// 测试 4: 拷贝赋值
TEST_F(FNameTest, CopyAssignment)
{
    FName name1("test");
    FName name2("other");

    name2 = name1;

    EXPECT_EQ(name1._index, name2._index);
    EXPECT_STREQ(name1._data.data(), name2._data.data());
}

// 测试 5: 移动赋值
TEST_F(FNameTest, MoveAssignment)
{
    FName   name1("test");
    FName   name2("other");
    index_t originalIndex = name1._index;

    name2 = std::move(name1);

    EXPECT_EQ(name2._index, originalIndex);
    EXPECT_EQ(name1._index, 0);
}

// 测试 6: 生命周期后 index 一致性
TEST_F(FNameTest, IndexConsistencyAfterDestruction)
{
    index_t index1;

    // 创建并销毁第一个 FName
    {
        FName name1("test");
        index1 = name1._index;
    }

    // 重新创建相同字符串的 FName
    FName name2("test");

    // 应该得到相同的 index（string interning）
    EXPECT_EQ(name2._index, index1);
}

// 测试 7: hash 一致性
TEST_F(FNameTest, HashConsistency)
{
    FName name1("test");
    FName name2("test");
    FName name3("other");

    std::hash<FName> hasher;

    EXPECT_EQ(hasher(name1), hasher(name2));
    EXPECT_NE(hasher(name1), hasher(name3));
}

// 测试 8: 在 unordered_map 中使用
TEST_F(FNameTest, UnorderedMapUsage)
{
    std::unordered_map<FName, int> map;

    FName key1("test");
    map[key1] = 42;

    FName key2("test");       // 相同字符串
    EXPECT_EQ(map[key2], 42); // 应该能找到

    FName key3("other");
    map[key3] = 100;

    EXPECT_EQ(map.size(), 2);
    EXPECT_EQ(map[key1], 42);
    EXPECT_EQ(map[key3], 100);
}

// 测试 9: 临时对象在 unordered_map 中的使用
TEST_F(FNameTest, TemporaryObjectInMap)
{
    std::unordered_map<FName, int> map;

    // 使用临时对象作为 key
    map[FName("temp")] = 999;

    // 用新的临时对象查找
    auto it = map.find(FName("temp"));
    ASSERT_NE(it, map.end());
    EXPECT_EQ(it->second, 999);
}

// 测试 10: 多线程安全性 - 并发创建相同 FName
TEST_F(FNameTest, MultithreadSameString)
{
    const int                threadCount = 10;
    std::vector<std::thread> threads;
    std::vector<index_t>     indices(threadCount);

    for (int i = 0; i < threadCount; ++i)
    {
        threads.emplace_back([&indices, i]() {
            FName name("concurrent_test");
            indices[i] = name._index;
        });
    }

    for (auto &t : threads)
    {
        t.join();
    }

    // 所有线程应该得到相同的 index
    for (int i = 1; i < threadCount; ++i)
    {
        EXPECT_EQ(indices[0], indices[i]);
    }
}

// 测试 11: 多线程安全性 - 并发创建不同 FName
TEST_F(FNameTest, MultithreadDifferentStrings)
{
    const int                threadCount = 10;
    std::vector<std::thread> threads;
    std::vector<index_t>     indices(threadCount);

    for (int i = 0; i < threadCount; ++i)
    {
        threads.emplace_back([&indices, i]() {
            std::string str = "thread_" + std::to_string(i);
            FName       name(str);
            indices[i] = name._index;
        });
    }

    for (auto &t : threads)
    {
        t.join();
    }

    // 所有线程应该得到不同的 index
    std::set<index_t> uniqueIndices(indices.begin(), indices.end());
    EXPECT_EQ(uniqueIndices.size(), threadCount);
}

// 测试 12: 多线程 - 同时创建和销毁
TEST_F(FNameTest, MultithreadCreateAndDestroy)
{
    const int                threadCount         = 10;
    const int                iterationsPerThread = 100;
    std::vector<std::thread> threads;
    std::atomic<int>         errorCount{0};

    for (int i = 0; i < threadCount; ++i)
    {
        threads.emplace_back([&errorCount]() {
            for (int j = 0; j < iterationsPerThread; ++j)
            {
                FName name1("test");
                FName name2("test");

                if (name1._index != name2._index)
                {
                    errorCount++;
                }

                // 短暂延迟增加竞争
                std::this_thread::yield();
            }
        });
    }

    for (auto &t : threads)
    {
        t.join();
    }

    EXPECT_EQ(errorCount.load(), 0);
}

// 测试 13: 多线程 - unordered_map insert 竞态
TEST_F(FNameTest, MultithreadMapInsert)
{
    std::unordered_map<FName, int> map;
    std::mutex                     mapMutex;

    const int                threadCount = 10;
    std::vector<std::thread> threads;

    // 先创建一些 FName，让它们在 registry 中
    {
        FName temp1("shared1");
        FName temp2("shared2");
        FName temp3("shared3");
    }

    for (int i = 0; i < threadCount; ++i)
    {
        threads.emplace_back([&map, &mapMutex, i]() {
            for (int j = 0; j < 50; ++j)
            {
                FName key("shared" + std::to_string(j % 3 + 1));

                std::lock_guard<std::mutex> lock(mapMutex);
                map[key] = i * 100 + j;
            }
        });
    }

    for (auto &t : threads)
    {
        t.join();
    }

    // 验证 map 仍然可以正确查找
    FName key1("shared1");
    FName key2("shared2");
    FName key3("shared3");

    EXPECT_NE(map.find(key1), map.end());
    EXPECT_NE(map.find(key2), map.end());
    EXPECT_NE(map.find(key3), map.end());
}

// 测试 14: 引用计数正确性
TEST_F(FNameTest, ReferenceCountingCorrectness)
{
    // 这个测试验证引用计数不会导致过早删除
    std::vector<FName> names;

    for (int i = 0; i < 100; ++i)
    {
        names.emplace_back("test_ref_count");
    }

    index_t commonIndex = names[0]._index;

    // 所有 FName 应该有相同的 index
    for (const auto &name : names)
    {
        EXPECT_EQ(name._index, commonIndex);
    }

    // 清空一半
    names.erase(names.begin(), names.begin() + 50);

    // 创建新的 FName，应该仍然得到相同的 index
    FName newName("test_ref_count");
    EXPECT_EQ(newName._index, commonIndex);
}

// 测试 15: 空字符串处理
TEST_F(FNameTest, EmptyString)
{
    FName empty1("");
    FName empty2("");

    EXPECT_EQ(empty1._index, empty2._index);
}

// 测试 16: 特殊字符
TEST_F(FNameTest, SpecialCharacters)
{
    FName name1("test@#$%");
    FName name2("test@#$%");
    FName name3("test_123");

    EXPECT_EQ(name1._index, name2._index);
    EXPECT_NE(name1._index, name3._index);
}

// 测试 17: 长字符串
TEST_F(FNameTest, LongString)
{
    std::string longStr(1000, 'a');
    FName       name1(longStr);
    FName       name2(longStr);

    EXPECT_EQ(name1._index, name2._index);
}

// ============================================================================
// 性能测试
// ============================================================================

class FNamePerformanceTest : public ::testing::Test
{
  protected:
    void SetUp() override {}
    void TearDown() override {}

    // 辅助函数：测量执行时间
    template <typename Func>
    double measureTime(Func &&func)
    {
        auto start = std::chrono::high_resolution_clock::now();
        func();
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count();
    }
};

// 性能测试 1: 单线程创建已存在的 FName
TEST_F(FNamePerformanceTest, SingleThreadExistingNames)
{
    // 预先创建一些 FName
    std::vector<std::string> names = {"Player", "Enemy", "Bullet", "Item", "NPC"};
    for (const auto &name : names)
    {
        FName temp(name);
    }

    // 测试：重复创建已存在的 FName
    const int iterations = 100000;
    auto      duration   = measureTime([&]() {
        for (int i = 0; i < iterations; ++i)
        {
            for (const auto &name : names)
            {
                FName temp(name);
            }
        }
    });

    std::cout << "Single-thread existing names: " << duration << " ms for " << (iterations * names.size())
              << " operations\n";
    std::cout << "Average: " << (duration / (iterations * names.size()) * 1000000) << " ns per operation\n";

    // 性能预期：应该很快（共享锁）
    EXPECT_LT(duration, 5000); // 小于 5 秒
}

// 性能测试 2: 单线程创建新的 FName
TEST_F(FNamePerformanceTest, SingleThreadNewNames)
{
    const int iterations = 10000;
    auto      duration   = measureTime([&]() {
        for (int i = 0; i < iterations; ++i)
        {
            FName temp("unique_name_" + std::to_string(i));
        }
    });

    std::cout << "Single-thread new names: " << duration << " ms for " << iterations << " operations\n";
    std::cout << "Average: " << (duration / iterations * 1000000) << " ns per operation\n";

    // 创建新 FName 会慢一些（需要独占锁）
    EXPECT_LT(duration, 10000); // 小于 10 秒
}

// 性能测试 3: 多线程读取已存在的 FName（高并发场景）
TEST_F(FNamePerformanceTest, MultithreadExistingNamesHighConcurrency)
{
    // 预先创建
    std::vector<std::string> names = {"Player", "Enemy", "Bullet", "Item", "NPC", "Boss", "Weapon", "Armor"};
    for (const auto &name : names)
    {
        FName temp(name);
    }

    const int threadCount        = 16;
    const int iterationsPerThread = 50000;

    auto duration = measureTime([&]() {
        std::vector<std::thread> threads;
        for (int t = 0; t < threadCount; ++t)
        {
            threads.emplace_back([&names, iterationsPerThread]() {
                for (int i = 0; i < iterationsPerThread; ++i)
                {
                    for (const auto &name : names)
                    {
                        FName temp(name);
                    }
                }
            });
        }

        for (auto &thread : threads)
        {
            thread.join();
        }
    });

    int totalOps = threadCount * iterationsPerThread * names.size();
    std::cout << "Multi-thread existing names (" << threadCount << " threads): " << duration << " ms for "
              << totalOps << " operations\n";
    std::cout << "Average: " << (duration / totalOps * 1000000) << " ns per operation\n";
    std::cout << "Throughput: " << (totalOps / duration * 1000) << " ops/sec\n";

    // shared_mutex 应该提供良好的并发性能
    EXPECT_LT(duration, 10000); // 小于 10 秒
}

// 性能测试 4: 多线程混合读写（90% 读，10% 写）
TEST_F(FNamePerformanceTest, MultithreadMixedReadWrite)
{
    // 预先创建一些常用名称
    std::vector<std::string> commonNames = {"Common1", "Common2", "Common3", "Common4", "Common5"};
    for (const auto &name : commonNames)
    {
        FName temp(name);
    }

    const int threadCount        = 8;
    const int iterationsPerThread = 10000;

    auto duration = measureTime([&]() {
        std::vector<std::thread> threads;
        for (int t = 0; t < threadCount; ++t)
        {
            threads.emplace_back([&commonNames, iterationsPerThread, t]() {
                for (int i = 0; i < iterationsPerThread; ++i)
                {
                    if (i % 10 == 0)
                    {
                        // 10% 写操作：创建新名称
                        FName temp("unique_" + std::to_string(t) + "_" + std::to_string(i));
                    }
                    else
                    {
                        // 90% 读操作：访问已存在的名称
                        FName temp(commonNames[i % commonNames.size()]);
                    }
                }
            });
        }

        for (auto &thread : threads)
        {
            thread.join();
        }
    });

    int totalOps = threadCount * iterationsPerThread;
    std::cout << "Multi-thread mixed (90% read, 10% write, " << threadCount << " threads): " << duration
              << " ms for " << totalOps << " operations\n";
    std::cout << "Average: " << (duration / totalOps * 1000000) << " ns per operation\n";

    EXPECT_LT(duration, 10000); // 小于 10 秒
}

// 性能测试 5: FName 拷贝性能
TEST_F(FNamePerformanceTest, CopyPerformance)
{
    FName source("test_name");

    const int iterations = 1000000;
    auto      duration   = measureTime([&]() {
        for (int i = 0; i < iterations; ++i)
        {
            FName copy(source);
        }
    });

    std::cout << "FName copy: " << duration << " ms for " << iterations << " operations\n";
    std::cout << "Average: " << (duration / iterations * 1000000) << " ns per operation\n";

    // 拷贝应该很快（只需要增加引用计数）
    EXPECT_LT(duration, 1000); // 小于 1 秒
}

// 性能测试 6: unordered_map 查找性能
TEST_F(FNamePerformanceTest, UnorderedMapLookup)
{
    std::unordered_map<FName, int> map;

    // 填充 map
    const int mapSize = 1000;
    for (int i = 0; i < mapSize; ++i)
    {
        map[FName("key_" + std::to_string(i))] = i;
    }

    // 测试查找性能
    const int iterations = 100000;
    auto      duration   = measureTime([&]() {
        for (int i = 0; i < iterations; ++i)
        {
            int idx    = i % mapSize;
            auto it    = map.find(FName("key_" + std::to_string(idx)));
            bool found = (it != map.end());
            (void)found; // 避免优化掉
        }
    });

    std::cout << "unordered_map lookup: " << duration << " ms for " << iterations << " operations\n";
    std::cout << "Average: " << (duration / iterations * 1000000) << " ns per operation\n";

    EXPECT_LT(duration, 5000); // 小于 5 秒
}

// 性能测试 7: 压力测试 - 极限并发
TEST_F(FNamePerformanceTest, StressTestExtremeConcurrency)
{
    // 预创建常用名称
    std::vector<std::string> names;
    for (int i = 0; i < 20; ++i)
    {
        names.push_back("common_" + std::to_string(i));
        FName temp(names.back());
    }

    const int threadCount        = 32; // 高并发
    const int iterationsPerThread = 10000;

    auto duration = measureTime([&]() {
        std::vector<std::thread> threads;
        for (int t = 0; t < threadCount; ++t)
        {
            threads.emplace_back([&names, iterationsPerThread, t]() {
                for (int i = 0; i < iterationsPerThread; ++i)
                {
                    // 混合操作
                    if (i % 20 == 0)
                    {
                        FName temp("stress_" + std::to_string(t) + "_" + std::to_string(i));
                    }
                    else
                    {
                        FName temp(names[i % names.size()]);
                    }
                }
            });
        }

        for (auto &thread : threads)
        {
            thread.join();
        }
    });

    int totalOps = threadCount * iterationsPerThread;
    std::cout << "Stress test (" << threadCount << " threads): " << duration << " ms for " << totalOps
              << " operations\n";
    std::cout << "Average: " << (duration / totalOps * 1000000) << " ns per operation\n";
    std::cout << "Throughput: " << (totalOps / duration * 1000) << " ops/sec\n";

    // 压力测试，允许更长时间
    EXPECT_LT(duration, 20000); // 小于 20 秒
}


