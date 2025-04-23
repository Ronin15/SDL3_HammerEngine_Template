#ifndef ENTITY_STATE_MANAGER_HPP
#define ENTITY_STATE_MANAGER_HPP

#include <memory>
#include <string>
#include <boost/container/flat_map.hpp>
// forward declaration Entity state base class
class EntityState;

class EntityStateManager {

 public:
  EntityStateManager();
  void addState(const std::string& stateName, std::unique_ptr<EntityState> state);
  void setState(const std::string& stateName);
  std::string getCurrentStateName() const;
  bool hasState(const std::string& stateName) const;
  void removeState(const std::string& stateName);
  void update();
  ~EntityStateManager();

  private:
   boost::container::flat_map<std::string, std::unique_ptr<EntityState>> states;
   EntityState* currentState{nullptr};
};

#endif  // ENTITY_STATE_MANAGER_HPP
