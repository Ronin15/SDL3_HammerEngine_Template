/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "../../../include/ai/internal/RequestQueue.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>

namespace AIInternal {

RequestQueue::RequestQueue(size_t capacity)
    : m_capacity(nextPowerOf2(std::max(capacity, size_t(4))))
    , m_requests(std::make_unique<PathfindingRequest[]>(m_capacity))
    , m_mask(m_capacity - 1)
{
    // Initialize all requests to default state
    for (size_t i = 0; i < m_capacity; ++i) {
        m_requests[i] = PathfindingRequest{};
    }
}

bool RequestQueue::enqueue(const PathfindingRequest& request) {
    // Load current indices
    const size_t currentTail = m_tail.load(std::memory_order_relaxed);
    const size_t nextTail = (currentTail + 1) & m_mask;
    
    // Check if queue is full (would collide with head)
    if (nextTail == m_head.load(std::memory_order_acquire)) {
        m_enqueueFailed.fetch_add(1, std::memory_order_relaxed);
        return false; // Queue full
    }
    
    // Copy request data to the queue slot
    m_requests[currentTail] = request;
    
    // Commit the enqueue by advancing tail (release semantics ensure request data is visible)
    m_tail.store(nextTail, std::memory_order_release);
    
    // Update statistics
    m_totalEnqueues.fetch_add(1, std::memory_order_relaxed);
    
    return true;
}

bool RequestQueue::dequeue(PathfindingRequest& outRequest) {
    // Load current indices
    const size_t currentHead = m_head.load(std::memory_order_relaxed);
    
    // Check if queue is empty
    if (currentHead == m_tail.load(std::memory_order_acquire)) {
        m_dequeueFailed.fetch_add(1, std::memory_order_relaxed);
        return false; // Queue empty
    }
    
    // Copy request data from the queue slot
    outRequest = m_requests[currentHead];
    
    // Clear the slot (optional, but helps with debugging)
    m_requests[currentHead] = PathfindingRequest{};
    
    // Commit the dequeue by advancing head (release semantics ensure slot is available)
    const size_t nextHead = (currentHead + 1) & m_mask;
    m_head.store(nextHead, std::memory_order_release);
    
    // Update statistics
    m_totalDequeues.fetch_add(1, std::memory_order_relaxed);
    
    return true;
}

size_t RequestQueue::size() const {
    const size_t currentTail = m_tail.load(std::memory_order_acquire);
    const size_t currentHead = m_head.load(std::memory_order_acquire);
    
    // Handle wraparound correctly
    return (currentTail - currentHead) & m_mask;
}

bool RequestQueue::empty() const {
    return m_head.load(std::memory_order_acquire) == m_tail.load(std::memory_order_acquire);
}

bool RequestQueue::full() const {
    const size_t currentTail = m_tail.load(std::memory_order_acquire);
    const size_t nextTail = (currentTail + 1) & m_mask;
    return nextTail == m_head.load(std::memory_order_acquire);
}

RequestQueue::Statistics RequestQueue::getStatistics() const {
    Statistics stats;
    stats.totalEnqueues = m_totalEnqueues.load(std::memory_order_relaxed);
    stats.totalDequeues = m_totalDequeues.load(std::memory_order_relaxed);
    stats.enqueueFailed = m_enqueueFailed.load(std::memory_order_relaxed);
    stats.dequeueFailed = m_dequeueFailed.load(std::memory_order_relaxed);
    stats.currentSize = size();
    stats.maxCapacity = m_capacity;
    
    if (m_capacity > 0) {
        stats.utilizationPercent = (static_cast<double>(stats.currentSize) / m_capacity) * 100.0;
    }
    
    return stats;
}

void RequestQueue::resetStatistics() {
    m_totalEnqueues.store(0, std::memory_order_relaxed);
    m_totalDequeues.store(0, std::memory_order_relaxed);
    m_enqueueFailed.store(0, std::memory_order_relaxed);
    m_dequeueFailed.store(0, std::memory_order_relaxed);
}

size_t RequestQueue::nextPowerOf2(size_t value) {
    if (value == 0) return 1;
    
    // Handle power of 2 values
    if ((value & (value - 1)) == 0) {
        return value;
    }
    
    // Find next power of 2
    value--;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    if (sizeof(size_t) > 4) {
        value |= value >> 32;
    }
    value++;
    
    return value;
}

} // namespace AIInternal