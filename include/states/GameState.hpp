/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef GAME_STATE_HPP
#define GAME_STATE_HPP

#include <string>
// pure virtual for inheritance

class GameState {
 public:
  virtual bool enter() = 0;
  virtual void update() = 0;
  virtual void render() = 0;
  virtual bool exit() = 0;
  virtual std::string getName() const = 0;
  virtual ~GameState() = default;
};
#endif  // GAME_STATE_HPP
