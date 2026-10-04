#pragma once

// #include "level.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <functional>
#include <iostream>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <source_location>
#include <thread>

#include <format>
#include <fstream>
#include <unordered_map>
#include <vector>



#include "base.h"
#include "log_level.h"



TOP_LEVEL_NAMESPACE_BEGIN


extern std::string LOG_CC_API getCurentTimeStr();
// #define LOG_CC_PROFILE_ENABLE



struct LOG_CC_API Config
{
    LogLevel::T logLevel       = LogLevel::Debug;
    LogLevel::T logDetailLevel = LogLevel::Warn; // With source location

    void setLogLevel(LogLevel::T level);
    void setLogDetailLevel(LogLevel::T level);
};


struct MessageElem
{
    LogLevel::T level;
    std::string msg;
};

struct ConsoleAppender
{
    std::ostream &out = std::cout;

    ConsoleAppender() = default;

    void operator()(const MessageElem &elem)
    {
        static const std::string_view resetColor = LogLevel::color2TerminalColorCode.find(LogLevel::ETerminalColor::Reset)->second;
        const std::string_view        color      = LogLevel::level2TerminalColorCode.find(elem.level)->second;
        out.write(color.data(), color.size());
        out.write(elem.msg.data(), elem.msg.size());
        out.write(resetColor.data(), resetColor.size());
    }

    void operator<<(const MessageElem &elem)
    {
        static const std::string_view resetColor = LogLevel::color2TerminalColorCode.find(LogLevel::ETerminalColor::Reset)->second;
        const std::string_view        color      = LogLevel::level2TerminalColorCode.find(elem.level)->second;
        out.write(color.data(), color.size());
        out.write(elem.msg.data(), elem.msg.size());
        out.write(resetColor.data(), resetColor.size());
    }
};

struct FileAppender
{
    std::string   filename;
    std::ofstream fileStream;


    FileAppender() = default;

    FileAppender(std::string_view filename)
    {
        this->filename = std::string(filename);
        fileStream     = std::ofstream(std::string(filename), std::ios::out | std::ios::app);
    }
    ~FileAppender()
    {
        fileStream.close();
    }

    FileAppender(FileAppender &&other) noexcept
    {
        filename   = std::move(other.filename);
        fileStream = std::move(other.fileStream);
    }

    FileAppender &operator=(FileAppender &&other) noexcept
    {
        if (this != &other) {
            filename   = std::move(other.filename);
            fileStream = std::move(other.fileStream);
        }
        return *this;
    }

    FileAppender(const FileAppender &)            = delete;
    FileAppender &operator=(const FileAppender &) = delete;


    void flush()
    {
        fileStream.flush();
    }

    void operator()(const MessageElem &elem)
    {
        fileStream << elem.msg;
    }

    void operator<<(const MessageElem &elem)
    {
        fileStream << elem.msg;
    }
};



struct LOG_CC_API DefaultFormatter
{
    bool operator()(const Config &config, std::string &output, LogLevel::T level, std::string_view msg, const std::source_location &location);
};

struct LOG_CC_API CategoryFormatter
{
    std::string category;

    bool operator()(const Config &config, std::string &output, LogLevel::T level, std::string_view msg, const std::source_location &location);
};

//----------------------
#pragma region Async Log


struct MessageQueue
{


    std::queue<MessageElem> queue;
    std::mutex              mutex;
    std::condition_variable cv;
    bool                    bShutdown = false;

  public:

    void push(MessageElem &&elem)
    {
        std::lock_guard<std::mutex> lock(mutex);
        queue.emplace(std::move(elem));
        cv.notify_one();
    }

    bool pop(MessageElem &elem)
    {
        std::unique_lock<std::mutex> lock(mutex); // this will lock automatically! double lock cause a error
        cv.wait(lock, [this]() {
            // printf("queue size: %zu, bShutdown: %d", queue.size(), bShutdown);
            return !queue.empty() || bShutdown;
        });
        if (bShutdown && queue.empty()) {
            return false;
        }

        // why? save it to file
        while (bShutdown && !queue.empty()) {
            elem = queue.front();
            queue.pop();
            return false;
        }

        elem = std::move(queue.front());
        queue.pop();
        return true;
    }


    void shutdown()
    {
        std::lock_guard<std::mutex> lock(mutex);
        bShutdown = true;
        cv.notify_all();
    }
};

