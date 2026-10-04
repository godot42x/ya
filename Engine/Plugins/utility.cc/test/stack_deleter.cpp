#include <cassert>
#include <iostream>

#include "utility.cc/stack_deleter.h"


// 示例类，用于测试析构函数调用
class TestResource
{
  private:
    std::string name_;
    static int  instance_count_;

  public:
    TestResource(const std::string &name) : name_(name)
    {
        instance_count_++;
        std::cout << "创建资源: " << name_ << " (总数: " << instance_count_ << ")" << std::endl;
    }

    ~TestResource()
    {
        instance_count_--;
        std::cout << "销毁资源: " << name_ << " (剩余: " << instance_count_ << ")" << std::endl;
    }

    const std::string &getName() const { return name_; }
    static int         getInstanceCount() { return instance_count_; }
};

int TestResource::instance_count_ = 0;

// 模拟复杂的资源依赖关系
class DatabaseConnection
{
  public:
    DatabaseConnection()
    {
        std::cout << "Database connection established" << std::endl;
    }

    ~DatabaseConnection()
    {
        std::cout << "Database connection closed" << std::endl;
    }
};

class NetworkManager
{
  public:
    NetworkManager()
    {
        std::cout << "Network manager initialized" << std::endl;
    }

    ~NetworkManager()
    {
        std::cout << "Network manager closed" << std::endl;
    }
};

void demonstrateStackDeleter()
{
    std::cout << "\n=== StackDeleter演示开始 ===" << std::endl;

    {
        ut::StackDeleter deleter;

        // Create resources and register to deleter
        auto *db = new DatabaseConnection();
        deleter.push("Database", db, [](void *ptr) {
            delete static_cast<DatabaseConnection*>(ptr);
        });

        auto *network = new NetworkManager();
        deleter.push("Network", network, [](void *ptr) {
            delete static_cast<NetworkManager*>(ptr);
        });

        auto *resource1 = new TestResource("Resource1");
        deleter.push("TestResource1", resource1, [](void *ptr) {
            delete static_cast<TestResource*>(ptr);
        });

        auto *resource2 = new TestResource("Resource2");
        deleter.push("TestResource2", resource2, [](void *ptr) {
            delete static_cast<TestResource*>(ptr);
        });

        // 添加自定义清理逻辑
        deleter.push("Custom Cleanup", [](void *) {
            std::cout << "---> Executing custom cleanup logic" << std::endl;
        });

        std::cout << "\n当前管理的资源数量: " << deleter.size() << std::endl;
        std::cout << "当前TestResource实例数量: " << TestResource::getInstanceCount() << std::endl;

        std::cout << "\n离开作用域，开始自动清理..." << std::endl;
    } // deleter destructs here, cleaning all resources in LIFO order

    std::cout << "\n清理完成，剩余TestResource实例数量: " << TestResource::getInstanceCount() << std::endl;
    std::cout << "=== StackDeleter演示结束 ===\n"
              << std::endl;
}

void demonstrateManualClear()
{
    std::cout << "=== 手动清理演示开始 ===" << std::endl;

    ut::StackDeleter deleter;

    auto *resource1 = new TestResource("手动资源1");
    auto *resource2 = new TestResource("手动资源2");

    deleter.push("手动资源 1", resource1, [](void *ptr) {
        delete static_cast<TestResource*>(ptr);
    });
    deleter.push("手动资源 2", resource2, [](void *ptr) {
        delete static_cast<TestResource*>(ptr);
    });

    std::cout << "添加资源后，实例数量: " << TestResource::getInstanceCount() << std::endl;

    // 手动清理
    std::cout << "执行手动清理..." << std::endl;
    deleter.clear();

    std::cout << "手动清理完成，实例数量: " << TestResource::getInstanceCount() << std::endl;
    std::cout << "=== 手动清理演示结束 ===\n"
              << std::endl;
}

int main()
{
    std::cout << "StackDeleter测试程序\n"
              << std::endl;

    // 演示自动清理
    demonstrateStackDeleter();

    // 演示手动清理
    demonstrateManualClear();

    // 验证所有资源都被正确清理
    assert(TestResource::getInstanceCount() == 0);
    std::cout << "所有测试通过！所有资源都被正确清理。" << std::endl;

    return 0;
}
