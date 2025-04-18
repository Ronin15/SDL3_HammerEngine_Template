#ifndef TEXTURE_MANAGER_HPP
#define TEXTURE_MANAGER_HPP

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <map>
#include <string>

class TextureManager {
 public:
 TextureManager() {}
 ~TextureManager() {}

  static TextureManager* Instance() {
    if (sp_Instance == 0) {
      sp_Instance = new TextureManager();
      return sp_Instance;
    }

    return sp_Instance;
  }

  // Loads a texture from a file or all PNG textures from a directory
  // If fileName is a directory, it loads all PNG files from that directory
  // Returns true if at least one texture was loaded successfully
  // When loading a directory, textureID is used as a prefix for filenames
  bool load(std::string fileName,
            std::string textureID,
            SDL_Renderer* p_renderer);

  void draw(std::string textureID,
            int x,
            int y,
            int width,
            int height,
            SDL_Renderer* p_Renderer,
            SDL_FlipMode flip = SDL_FLIP_NONE);

  void drawFrame(std::string textureID,
                 int x,
                 int y,
                 int width,
                 int height,
                 int currentRow,
                 int currentFrame,
                 SDL_Renderer* p_renderer,
                 SDL_FlipMode flip = SDL_FLIP_NONE);
  void drawParallax(std::string textureID,
                    int x,
                    int y,
                    int width,
                    int height,
                    int scroll,
                    SDL_Renderer* p_renderer);
  void clearFromTexMap(std::string textureID);
  bool isTextureInMap(std::string textureID);

 private:
  std::string m_textureID{""};
  std::map<std::string, SDL_Texture*> m_textureMap;
  static TextureManager* sp_Instance;
};

#endif  // TEXTURE_MANAGER_HPP