struct AsyncLogControl
{
    AsyncLogControl()                                    = default;
    AsyncLogControl(const AsyncLogControl &)                     = delete;
    AsyncLogControl(AsyncLogControl &&)                          = delete;
    AsyncLogControl          &operator=(const AsyncLogControl &) = delete;
    AsyncLogControl          &operator=(AsyncLogControl &&)      = delete;
    std::vector<FileAppender> fileAppenders;
    ConsoleAppender           consoleAppender;

    std::thread  workerThread;
    MessageQueue msgQueue;


    ~AsyncLogControl()
    {
        msgQueue.shutdown();
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }

    void run()
    {
        // std::format("{}", 123);
        static constexpr int flushIntervalSec = 10;

        auto flushTask = [this]() {
            static auto last = std::chrono::system_clock::now();
            std::size_t gap  = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - last).count();
            if (gap >= flushIntervalSec) {
                for (auto &fileAppender : fileAppenders) {
                    fileAppender.flush();
                    debug("log.cc::AsyncLogControl"),
                        (std::format(" {} log thread flushed {} , gap: {}\n", getCurentTimeStr(), fileAppender.filename, gap));
                }
                last = std::chrono::system_clock::now();
            }
        };


        workerThread = std::thread([this, flushTask]() {
            static MessageElem elem;
            while (msgQueue.pop(elem)) {
                // internalLog(std::format("pop msg:{} \n", elem.msg.c_str()));
                for (auto &fileAppender : fileAppenders) {
                    fileAppender(elem);
                }

                consoleAppender(elem);

                flushTask();
            }
        });
    }

    void push(std::string &&msg, LogLevel::T level = LogLevel::Info)
    {
        msgQueue.push({
            .level = level,
            .msg   = std::move(msg),
        });
    }

    void addFileAppender(std::string_view filename)
    {
        // auto ap = FileAppender(filename);
        // fileAppenders.push_back(std::move(ap));
        fileAppenders.emplace_back(filename);
    }
};


#pragma endregion
//----------------------


struct LOG_CC_API LoggerBase
{
    Config          config;
    ConsoleAppender consoleAppender;

    using formatter_t     = std::function<bool(const Config &config, std::string &output, LogLevel::T, std::string_view, const std::source_location &)>;
    formatter_t formatter = nullptr;


    LoggerBase()
    {
        if (!formatter) {
            formatter = DefaultFormatter{};
        }
    }

    void setFormatter(formatter_t formatter_)
    {
        formatter = formatter_;
    }
};

struct LOG_CC_API AsyncLogger : public LoggerBase
{
    using Super = LoggerBase;
    std::shared_ptr<AsyncLogControl> logCore;

    AsyncLogger(std::shared_ptr<AsyncLogControl> logCore)
        : LoggerBase()
    {
        this->logCore = logCore;
    }


    void log(LogLevel::T level, std::string_view msg, std::source_location location = std::source_location::current())
    {
        if (level < config.logLevel) {
            return;
        }
        std::string output;
        if (formatter(config, output, level, msg, location)) {
            logCore->push(std::move(output), level);
        }
    }
};

struct SyncLogger : public LoggerBase
{
    std::atomic<bool>         bProcessing = false;
    std::vector<FileAppender> fileAppenders;


    LOG_CC_API SyncLogger()
        : LoggerBase()
    {
    }

    SyncLogger(SyncLogger &&) noexcept            = default;
    SyncLogger &operator=(SyncLogger &&) noexcept = delete;
    SyncLogger(const SyncLogger &)                = delete;
    SyncLogger &operator=(const SyncLogger &)     = delete;

    LOG_CC_API void log(LogLevel::T level, std::string_view msg, std::source_location location = std::source_location::current())
    {
#ifdef LOG_CC_PROFILE_ENABLE
        using clock_t = std::chrono::steady_clock;
        auto now      = clock_t::now();
#endif
        if (level < config.logLevel) {
            return;
        }
        std::string output;
        formatter(config, output, level, msg, location);

        // wait io
        while (bProcessing.load() == true) {
        }
        bProcessing.store(true);

        {
            MessageElem elem{.level = level, .msg = std::move(output)};
            consoleAppender << elem;
            for (auto &fileAppender : fileAppenders) {
                fileAppender << elem;
            }
        }
        // reset io
        bProcessing.store(false);
#ifdef LOG_CC_PROFILE_ENABLE
        auto gap = clock_t::now();
        auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(gap - now).count();
        printf("cost: %f ms(%lld ns)\n", (double)ns / 1000000.0, ns);
#endif
    }
};



TOP_LEVEL_NAMESPACE_END