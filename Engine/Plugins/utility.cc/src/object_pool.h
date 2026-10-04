
#pragma once

#include <queue>
#include <stdexcept>
#include <vector>



template <typename T>
struct ObjectPool
{
    std::vector<T *> _allObjects;
    std::queue<T *>  _availableObjects;
    size_t           initialSize = 5;
    size_t           maxSize     = 20;

    ~ObjectPool()
    {
        for (T *obj : _allObjects) {
            delete obj;
        }
        _allObjects.clear();
        while (!_availableObjects.empty()) {
            _availableObjects.pop();
        }
    }

    T *acquire()
    {
        if (!_availableObjects.empty()) {
            T *obj = _availableObjects.front();
            _availableObjects.pop();
            return obj;
        }

        if (_allObjects.size() < initialSize) {
            T *newObj = new T();
            _allObjects.push_back(newObj);
            _availableObjects.push(newObj);
            return newObj;
        }

        if (_allObjects.size() < maxSize) {
            T *newObj = new T();
            _allObjects.push_back(newObj);
            return newObj;
        }

        // Pool is exhausted

        if (_allObjects.size() >= maxSize) {
            throw std::runtime_error("ObjectPool exhausted");
        }


        throw std::runtime_error("No available objects in the pool");
    }

    void returnBack(T *obj)
    {
        if (obj) {
            _availableObjects.push(obj);
        }

        // auto shrinkToFit
        // if (_availableObjects.size() > initialSize) {
        //     T *obj = _availableObjects.front();
        //     _availableObjects.pop();
        //     auto it = std::find(_allObjects.begin(), _allObjects.end(), obj);
        //     if (it != _allObjects.end()) {
        //         delete obj;
        //         _allObjects.erase(it);
        //     }
        // }
    }
};