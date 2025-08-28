
#include "Core/Log.h"
#include <cxxopts.hpp>


struct CliParams
{
    int                  _argc = 0;
    char               **_argv = nullptr;
    cxxopts::Options     _opt;
    cxxopts::ParseResult _optResult;


    CliParams(const char *programName, const char *description)
        : _opt(programName, description)
    {
    }

    template <typename T>
    CliParams &opt(
        const char                       *sn,   // short name
        const std::vector<std::string>   &lns,  // long names
        const char                       *desc, // description
        const std::optional<std::string> &defaultStr = {},
        const char                       *helpStr    = "")
    {
        _opt.add_option(
            "",
            sn,
            lns,
            desc,
            defaultStr.has_value() ? cxxopts::value<T>()->default_value(*defaultStr) : cxxopts::value<T>(),
            helpStr);
        return *this;
    }

    void parse(int argc, char **argv)
    {
        _optResult = _opt.parse(argc, argv);
    }

    template <typename T>
    const T &get(const std::string &name) const
    {
        if (_optResult.contains(name)) {
            return _optResult[name].as<T>();
        }
        else {
            throw std::runtime_error("Option not found: " + name);
        }
    }

    template <typename T>
    bool tryGet(const std::string &name, T &outValue) const
    {
        if (_optResult.contains(name)) {
            outValue = _optResult[name].as<T>();
            return true;
        }
        return false;
    }

    [[nodiscard]] const char *getRaw(uint32_t index) const
    {
        if (index < static_cast<uint32_t>(_argc)) {
            return _argv[index];
        }
        else {
            throw std::out_of_range("Index out of range for command line arguments");
        }
    }
};