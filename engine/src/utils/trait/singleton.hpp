


#pragma once


template <class T>
class Singleton
{
  public:
    static T *instance()
    {
        static T instance;
        return &instance;
    }
};
