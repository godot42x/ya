


#pragma once

#include "Core/Base.h"
#include "FName.h"

struct MessageBus
{
    using cb_t = std::function<void()>;
    std::unordered_map<FName, std::vector<cb_t>> subscribers;


    static MessageBus &get()
    {
        static MessageBus instance;
        return instance;
    }

    template <typename... Args>
    bool publish(const FName topic, Args... args)
    {
        auto it = subscribers.find(topic);
        if (it != subscribers.end())
        {
            for (auto &cb : it->second)
            {
                cb.target<void(Args...)>()(std::forward<Args>(args)...);
            }
            return true;
        }
        return false;
    }

    template <typename CallbackType>
    void subscribe(const FName topic, CallbackType cb)
    {
        subscribers[topic].emplace_back(cb);
    }
};


namespace Test
{

void test_message_bus()
{
    auto &bus = MessageBus::get();
    bus.subscribe("test", []() { std::cout << "Message received!" << std::endl; });
    bus.publish("test", 1, 2, 3);
}
} // namespace Test