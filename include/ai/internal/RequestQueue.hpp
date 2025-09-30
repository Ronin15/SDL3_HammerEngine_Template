/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef REQUEST_QUEUE_HPP
#define REQUEST_QUEUE_HPP

/**
 * @file RequestQueue.hpp
 * @brief Ultra-high-performance lock-free circular buffer for pathfinding requests
 *
 * This lock-free queue is designed for maximum performance in the pathfinding system:
 * - Lock-free operations using atomic indices (no mutex contention)
 * - Circular buffer design for efficient memory usage
 * - Single-producer (AIManager), single-consumer (PathfindingWorker) model
 * - Request submission completes in <0.001ms
 * - Cache-friendly fixed-size structure
 */

#include "utils/Vector2D.hpp"
#include "entities/Entity.hpp"
#include "PathPriority.hpp"
#include <atomic>
#include <functional>
#include <vector>

namespace AIInternal {

/**
 * @brief Pathfinding request structure optimized for cache efficiency
 */
struct PathfindingRequest {
    EntityID entityId{0};
    Vector2D start{0.0f, 0.0f};
    Vector2D goal{0.0f, 0.0f};
    PathPriority priority{PathPriority::Normal};
    std::function<void(EntityID, const std::vector<Vector2D>&)> callback{nullptr};
    uint64_t timestamp{0};
    uint64_t requestId{0};

    // Default constructor for array initialization
    PathfindingRequest() = default;

    // Constructor for actual requests
    PathfindingRequest(EntityID id, const Vector2D& s, const Vector2D& g,
                      PathPriority p, std::function<void(EntityID, const std::vector<Vector2D>&)> cb,
                      uint64_t ts, uint64_t rid)
        : entityId(id), start(s), goal(g), priority(p), callback(cb), timestamp(ts), requestId(rid) {}
};

/**
 * @brief Lock-free circular buffer queue for pathfinding requests
 *
 * This queue provides ultra-fast enqueueing for pathfinding requests with the following guarantees:
 * - Lock-free operations (no mutex, no blocking)
 * - Single-producer, single-consumer safe
 * - Fixed-size circular buffer with power-of-2 size for efficient modulo
 * - Memory ordering guarantees for cross-thread communication
 * - Bounded queue with overflow detection
 */
class RequestQueue {
public:
    /**
     * @brief Constructs a request queue with specified capacity
     * @param capacity Queue capacity (will be rounded up to nearest power of 2)
     */
    explicit RequestQueue(size_t capacity = DEFAULT_CAPACITY);

    /**
     * @brief Destructor
     */
    ~RequestQueue() = default;

    /**
     * @brief Attempts to enqueue a pathfinding request (lock-free, non-blocking)
     * @param request The request to enqueue
     * @return true if successfully enqueued, false if queue is full
     *
     * This method is designed to complete in <0.001ms with no blocking operations:
     * - Single atomic load to check available space
     * - Memory copy of request data
     * - Single atomic store to commit the request
     */
    bool enqueue(const PathfindingRequest& request);

    /**
     * @brief Attempts to dequeue a pathfinding request (lock-free, non-blocking)
     * @param outRequest Reference to store the dequeued request
     * @return true if successfully dequeued, false if queue is empty
     *
     * This method is used by the background PathfindingWorker thread:
     * - Single atomic load to check available requests
     * - Memory copy of request data
     * - Single atomic store to commit the dequeue
     */
    bool dequeue(PathfindingRequest& outRequest);

    /**
     * @brief Gets the current number of requests in the queue (approximate)
     * @return Current queue size (may be slightly stale due to concurrent access)
     */
    size_t size() const;

    /**
     * @brief Checks if the queue is empty (approximate)
     * @return true if queue appears empty, false otherwise
     */
    bool empty() const;

    /**
     * @brief Checks if the queue is full (approximate)
     * @return true if queue appears full, false otherwise
     */
    bool full() const;

    /**
     * @brief Gets the maximum capacity of the queue
     * @return Queue capacity
     */
    size_t capacity() const { return m_capacity; }

    /**
     * @brief Gets queue statistics for monitoring
     * @return Statistics structure with performance metrics
     */
    struct Statistics {
        uint64_t totalEnqueues{0};
        uint64_t totalDequeues{0};
        uint64_t enqueueFailed{0};
        uint64_t dequeueFailed{0};
        size_t currentSize{0};
        size_t maxCapacity{0};
        double utilizationPercent{0.0};
    };
    Statistics getStatistics() const;

    /**
     * @brief Resets all statistics counters
     */
    void resetStatistics();

private:
    // Default capacity (power of 2 for efficient modulo)
    static constexpr size_t DEFAULT_CAPACITY = 1024;

    // Queue storage - order matches constructor initialization
    size_t m_capacity;
    std::unique_ptr<PathfindingRequest[]> m_requests;
    size_t m_mask; // For efficient modulo with power-of-2 sizes

    // Lock-free indices with cache line padding to avoid false sharing
    alignas(64) std::atomic<size_t> m_head{0};    // Consumer index
    alignas(64) std::atomic<size_t> m_tail{0};    // Producer index

    // Statistics (atomic for thread safety)
    alignas(64) mutable std::atomic<uint64_t> m_totalEnqueues{0};
    mutable std::atomic<uint64_t> m_totalDequeues{0};
    mutable std::atomic<uint64_t> m_enqueueFailed{0};
    mutable std::atomic<uint64_t> m_dequeueFailed{0};

    /**
     * @brief Rounds up to the next power of 2
     * @param value Input value
     * @return Next power of 2 >= value
     */
    static size_t nextPowerOf2(size_t value);

    // Prevent copying and assignment
    RequestQueue(const RequestQueue&) = delete;
    RequestQueue& operator=(const RequestQueue&) = delete;
};

} // namespace AIInternal

#endif // REQUEST_QUEUE_HPP
