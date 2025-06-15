// DynamicLoader.h
#pragma once
#include <Windows.h>
#include <string>
#include <functional>

class DynamicLibraryLoader {
private:
    HMODULE hModule = nullptr;
    
public:
    // Load a DLL
    bool LoadLibrary(const std::string& dllPath) {
        hModule = ::LoadLibraryA(dllPath.c_str());
        return hModule != nullptr;
    }
    
    // Get a specific function from the DLL
    template<typename FuncType>
    FuncType GetFunction(const std::string& functionName) {
        if (!hModule) return nullptr;
        
        FARPROC proc = GetProcAddress(hModule, functionName.c_str());
        return reinterpret_cast<FuncType>(proc);
    }
    
    // Unload the DLL
    void UnloadLibrary() {
        if (hModule) {
            FreeLibrary(hModule);
            hModule = nullptr;
        }
    }
    
    ~DynamicLibraryLoader() {
        UnloadLibrary();
    }
};

// Usage example:
void UseDynamicLibrary() {
    DynamicLibraryLoader loader;
    
    // Load the DLL (static constructors run here!)
    if (loader.LoadLibrary("MyPlugin.dll")) {
        
        // Get specific functions
        typedef void (*InitFunc)();
        typedef int (*ProcessFunc)(int);
        
        auto initFunc = loader.GetFunction<InitFunc>("InitializePlugin");
        auto processFunc = loader.GetFunction<ProcessFunc>("ProcessData");
        
        if (initFunc) {
            initFunc(); // Call initialization
        }
        
        if (processFunc) {
            int result = processFunc(42); // Call specific function
        }
    }
    
    // DLL is automatically unloaded when loader goes out of scope
}
