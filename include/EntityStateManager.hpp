#ifndef ENTITY_STATE_MANAGER_HPP
#define ENTITY_STATE_MANAGER_HPP

#include <memory>
#include <string>
#include <unordered_map>
//forward declaration Entity state base class
class EntityState;

class EntityStateManager{
  private:
    std::unordered_map<std::string, std::unique_ptr<EntityState>> states;
    EntityState* currentState{nullptr};
    public:
    EntityStateManager();
    void addState(const std::string& stateName,std::unique_ptr<EntityState> state);
    void setState(const std::string& stateName);
    std::string getCurrentStateName() const;
    bool hasState(const std::string& stateName) const;
    void removeState(const std::string& stateName);
    void update();
    ~EntityStateManager();
};

#endif // ENTITY_STATE_MANAGER_HPP
