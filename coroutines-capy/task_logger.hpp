#ifndef TASK_LOGGER_HPP
#define TASK_LOGGER_HPP

#include <iostream>
#include <format>
#include <syncstream>

struct NullDev
{
    template<typename T>
    NullDev& operator<<(T&&)
    {
        return *this;
    }
};

#ifdef LOG_ENABLED
#define LOGGER std::osyncstream{std::cout}
#else
#define LOGGER NullDev{}
#endif

#endif