#include "TestFramework.h"
#include <iostream>

// Declarations for test registration
extern void RegisterManualTests();
extern void ForceTestCaseInitialization();
extern void ForceExampleTestsInitialization();

int main() {
    std::cout << "Test Runner - Running all registered tests" << std::endl;
    
    // Register manual tests
    RegisterManualTests();
    
    // Force macro-based test registration
    ForceTestCaseInitialization();
    ForceExampleTestsInitialization();
    
    std::cout << "Found " << TestFramework::TestRegistry::GetTestCount() << " tests" << std::endl;
    
    bool success = TestFramework::TestRegistry::RunAllTests();
    
    return success ? 0 : 1;
}
