#pragma once
#include "core/base.h"
#include "utils/delegate.h"

#include "SDL_error.h"
#include "SDL_events.h"
#include "SDL_rect.h"
#include "SDL_render.h"
#include "SDL_video.h"

#include <glm/glm.hpp>

#include <SDL.h>


struct SDLState : public Layer
{
    SDL_Window   *m_Window       = nullptr;
    SDL_Renderer *m_RenderHandle = nullptr;

    int       m_Width      = 1024;
    int       m_Height     = 768;
    glm::vec4 m_ClearColor = {0, 0, 0, SDL_ALPHA_OPAQUE};


    glm::mat3 m_Transform;

    MulticastDelegate<> OnExit;


    void Init() override
    {
        if (0 != SDL_Init(SDL_INIT_VIDEO)) {
            panic("SDL_Init error");
        }

        m_Window = SDL_CreateWindow(
            "Neon",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            m_Width,
            m_Height,
            0);
        NE_ASSERT(m_Window, "Failed to create window: {}", SDL_GetError());

        m_RenderHandle = SDL_CreateRenderer(m_Window, -1, 0);
        NE_ASSERT(m_RenderHandle, "Failed to create renderer: {}", SDL_GetError());

        glm::mat3 yInverse =
            {
                {1, 0, 0},
                {0, -1, 0},
                {0, 0, 1}};
        glm::mat3 translateH =
            {
                {1, 0, 0},
                {0, 1, 0},
                {0, m_Height, 1}};
        glm::mat3 sdlTransform = translateH * yInverse;
    }

    void Uninit() override
    {
        SDL_DestroyRenderer(m_RenderHandle);
        SDL_DestroyWindow(m_Window);
        SDL_Quit();
    }

    void OnUpdate() override
    {
        SDL_Event event;
        SDL_PollEvent(&event);
        if (event.type == SDL_QUIT) {
            OnExit.Broadcast();
        }


        if constexpr (1) // clear color
        {
            SDL_SetRenderDrawColor(m_RenderHandle,
                                   m_ClearColor.x,
                                   m_ClearColor.y,
                                   m_ClearColor.z,
                                   m_ClearColor.w);
            SDL_RenderClear(m_RenderHandle);
        }


        SDL_Rect rect;
        float    w = m_Width / 2.0f;
        float    h = m_Height / 2.0f;
        float    x = m_Width / 2.0f - w / 2.0f;
        float    y = m_Height / 2.0f - h / 2.0f;
        rect.x     = x;
        rect.y     = y;
        rect.w     = w;
        rect.h     = h;
        SDL_SetRenderDrawColor(m_RenderHandle, 50, 60, 100, 255);
        SDL_RenderFillRect(m_RenderHandle, &rect);

        SDL_RenderPresent(m_RenderHandle);
    }

    void DrawPixel(int x, int y)
    {
        // Need set the draw color once
        SDL_RenderDrawPoint(m_RenderHandle, x, y);
    }
};
