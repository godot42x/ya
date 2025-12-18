# test.cc - Rust-Style Inline Test Framework for C++

A modern, lightweight test framework for C++ that allows writing Rust-style inline tests with seamless VS Code integration.

## ✨ Features

- **Inline Test Definitions**: Write tests directly in your source files using `TEST_CASE` macro
- **VS Code Integration**: 
  - CodeLens buttons (▶️ Run, 🐛 Debug) appear above each test
  - Native Test Explorer sidebar support
  - Real-time test result display with timing
  - Inline error diagnostics for failed tests
- **Cross-Platform**: Supports Windows, Linux, and macOS
- **Multiple Build Systems**: Works with both XMake and CMake
- **Zero Dependencies**: Self-contained framework with no external requirements
- **Test Result Tracking**: Persistent test results with timing information

## 📦 Installation

### 1. Build System Setup

**Using XMake (Recommended):**
```bash
xmake build test.cc
xmake build test-runner
```

**Using CMake:**
```bash
cmake -B build
cmake --build build --target test-runner
```

### 2. VS Code Extension Setup

1. Navigate to the extension directory:
   ```bash
   cd Engine/Plugins/Tesings/vscode
   ```

2. Install dependencies and compile:
   ```bash
   npm install
   npm run compile
   ```

3. Install the extension:
   - Open VS Code
   - Press `F5` to launch Extension Development Host
   - Or package and install: `vsce package` → Install `.vsix` file

## 🚀 Usage

### Writing Tests

Add tests inline in any C++ file:

```cpp
#include "test.cc/framework.h"

// Your production code
class Vector3 {
public:
    float x, y, z;
    
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}
    
    float length() const {
        return sqrt(x*x + y*y + z*z);
    }
};

// Inline test case - appears right where you need it!
TEST_CASE(Vector3Length) {
    Vector3 v(3, 4, 0);
    TEST_ASSERT_EQ(5.0f, v.length());
    return true;
}

TEST_CASE(Vector3Constructor) {
    Vector3 v(1, 2, 3);
    TEST_ASSERT(v.x == 1.0f);
    TEST_ASSERT(v.y == 2.0f);
    TEST_ASSERT(v.z == 3.0f);
    return true;
}
```

### Running Tests

**From VS Code:**

1. **CodeLens Buttons** (Appears above each test):
   - Click `▶️ Run Test` to execute a single test
   - Click `🐛 Debug Test` to debug with breakpoints
   - Click `🧪 Run All Tests` (at file top) to run all tests in file

2. **Test Explorer Sidebar**:
   - Open Testing view (`Ctrl+Shift+T` / `Cmd+Shift+T`)
   - See all tests in a tree view
   - Run/debug tests individually or in batches
   - View test results with timing

3. **Command Palette** (`Ctrl+Shift+P` / `Cmd+Shift+P`):
   - `ya: Run Test` - Run specific test
   - `ya: Run All Tests` - Run all discovered tests
   - `ya: Build Tests` - Build test runner
   - `ya: Debug Test` - Debug specific test

**From Command Line:**

```bash
# List all tests
./build/windows/x64/debug/test-runner --list  # Windows/XMake
./build/bin/test-runner --list                # CMake

# Run all tests
./test-runner

# Run specific test
./test-runner Vector3Length

# Run with output
./test-runner 2>&1 | tee test-output.txt
```

### Test Result Display

**CodeLens shows test status:**
- `✅ Run Test (12.5ms)` - Passed with timing
- `❌ Run Test (failed)` - Failed (click for details)
- `▶️ Run Test` - Not yet run

**Failed tests show inline diagnostics:**
- Red underline in editor
- Error message in Problems panel
- Hover for detailed failure info

## 🔧 Configuration

### VS Code Settings

Open Settings (`Ctrl+,` / `Cmd+,`) and search for "ya.test":

```jsonc
{
  // Build system to use (auto-detect, xmake, or cmake)
  "ya.test.buildSystem": "auto",
  
  // Build mode (debug or release)
  "ya.test.buildMode": "debug",
  
  // Build directory
  "ya.test.buildDir": "${workspaceFolder}/build",
  
  // Custom test runner path (optional)
  "ya.test.testRunnerPath": "",
  
  // Auto-build before running tests
  "ya.test.autoBuild": true
}
```

### Build System Integration

**XMake (xmake.lua):**
```lua
target("my-project")
    set_kind("binary")
    add_deps("test.cc")
    add_includedirs("Engine/Plugins/Tesings/include")
    add_files("src/*.cpp")
```

**CMake (CMakeLists.txt):**
```cmake
add_subdirectory(Engine/Plugins/Tesings)

add_executable(my-project src/main.cpp)
target_link_libraries(my-project PRIVATE test.cc)
```

## 📚 API Reference

### Macros

**`TEST_CASE(TestName)`**
- Defines a test case
- `TestName` must be a valid C++ identifier
- Returns `bool` (true = pass, false = fail)

**`TEST_ASSERT(condition)`**
- Asserts a condition is true
- Prints failure message with file/line on failure
- Returns false immediately on failure

