#include "framework.h"
#include <cstring>
#include <iostream>


namespace TestFramework
{

// Static member functions to ensure proper initialization
std::map<std::string, TestRegistry::TestFunction> &TestRegistry::GetTests()
{
    static std::map<std::string, TestFunction> tests;
    return tests;
}

std::map<std::string, std::string> &TestRegistry::GetLocations()
{
    static std::map<std::string, std::string> locations;
    return locations;
}

void TestRegistry::RegisterTest(const std::string &name, TestFunction testFunc, const std::string &file, int line)
{
    GetTests()[name]     = testFunc;
    GetLocations()[name] = file + ":" + std::to_string(line);
    std::cout << "Registered test: " << name << std::endl;
}

bool TestRegistry::RunTest(const std::string &name)
{
    auto &tests = GetTests();
    auto  it    = tests.find(name);
    if (it != tests.end()) {
        try {
            std::cout << "Running test: " << name << "... ";
            bool result = it->second();
            std::cout << (result ? "PASSED" : "FAILED") << std::endl;
            return result;
        }
        catch (const std::exception &e) {
            std::cout << "EXCEPTION: " << e.what() << std::endl;
            return false;
        }
    }
    std::cout << "Test not found: " << name << std::endl;
    return false;
}

bool TestRegistry::RunAllTests()
{
    auto &tests     = GetTests();
    bool  allPassed = true;
    int   passed    = 0;
    int   total     = tests.size();

    std::cout << "\n=== Running " << total << " tests ===\n"
              << std::endl;

    for (const auto &test : tests) {
        bool result = RunTest(test.first);
        if (result) {
            passed++;
        }
        else {
            allPassed = false;
        }
    }

    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Passed: " << passed << "/" << total << std::endl;
    std::cout << "Result: " << (allPassed ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << std::endl;

    return allPassed;
}

std::vector<std::string> TestRegistry::GetTestNames()
{
    auto                    &tests = GetTests();
    std::vector<std::string> names;
    for (const auto &test : tests) {
        names.push_back(test.first);
    }
    return names;
}

std::string TestRegistry::GetTestLocation(const std::string &name)
{
    auto &locations = GetLocations();
    auto  it        = locations.find(name);
    return (it != locations.end()) ? it->second : "";
}
bool TestRegistry::HasTest(const std::string &name)
{
    auto &tests = GetTests();
    return tests.find(name) != tests.end();
}

int TestRegistry::GetTestCount()
{
    return static_cast<int>(GetTests().size());
}
} // namespace TestFramework