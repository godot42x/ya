
#pragma once


namespace ya
{



struct ISystem
{
    virtual void onUpdate(float deltaTime) = 0;


    virtual ~ISystem() = default;
};



} // namespace ya