**`TEST_ASSERT_EQ(expected, actual)`**
- Asserts two values are equal
- Prints both values on failure
- Works with any type that supports `operator==` and `operator<<`

### Test Registry API

```cpp
namespace TestFramework {
    // Run a single test and get detailed result
    TestResult RunTestWithResult(const std::string& name);
    
    // Run all tests and get results
    std::vector<TestResult> RunAllTestsWithResults();
    
    // Get test location (file:line)
    std::string GetTestLocation(const std::string& name);
    
    // Get cached result from last run
    TestResult GetLastResult(const std::string& name);
    
    // Check if test exists
    bool HasTest(const std::string& name);
    
    // Get all test names
    std::vector<std::string> GetTestNames();
}
```

### TestResult Structure

```cpp
struct TestResult {
    std::string name;        // Test name
    bool        passed;      // true if test passed
    double      elapsedMs;   // Execution time in milliseconds
    std::string output;      // Captured output (future)
    std::string errorMsg;    // Error message if failed
};
```

## 🎯 Examples

### Basic Assertions

```cpp
TEST_CASE(BasicAssertions) {
    TEST_ASSERT(true);
    TEST_ASSERT(2 + 2 == 4);
    TEST_ASSERT_EQ(42, 6 * 7);
    return true;
}
```

### Testing Classes

```cpp
class Calculator {
public:
    int add(int a, int b) { return a + b; }
    int multiply(int a, int b) { return a * b; }
};

TEST_CASE(CalculatorAdd) {
    Calculator calc;
    TEST_ASSERT_EQ(5, calc.add(2, 3));
    TEST_ASSERT_EQ(0, calc.add(-1, 1));
    return true;
}

TEST_CASE(CalculatorMultiply) {
    Calculator calc;
    TEST_ASSERT_EQ(6, calc.multiply(2, 3));
    TEST_ASSERT_EQ(0, calc.multiply(5, 0));
    return true;
}
```

### Edge Cases and Error Testing

```cpp
TEST_CASE(DivisionByZero) {
    bool exceptionThrown = false;
    try {
        int result = 10 / 0;
    } catch (const std::exception& e) {
        exceptionThrown = true;
    }
    TEST_ASSERT(exceptionThrown);
    return true;
}
```

## 🔍 Troubleshooting

### Test Runner Not Found
- Run `ya: Build Tests` from Command Palette
- Check `ya.test.buildMode` matches your build configuration
- Verify build system (XMake/CMake) is installed

### Tests Not Appearing in CodeLens
- Ensure file contains `TEST_CASE` macro
- Reload VS Code window (`Ctrl+R` / `Cmd+R`)
- Check extension is activated (C++ file open)

### Debugging Not Working
- Install C++ debugger extension (C/C++ or CodeLLDB)
- Ensure test runner built in debug mode
- Check `launch.json` configuration

### Cross-Platform Path Issues
- Use forward slashes `/` in paths
- Set `ya.test.testRunnerPath` explicitly if needed
- Check build output directory structure

## 🚧 Advanced Features

### Custom Test Runners

Create your own test runner:
`cpp
#include "test.cc/framework.h"
#include <iostream>

int main() {
    auto testNames = TestFramework::TestRegistry::GetTestNames();
    
    std::cout << "Running " << testNames.size() << " tests...\n";
    
    for (const auto& name : testNames) {
        auto result = TestFramework::TestRegistry::RunTestWithResult(name);
        
        if (result.passed) {
            std::cout << "✓ " << name << " (" << result.elapsedMs << "ms)\n";
        } else {
            std::cout << "✗ " << name << ": " << result.errorMsg << "\n";
        }
    }
    
    return 0;
}
`

### Test Fixtures (Workaround)

While the framework doesn't have built-in fixtures, you can use helper functions:

`cpp
struct DatabaseFixture {
    Database db;
    
    DatabaseFixture() {
        db.connect("test.db");
        db.createTables();
    }
    
    ~DatabaseFixture() {
        db.dropTables();
        db.disconnect();
    }
};

TEST_CASE(DatabaseInsert) {
    DatabaseFixture fixture;
    
    fixture.db.insert("user", {{"name", "Alice"}});
    TEST_ASSERT_EQ(1, fixture.db.count("user"));
    
    return true;
}
`

## 📝 Best Practices

1. **Test Near Code**: Place tests close to the code they test
2. **Descriptive Names**: Use clear, descriptive test names (e.g., ParserHandlesEmptyInput)
3. **Single Responsibility**: Each test should verify one thing
4. **Fast Tests**: Keep tests fast for quick feedback
5. **Independent Tests**: Tests should not depend on each other's state

## 🤝 Contributing

Contributions are welcome! Areas for improvement:

- [ ] Test fixtures/setup/teardown
- [ ] Parameterized tests
- [ ] Test tagging/categorization
- [ ] HTML test reports
- [ ] Code coverage integration
- [ ] Mocking support

## 📄 License

This test framework is part of the YA Engine project.

## 🙏 Acknowledgments

Inspired by:
- Rust's #[test] attribute
- Catch2 C++ test framework
- Google Test

---

**Happy Testing! 🧪**
