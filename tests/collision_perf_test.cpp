
#include "managers/CollisionManager.hpp"
#include "collisions/AABB.hpp"
#include <chrono>
#include <iostream>
#include <random>

int main() {
    CollisionManager& cm = CollisionManager::Instance();
    cm.init();
    
    // Create test bodies
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(-500.0, 500.0);
    
    const int bodyCount = 1000;
    std::vector<EntityID> bodies;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Insert bodies
    for (int i = 0; i < bodyCount; ++i) {
        EntityID id = i + 1;
        float x = dis(gen);
        float y = dis(gen);
        AABB aabb(x, y, 16.0f, 16.0f);
        cm.addBody(id, aabb, BodyType::DYNAMIC);
        bodies.push_back(id);
    }
    
    auto insertEnd = std::chrono::high_resolution_clock::now();
    auto insertMs = std::chrono::duration_cast<std::chrono::milliseconds>(insertEnd - start).count();
    
    // Update all bodies (simulate movement)
    auto updateStart = std::chrono::high_resolution_clock::now();
    for (int frame = 0; frame < 100; ++frame) {
        for (EntityID id : bodies) {
            float newX = dis(gen);
            float newY = dis(gen);
            cm.setKinematicPose(id, Vector2D(newX, newY));
        }
        cm.update(0.016f); // 60 FPS
    }
    
    auto updateEnd = std::chrono::high_resolution_clock::now();
    auto updateMs = std::chrono::duration_cast<std::chrono::milliseconds>(updateEnd - updateStart).count();
    
    std::cout << "Performance Results:" << std::endl;
    std::cout << "Insert " << bodyCount << " bodies: " << insertMs << "ms" << std::endl;
    std::cout << "100 frame updates: " << updateMs << "ms" << std::endl;
    std::cout << "Average frame time: " << (updateMs / 100.0) << "ms" << std::endl;
    
    cm.clean();
    return 0;
}

