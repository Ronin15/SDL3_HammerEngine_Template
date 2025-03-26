#include "Game_State_Manager.hpp"
#include "Game_State.hpp"

 void Game_State_Manager::run(std::unique_ptr<Game_State> initialState){
    states.push_back(std::move(initialState));

    while(!states.empty()){
      //get current state
      Game_State* currentState = states.back().get();

      //update and render current state.
      currentState->Update();
      currentState->Render();

      //check if state is complete
      if (currentState->IsComplete()){
        //get next state if any
        std::unique_ptr<Game_State> nextState = currentState->getNextState();
        //remove current state
        states.pop_back();
        //add next state if exists
        if (nextState){
          states.push_back(std::move(nextState));
        }
      }
    }
 }
