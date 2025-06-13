# Neon Test Framework

A simple test framework for the Neon engine that allows writing Rust-style inline tests.

## Features

- Write tests inline in your code files
- Run tests with a simple command line tool
- VS Code integration for running tests directly from the editor
- Tests are discovered automatically

## Usage

### Writing Tests

Add a test to any C++ file:

```cpp
#include "TestFramework.h"

// Your code
class Vector3 {
public:
    float x, y, z;
    
    Vector3() : x(0), y(0), z(0) {}
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}
    
    float length() const {
        return sqrt(x*x + y*y + z*z);
    }
};

// Inline test case
TEST_CASE(Vector3Length) {
    Vector3 v(3, 4, 0);
    bool success = (v.length() == 5.0f);
    return success;
}
```

### Building the Test Framework

```bash
xmake build test.cc
xmake build test_runner
```

### Running Tests

From the command line:

```bash
# List all tests
build/windows/x64/debug/test_runner.exe --list

# Run all tests
build/windows/x64/debug/test_runner.exe --all

# Run a specific test
build/windows/x64/debug/test_runner.exe Vector3Length
```

### VS Code Integration

1. Install the extension from the Tool/vscode-neon-test directory:
   ```bash
   cd Tool/vscode-neon-test
   npm install
   npm run compile
   ```

2. In VS Code:
   - Press F5 to launch the extension in debug mode
   - Open any file with a TEST_CASE macro
   - Click the "▶ Run Test" button that appears above the test

3. Configure the extension in VS Code settings:
   ```json
   "neon.test.testRunnerPath": "${workspaceFolder}/build/windows/x64/debug/test_runner.exe",
   "neon.test.testDllPath": "${workspaceFolder}/build/windows/x64/debug/test.cc.dll"
   ```

## How It Works

The test framework is based on a registry of test functions that are automatically registered when the program starts. Each TEST_CASE macro creates a static object whose constructor registers the test with the registry.

When the test runner is executed, it loads the tests from the registry and runs them.
