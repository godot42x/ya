
#pragma once


namespace ya
{



struct ISystem
{
    virtual void onUpdate(float deltaTime) = 0;


    virtual ~ISystem() = default;
};

// life time: exe start to exe end
struct EngineSystem : public ISystem
{
    void onUpdate(float deltaTime) override;
};

// life time: game start to game end
struct GameInstanceSystem : public ISystem
{
    void onUpdate(float deltaTime) override = 0;
};


struct RenderSystem
{
    virtual ~RenderSystem() = default;

    virtual void onUpdate(float deltaTime) = 0;
    virtual void onRender()                = 0;
};


} // namespace ya