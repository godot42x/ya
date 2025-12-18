# Quick Start Guide - test.cc Framework

Get up and running with Rust-style inline tests in 5 minutes!

## 1️⃣ Build the Framework (30 seconds)

**Option A: Using XMake**
```bash
cd Engine/Plugins/Tesings
xmake build test.cc
xmake build test-runner
```

**Option B: Using CMake**
```bash
cd Engine/Plugins/Tesings
cmake -B build
cmake --build build --target test-runner
```

## 2️⃣ Install VS Code Extension (1 minute)

```bash
cd vscode
npm install
npm run compile
```

Then press `F5` in VS Code to launch the extension.

## 3️⃣ Write Your First Test (1 minute)

Create `test_example.cpp`:

```cpp
#include "test.cc/framework.h"

TEST_CASE(MyFirstTest) {
    int result = 2 + 2;
    TEST_ASSERT_EQ(4, result);
    return true;
}

TEST_CASE(StringTest) {
    std::string hello = "Hello, World!";
    TEST_ASSERT(hello.length() == 13);
    return true;
}
```

## 4️⃣ Run Tests from VS Code (10 seconds)

Open `test_example.cpp` in VS Code and you'll see:

```
🧪 Run All Tests (2 found)          ← Click to run all

TEST_CASE(MyFirstTest) {            
▶️ Run Test    🐛 Debug Test        ← Click to run/debug

TEST_CASE(StringTest) {
▶️ Run Test    🐛 Debug Test
```

Click `▶️ Run Test` and watch the magic happen! ✨

## 5️⃣ View Results

Results appear instantly:
- ✅ `Run Test (2.5ms)` - Passed with timing
- ❌ `Run Test (failed)` - Failed (shows error inline)

Failed tests show red underlines with error messages!

---

## Next Steps

📖 **Read the full README**: `Engine/Plugins/Tesings/README.md`

🔧 **Configure settings**: Open VS Code Settings → Search "ya.test"

🎯 **Check examples**: See `example/BasicTests.cpp`

🐛 **Debug tests**: Click `🐛 Debug Test` button (sets breakpoints!)

🧪 **Test Explorer**: Open Testing sidebar (`Ctrl+Shift+T`)

---

## Troubleshooting

**Tests not appearing?**
- Ensure file extension is `.cpp`
- Reload VS Code window (`Ctrl+R`)

**Build failed?**
- Check XMake/CMake is installed
- Run `ya: Build Tests` from Command Palette

**Can't debug?**
- Install C/C++ extension for VS Code
- Build in debug mode: `ya.test.buildMode: "debug"`

---

**That's it! Happy testing! 🚀**
