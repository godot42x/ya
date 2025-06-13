#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>
#include <iostream>

namespace TestFramework {
    
    class TestRegistry {
    public:
        using TestFunction = std::function<bool()>;
        
        static void RegisterTest(const std::string& name, TestFunction testFunc, const std::string& file, int line);
        static bool RunTest(const std::string& name);
        static bool RunAllTests();
        static std::vector<std::string> GetTestNames();
        static std::string GetTestLocation(const std::string& name);
        static bool HasTest(const std::string& name);
        static int GetTestCount();
        
    private:
        static std::map<std::string, TestFunction>& GetTests();
        static std::map<std::string, std::string>& GetLocations();
    };
    
    // Test case helper structure
    template<typename TestClass>
    struct TestCaseRegistrar {
        TestCaseRegistrar(const std::string& name, const std::string& file, int line) {
            TestRegistry::RegisterTest(name, []() -> bool {
                return TestClass::Run();
            }, file, line);
        }
    };
}

// Test macros
#define TEST_CASE(name) \
    class TestCase_##name { \
    public: \
        static bool Run(); \
    }; \
    static TestFramework::TestCaseRegistrar<TestCase_##name> \
        registrar_##name(#name, __FILE__, __LINE__); \
    bool TestCase_##name::Run()

#define TEST_ASSERT(condition) \
    do { \
        if (!(condition)) { \
            std::cout << "ASSERTION FAILED: " #condition " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while(0)

#define TEST_ASSERT_EQ(expected, actual) \
    do { \
        if ((expected) != (actual)) { \
            std::cout << "ASSERTION FAILED: expected " << (expected) << " but got " << (actual) \
                      << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while(0)
