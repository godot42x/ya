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

std::map<std::string, TestResult> &TestRegistry::GetResults()
{
    static std::map<std::string, TestResult> results;
    return results;
}

void TestRegistry::RegisterTest(const std::string &name, TestFunction testFunc, const std::string &file, int line)
{
    GetTests()[name]     = testFunc;
    GetLocations()[name] = file + ":" + std::to_string(line);
    std::cout << "Registered test: " << name << std::endl;
}

TestResult TestRegistry::RunTestWithResult(const std::string &name)
{
    TestResult result;
    result.name = name;

    auto &tests = GetTests();
    auto  it    = tests.find(name);
    if (it == tests.end()) {
        result.passed   = false;
        result.errorMsg = "Test not found: " + name;
        return result;
    }

    try {
        auto startTime = std::chrono::high_resolution_clock::now();
        result.passed  = it->second();
        auto endTime   = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double, std::milli> elapsed = endTime - startTime;
        result.elapsedMs                                   = elapsed.count();
    }
    catch (const std::exception &e) {
        result.passed   = false;
        result.errorMsg = std::string("EXCEPTION: ") + e.what();
    }

    GetResults()[name] = result;
    return result;
}

bool TestRegistry::RunTest(const std::string &name)
{
    auto result = RunTestWithResult(name);
    std::cout << "Running test: " << name << "... ";
    if (result.passed) {
        std::cout << "PASSED (" << result.elapsedMs << "ms)" << std::endl;
    }
    else {
        std::cout << "FAILED";
        if (!result.errorMsg.empty()) {
            std::cout << " - " << result.errorMsg;
        }
        std::cout << std::endl;
    }
    return result.passed;
}

std::vector<TestResult> TestRegistry::RunAllTestsWithResults()
{
    auto                    &tests = GetTests();
    std::vector<TestResult>  results;
    
    for (const auto &test : tests) {
        results.push_back(RunTestWithResult(test.first));
    }
    
    return results;
}

bool TestRegistry::RunAllTests()
{
    auto results = RunAllTestsWithResults();
    
    int passed = 0;
    int total  = results.size();

    std::cout << "\n=== Running " << total << " tests ===\n"
              << std::endl;

    for (const auto &result : results) {
        std::cout << "Running test: " << result.name << "... ";
        if (result.passed) {
            std::cout << "PASSED (" << result.elapsedMs << "ms)" << std::endl;
            passed++;
        }
        else {
            std::cout << "FAILED";
            if (!result.errorMsg.empty()) {
                std::cout << " - " << result.errorMsg;
            }
            std::cout << std::endl;
        }
    }

    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Passed: " << passed << "/" << total << std::endl;
    std::cout << "Result: " << (passed == total ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << std::endl;

    return passed == total;
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

TestResult TestRegistry::GetLastResult(const std::string &name)
{
    auto &results = GetResults();
    auto  it      = results.find(name);
    if (it != results.end()) {
        return it->second;
    }
    TestResult notFound;
    notFound.name = name;
    return notFound;
}
} // namespace TestFramework