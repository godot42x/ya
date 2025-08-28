#include <any>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

constexpr const char *a = "123";



struct Tag
{
    std::vector<std::string> parts;

    constexpr Tag() {}


    auto toString() const
    {
        std::string result;
        for (const auto &part : parts)
        {
            if (!result.empty())
            {
                result += ".";
            }
            result += part;
        }
        return result;
    }
    constexpr Tag &operator,(const char *what)
    {
        parts.emplace_back(what);
        return *this;
    }
    constexpr Tag &operator[](const std::string &what)
    {
        parts.emplace_back(what);
        return *this;
    }
};


Tag b = (Tag{}, "123", "1235");
#define TAG(...) (Tag{}, __VA_ARGS__)

Tag c = TAG("game", "event", "123");
Tag D = Tag()["game"]["event"]["123"];

using FName = std::string;

struct MessageBus
{

    using cb_t = std::function<void(void *)>;

    struct Subscriber
    {
        std::type_index type;
        cb_t            cb;
    };

    std::unordered_map<FName, std::vector<Subscriber>> _subscribers;

    static MessageBus &Get()
    {
        static MessageBus Instance;
        return Instance;
    }

    // 订阅特定类型的消息
    template <typename T>
    void subscribe(const FName &topic, std::function<void(const T &)> callback)
    {
        _subscribers[topic].emplace_back(Subscriber{
            .type = std::type_index(typeid(T)),
            .cb   = [callback](void *msg) {
                callback(*static_cast<T *>(msg));
            }});
    }

    // 便利的Lambda订阅
    template <typename T, typename Lambda>
    void subscribe(const FName &Topic, Lambda &&Callback)
    {
        subscribe<T>(Topic, std::function<void(const T &)>(std::forward<Lambda>(Callback)));
    }

    // 广播特定类型的消息
    template <typename T>
    void publish(const FName &topic, const T &message)
    {
        auto It = _subscribers.find(topic);

        if (It != _subscribers.end())
        {
            auto type = std::type_index(typeid(T));
            for (const auto &subscriber : It->second)
            {

                if (subscriber.type != type)
                {
                    // LOG ERROR
                    std::cout << "Type mismatch for topic: " << topic << type.name() << " " << subscriber.type.name() << std::endl;
                    continue; // 类型不匹配，跳过
                }

                subscriber.cb((void *)&message);
            }
        }
    }
};

// 示例消息结构体
struct FPlayerDamageMessage
{
    int   PlayerId;
    float Damage;
    FName DamageType;

    FPlayerDamageMessage(int id, float dmg, const std::string &type)
        : PlayerId(id), Damage(dmg), DamageType(type) {}
};

struct FPlayerLevelUpMessage
{
    int PlayerId;
    int NewLevel;
    int ExperienceGained;

    FPlayerLevelUpMessage(int id, int level, int exp)
        : PlayerId(id), NewLevel(level), ExperienceGained(exp) {}
};

namespace Test
{

void test_message_bus()
{
    std::cout << "=== UE Gameplay Message System Demo ===\n";

    auto &bus = MessageBus::Get();

    // Key Point 1: Same topic with different parameter types
    std::cout << "\n1. Same topic supports different message types:\n";

    bus.subscribe<int>("game.event", [](const int &a) {
        std::cout << "  Received int message: " << a << std::endl;
    });

    bus.subscribe<std::string>("game.event", [](const std::string &msg) {
        std::cout << "  Received string message: " << msg << std::endl;
    });

    bus.subscribe<FPlayerDamageMessage>("game.event", [](const FPlayerDamageMessage &msg) {
        std::cout << "  Received damage message: Player " << msg.PlayerId << " took " << msg.Damage << " damage" << std::endl;
    });

    // Broadcast different types to the same topic
    bus.publish("game.event", 42);
    bus.publish("game.event", std::string("Hello UE"));
    bus.publish("game.event", FPlayerDamageMessage(1, 25.5f, "Fire"));

    // Key Point 2: Type safety - only matching types receive messages
    std::cout << "\n2. Type safety demonstration:\n";

    bus.subscribe<float>("player.stats", [](const float &value) {
        std::cout << "  Player stat updated: " << value << std::endl;
    });

    // Only float subscribers will receive this
    bus.publish("player.stats", 100.0f);
    // This won't trigger float subscribers due to type mismatch
    bus.publish("player.stats", std::string("This won't reach float subscribers"));

    // Key Point 3: Multiple subscribers for same type
    std::cout << "\n3. Multiple subscriber support:\n";

    bus.subscribe<FPlayerLevelUpMessage>("player.levelup", [](const FPlayerLevelUpMessage &msg) {
        std::cout << "  System log: Player " << msg.PlayerId << " reached level " << msg.NewLevel << std::endl;
    });

    bus.subscribe<FPlayerLevelUpMessage>("player.levelup", [](const FPlayerLevelUpMessage &msg) {
        std::cout << "  UI update: Show level up effect" << std::endl;
    });

    bus.subscribe<FPlayerLevelUpMessage>("player.levelup", [](const FPlayerLevelUpMessage &msg) {
        std::cout << "  Achievement system: Check level achievements" << std::endl;
    });

    bus.publish("player.levelup", FPlayerLevelUpMessage(1, 5, 1000));
}

} // namespace Test

int main()
{
    Test::test_message_bus();

    std::cout << b.toString() << std::endl;
    return 0;
}