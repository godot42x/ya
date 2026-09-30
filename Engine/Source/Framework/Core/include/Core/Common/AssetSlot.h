#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <utility>
#include <vector>

namespace ya
{

enum class EAssetSlotState : uint8_t
{
    Loading = 0,
    Ready,
    Failed,
};

/// Point-to-point subscriptions to a slot's updates. Game-thread only: the
/// resource layer fills slots on the game thread and subscribers manage
/// their tokens there, so the list carries no lock. Callbacks must only
/// enqueue work -- no registry mutation, no command recording, no
/// (un)subscribing on the same slot inside a callback.
class AssetObservers
{
  public:
    /// Unsubscribes on destruction. A token must not outlive the slot it
    /// observes; holders keep it next to the slot handle, whose shared
    /// count keeps the slot alive.
    class Token
    {
      public:
        AssetObservers*                          _observers = nullptr;
        std::list<std::function<void()>>::iterator _it{};

        Token() = default;
        ~Token()
        {
            reset();
        }
        Token(Token&& other) noexcept
            : _observers(std::exchange(other._observers, nullptr))
            , _it(std::move(other._it))
        {
        }
        Token& operator=(Token&& other) noexcept
        {
            if (this != &other) {
                reset();
                _observers = std::exchange(other._observers, nullptr);
                _it        = std::move(other._it);
            }
            return *this;
        }
        Token(const Token&)            = delete;
        Token& operator=(const Token&) = delete;

        void reset()
        {
            if (_observers != nullptr) {
                _observers->_callbacks.erase(_it);
                _observers = nullptr;
            }
        }
        [[nodiscard]] bool valid() const { return _observers != nullptr; }
    };

    /// Subscribe to the slot's fills; the token unsubscribes when it dies.
    [[nodiscard]] Token subscribe(std::function<void()> callback)
    {
        Token token;
        token._observers = this;
        token._it        = _callbacks.emplace(_callbacks.end(), std::move(callback));
        return token;
    }

    /// Snapshot of the callbacks. The manager gathers them under its lock
    /// and dispatches outside, so a callback may not depend on the lock.
    [[nodiscard]] std::vector<std::function<void()>> gather() const
    {
        return {_callbacks.begin(), _callbacks.end()};
    }

  private:
    friend class Token;
    std::list<std::function<void()>> _callbacks;
};

// One loaded asset, shared by every ref that names it. The resource layer
// owns and fills the slot on the game thread; refs only read it, so a load
// completing (or a reload replacing the resource) is visible to all of them
// at once without anyone polling.
//
// Ready implies a non-null resource. A reload keeps the slot Ready with the
// previous resource until the replacement is uploaded. generation advances
// every time the slot is updated (ready, failed or replaced); subscribers
// hear about each update through `observers`.
template <typename T>
struct AssetSlot
{
    EAssetSlotState    state = EAssetSlotState::Loading;
    std::shared_ptr<T> resource;
    uint64_t           generation = 0;
    /// Fill subscriptions. Mutable: handles hand out `const AssetSlot<T>`,
    /// and subscribing is a reader-side act.
    mutable AssetObservers observers;
};

template <typename T>
using AssetHandle = std::shared_ptr<const AssetSlot<T>>;

} // namespace ya
