#pragma once
#include <Windows.h>
#include <string>

namespace Utils {

    typedef void(*LogCallback)(const std::string&);
    void SetLogCallback(LogCallback cb);
    void Log(const std::string& msg);
    void LogError(const std::string& msg);
}
