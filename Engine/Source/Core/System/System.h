
#pragma once


namespace ya
{



struct ISystem
{
    virtual void init() {}
    virtual void onUpdate(float deltaTime) {}
    virtual void shutdown() {}
    virtual void onRenderGUI() {}


    virtual ~ISystem() = default;
};

// life time: exe start to exe end
struct EngineSystem : public ISystem
{
    virtual void onUpdate(float deltaTime) = 0;
};

// life time: game start to game end
struct GameInstanceSystem : public ISystem
{
};




} // namespace ya