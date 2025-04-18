#include "TextureManager.hpp"
#include <iostream>

TextureManager* TextureManager::sp_Instance{nullptr};

bool TextureManager::load(std::string fileName,
                          std::string textureID,
                          SDL_Renderer* p_renderer) {
  SDL_Surface* p_tempSurface = IMG_Load(fileName.c_str());

  std::cout << "Forge Game Engine - Loading texture: " << fileName << "!\n";

  if (p_tempSurface == 0) {
    std::cout << "Forge Game Engine - Could not load image: " << SDL_GetError();

    return false;
  }

  SDL_Texture* p_texture = SDL_CreateTextureFromSurface(p_renderer, p_tempSurface);

  SDL_DestroySurface(p_tempSurface);

  if (p_texture != 0) {
    m_textureMap[textureID] = p_texture;
    return true;
  }

  std::cout << "Forge Game Engine - Could not create Texture: " << SDL_GetError();

  return false;
}

void TextureManager::draw(std::string textureID,
                          int x,
                          int y,
                          int width,
                          int height,
                          SDL_Renderer* p_renderer,
                          SDL_FlipMode flip) {
  SDL_FRect srcRect;
  SDL_FRect destRect;
  SDL_FPoint center = {0, 0};  // Initialize center point
  double angle = 0.0;

  srcRect.x = 0;
  srcRect.y = 0;
  srcRect.w = destRect.w = width;
  srcRect.h = destRect.h = height;
  destRect.x = x;
  destRect.y = y;

  SDL_RenderTextureRotated(p_renderer, m_textureMap[textureID], &srcRect, &destRect, angle, &center, flip);
}

void TextureManager::drawFrame(std::string textureID,
                               int x,
                               int y,
                               int width,
                               int height,
                               int currentRow,
                               int currentFrame,
                               SDL_Renderer* p_renderer,
                               SDL_FlipMode flip) {
  SDL_FRect srcRect;
  SDL_FRect destRect;
  SDL_FPoint center = {0, 0};  // Initialize center point
  double angle = 0.0;

  srcRect.x = width * currentFrame;
  srcRect.y = height * (currentRow - 1);
  srcRect.w = destRect.w = width;
  srcRect.h = destRect.h = height;
  destRect.x = x;
  destRect.y = y;

  SDL_RenderTextureRotated(p_renderer, m_textureMap[textureID], &srcRect, &destRect, angle, &center, flip);
}
/*
void TextureManager::drawParallax(std::string textureID, int x, int y, int
width, int height, int scroll, SDL_Renderer* pRenderer) {

  SDL_Rect srcRect1;
  SDL_Rect destRect1;

  SDL_Rect srcRect2;
  SDL_Rect destRect2;
  scroll = scroll % width;
  srcRect1.x = 0;
  srcRect1.y = 0;
  srcRect1.w = destRect1.w = width;
  srcRect1.h = destRect1.h = height;
  destRect1.x = x;
  destRect1.y = y;

  srcRect2.x = (0 - width) + 1;
  srcRect2.y = 0;
  srcRect2.w = destRect2.w = width;
  srcRect2.h = destRect2.h = height;
  destRect2.x = x;
  destRect2.y = y;

  SDL_RenderCopy(pRenderer, textureMap_[textureID], &srcRect1, &destRect1);
  SDL_RenderCopy(pRenderer, textureMap_[textureID], &srcRect2, &destRect2);
}
*/
void TextureManager::clearFromTexMap(std::string textureID) {
    std::cout << "Forge Game Engine - Cleared : " << textureID << " texture" << std::endl;
  m_textureMap.erase(textureID);
}

bool TextureManager::isTextureInMap(std::string textureID) {
  return m_textureMap.find(textureID) != m_textureMap.end();
}
