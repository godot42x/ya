#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>       // std::hardware_destructive_interference_size
#include <optional>
#include <type_traits>

namespace ya
{

/**
 * @brief Fixed-size, lock-free Single-Producer Single-Consumer (SPSC) ring buffer.
 *
 * Zero contention: one thread writes, one thread reads, no CAS needed.
 * Uses acquire/release memory ordering on atomic head/tail indices.
 *
 * Ideal for: worker → main-thread callback delivery (one worker, one consumer).
 *
 * @tparam T        Element type (must be move-constructible).
 * @tparam Capacity Fixed ring buffer size (must be power-of-two for fast modulo).
 */
template <typename T, size_t Capacity>
class SPSCQueue
{
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two");
    static_assert(Capacity >= 2, "Capacity must be at least 2");

  public:
    SPSCQueue() = default;

    /**
     * @brief Push an element (producer only, single thread).
     * @return true if enqueued, false if full.
     */
    bool tryPush(T&& value)
    {
        const size_t head = _head.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & kMask;

        if (next == _tail.load(std::memory_order_acquire)) {
            return false; // Full
        }

        new (&_storage[head]) T(std::move(value));
        _head.store(next, std::memory_order_release);
        return true;
    }

    bool tryPush(const T& value)
    {
        T copy = value;
        return tryPush(std::move(copy));
    }

    /**
     * @brief Pop an element (consumer only, single thread).
     * @return The element, or std::nullopt if empty.
     */
    std::optional<T> tryPop()
    {
        const size_t tail = _tail.load(std::memory_order_relaxed);

        if (tail == _head.load(std::memory_order_acquire)) {
            return std::nullopt; // Empty
        }

        T* ptr = reinterpret_cast<T*>(&_storage[tail]);
        T  value = std::move(*ptr);
        ptr->~T();

        _tail.store((tail + 1) & kMask, std::memory_order_release);
        return value;
    }

    /**
     * @brief Check if queue is empty (approximate, safe from either side).
     */
    [[nodiscard]] bool empty() const
    {
        return _head.load(std::memory_order_acquire) == _tail.load(std::memory_order_acquire);
    }

    /**
     * @brief Approximate size (safe from either side, may be stale).
     */
    [[nodiscard]] size_t sizeApprox() const
    {
        const size_t h = _head.load(std::memory_order_acquire);
        const size_t t = _tail.load(std::memory_order_acquire);
        return (h - t) & kMask;
    }

    static constexpr size_t capacity() { return Capacity; }

  private:
    static constexpr size_t kMask = Capacity - 1;

    // Cache-line padding to prevent false sharing between producer and consumer.
    // Producer writes _head, consumer writes _tail — they must be on different cache lines.
    alignas(64) std::atomic<size_t> _head{0};
    alignas(64) std::atomic<size_t> _tail{0};

    // Aligned storage for elements (no default construction).
    alignas(alignof(T)) std::aligned_storage_t<sizeof(T), alignof(T)> _storage[Capacity];
};


/**
 * @brief Lock-free Multi-Producer Single-Consumer (MPSC) queue.
 *
 * Uses a CAS-based intrusive linked list (Michael & Scott style, simplified).
 * Multiple threads can push concurrently; only ONE thread may pop.
 *
 * - Push: O(1), lock-free (CAS on head pointer)
 * - Pop:  O(1), wait-free (single consumer)
 * - Memory: heap-allocated nodes (one `new` per push, `delete` per pop)
 *
 * Ideal for: multiple workers → main thread callback delivery.
 *
 * @tparam T Element type (must be move-constructible).
 */
template <typename T>
class MPSCQueue
{
    struct Node
    {
        T                    data;
        std::atomic<Node*>   next{nullptr};

        template <typename U>
        explicit Node(U&& val) : data(std::forward<U>(val)) {}
    };

  public:
    MPSCQueue()
    {
        // Sentinel node — simplifies push/pop logic (no null checks).
        auto* sentinel = new Node(T{});
        _head.store(sentinel, std::memory_order_relaxed);
        _tail = sentinel;
    }

    ~MPSCQueue()
    {
        // Drain all remaining nodes.
        while (tryPop()) {}
        // Delete sentinel.
        delete _tail;
    }

    MPSCQueue(const MPSCQueue&)            = delete;
    MPSCQueue& operator=(const MPSCQueue&) = delete;

    /**
     * @brief Push an element (thread-safe, lock-free, any thread).
     *
     * Uses CAS loop on the head pointer.  Typical fast path: 1 CAS attempt.
     */
    void push(T value)
    {
        auto* node = new Node(std::move(value));

        // Atomically swing _head to point to our new node.
        // prev->next will be set AFTER the CAS succeeds.
        Node* prev = _head.exchange(node, std::memory_order_acq_rel);

        // Link: the previous head's `next` now points to the new node.
        // The consumer sees this link via acquire load on `next`.
        prev->next.store(node, std::memory_order_release);
    }

    /**
     * @brief Pop an element (single consumer only!).
     * @return The element, or std::nullopt if empty.
     */
    std::optional<T> tryPop()
    {
        Node* tail = _tail;                                              // Current sentinel
        Node* next = tail->next.load(std::memory_order_acquire);         // First real element

        if (next == nullptr) {
            return std::nullopt; // Empty
        }

        // Advance tail past the old sentinel.
        // The old sentinel becomes garbage; `next` becomes the new sentinel.
        T value = std::move(next->data);
        _tail   = next;
        delete tail; // Free old sentinel

        return value;
    }

    /**
     * @brief Approximate emptiness check (safe from any thread).
     */
    [[nodiscard]] bool empty() const
    {
        // _tail is only read by consumer thread. We read _head from any thread.
        // This is approximate: a push() may be in-flight (prev->next not yet linked).
        Node* tail = _tail;
        return tail->next.load(std::memory_order_acquire) == nullptr;
    }

  private:
    alignas(64) std::atomic<Node*>  _head;   // Push end (multi-producer, CAS)
    alignas(64) Node*               _tail;   // Pop end  (single-consumer, no atomic needed)
};

} // namespace ya
