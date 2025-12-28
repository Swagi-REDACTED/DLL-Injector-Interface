#pragma once
#include <string>
#include <Windows.h>

namespace InjectorAPI {
    bool Inject(DWORD pid, const std::string& dllPath, int methodIndex, bool eraseHeaders = true);
}
