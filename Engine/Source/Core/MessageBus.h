

#pragma once

#include "Core/Base.h"
#include "Core/Event.h"
#include "FName.h"
#include <algorithm>
#include <functional>
#include <optional>

#include "Debug/Instrumentor.h"



namespace ya
{

struct MessageBus
{


    using cb_t = std::function<void(void *)>;

    struct Subscriber
    {
        std::size_t           type;
        cb_t                  cb;
        std::optional<void *> context; // Optional context pointer for member function binding
    };
    struct EventSubscriber
    {
        cb_t                  cb;
        std::optional<void *> context; // Optional context pointer for member function binding
    };

    std::unordered_map<FName, std::vector<Subscriber>>          _subscribers;
    std::unordered_map<EEvent::T, std::vector<EventSubscriber>> _eventSubscribers;
    // TODO: merge normal subscriptions and event subscriptions int  one map
    // for: every subscription has a unique id, can be used to unsubscribe
    std::unordered_map<uint64_t, std::vector<EventSubscriber>>  _allEventSubscribers;

    static MessageBus &get()
    {
        static MessageBus Instance;
        return Instance;
    }

    void unsubscribe(void *context)
    {
        // 取消消息订阅
        for (auto &topicPair : _subscribers)
        {
            topicPair.second.erase(
                std::remove_if(topicPair.second.begin(), topicPair.second.end(), [context](const Subscriber &subscriber) {
                    return subscriber.context.has_value() && subscriber.context.value() == context;
                }),
                topicPair.second.end());
        }

        // 取消事件订阅
        for (auto &eventPair : _eventSubscribers)
        {
            eventPair.second.erase(
                std::remove_if(eventPair.second.begin(),
                               eventPair.second.end(),
                               [context](const EventSubscriber &subscriber) {
                                   return subscriber.context.has_value() && subscriber.context.value() == context;
                               }),
                eventPair.second.end());
        }
    }

    // MARK: FName

    // 订阅特定类型的消息
    template <typename T>
    void subscribe(const FName &topic, std::function<void(const T &)> callback)
    {
        _subscribers[topic].emplace_back(Subscriber{
            .type = typeid(T).hash_code(),
            .cb   = [callback](void *msg) {
                callback(*static_cast<T *>(msg));
            },
            .context = {}});
    }

    // 便利的Lambda订阅
    template <typename T, typename Lambda>
    void subscribe(const FName &topic, Lambda &&callback)
    {
        subscribe<T>(topic, std::function<void(const T &)>(std::forward<Lambda>(callback)));
    }



    // 绑定类成员函数 - 支持消息类型
    template <typename T, typename Obj>
    void subscribe(const FName &topic, Obj *obj, void (Obj::*member_func)(const T &))
    {
        _subscribers[topic].emplace_back(Subscriber{
            .type = typeid(T).hash_code(),
            .cb   = [obj, member_func](void *msg) {
                (obj->*member_func)(*static_cast<T *>(msg));
            },
            .context = obj});
    }

    // 绑定类常量成员函数 - 支持消息类型
    template <typename T, typename Obj>
    void subscribe(const FName &topic, Obj *obj, bool (Obj::*member_func)(const T &) const)
    {
        _subscribers[topic].emplace_back(Subscriber{
            .type = typeid(T).hash_code(),
            .cb   = [obj, member_func](void *msg) {
                (obj->*member_func)(*static_cast<T *>(msg));
            },
            .context = obj});
    }

    // 绑定类成员函数 - 支持非const引用参数
    template <typename T, typename Obj>
    void subscribe(const FName &topic, Obj *obj, bool (Obj::*member_func)(T))
    {
        _subscribers[topic].emplace_back(Subscriber{
            .type = typeid(T).hash_code(),
            .cb   = [obj, member_func](void *msg) {
                (obj->*member_func)(*static_cast<T *>(msg));
            },
            .context = obj});
    }


    template <typename T>
    void publish(const FName &topic, const T &message)
    {
        auto It = _subscribers.find(topic);

        if (It != _subscribers.end())
        {
            // 清理无效的上下文指针
            It->second.erase(
                std::remove_if(It->second.begin(), It->second.end(), [](const Subscriber &subscriber) {
                    return subscriber.context.has_value() && subscriber.context.value() == nullptr;
                }),
                It->second.end());

            auto type = typeid(T).hash_code();
            for (const auto &subscriber : It->second)
            {
                if (YA_ENSURE(subscriber.type == type, "Type mismatch for event:  {} {}", typeid(Event).name(), typeid(subscriber.type).name()))
                {
                    subscriber.cb((void *)&message);
                }
            }
        }
    }

    // MARK: Event
    // 绑定类成员函数 - 支持事件类型
    template <typename EventType, typename Obj>
        requires(std::is_base_of_v<Event, EventType>)
    void subscribe(Obj *obj, bool (Obj::*func)(const EventType &))
    {
        _eventSubscribers[EventType::getStaticType()].push_back(
            EventSubscriber{
                .cb = [obj, func](void *msg) {
                    (obj->*func)(*static_cast<EventType *>(msg));
                },
                .context = obj});
    }

    template <typename EventType, typename Obj>
        requires(std::is_base_of_v<Event, EventType>)
    void subscribe(Obj context, std::function<bool(const EventType &)> cb)
    {
        _eventSubscribers[EventType::getStaticType()].push_back(
            EventSubscriber{
                .cb = [cb](void *msg) {
                    return cb(*static_cast<EventType *>(msg));
                },
                .context = context,
            });
    }


    template <typename EventType>
        requires(std::is_base_of_v<Event, EventType>)
    void subscribe(std::function<bool(const EventType &)> cb)
    {
        _eventSubscribers[EventType::getStaticType()].push_back(
            EventSubscriber{
                .cb = [cb](void *msg) {
                    return cb(*static_cast<EventType *>(msg));
                },
                .context = {},
            });
    }

    template <typename EventType>
    void publish(const EventType &event)
    {
        // YA_PROFILE_SCOPE(std::format("{}", event.getName()));
        if (auto it = _eventSubscribers.find(EventType::getStaticType()); it != _eventSubscribers.end())
        {
            // 清理无效的上下文指针
            it->second.erase(
                std::remove_if(it->second.begin(), it->second.end(), [](const EventSubscriber &subscriber) {
                    return subscriber.context.has_value() && subscriber.context.value() == nullptr;
                }),
                it->second.end());

            for (const EventSubscriber &subscriber : it->second)
            {
                subscriber.cb((void *)&event);
            }
        }
    }
};


} // namespace ya