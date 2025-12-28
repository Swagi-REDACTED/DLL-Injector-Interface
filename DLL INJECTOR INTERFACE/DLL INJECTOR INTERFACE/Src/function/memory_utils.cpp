#include "memory_utils.hpp"
#include <iostream>

namespace Utils {
    static LogCallback g_LogCallback = nullptr;

    void SetLogCallback(LogCallback cb) {
        g_LogCallback = cb;
    }

    void Log(const std::string& msg) {
        if (g_LogCallback) {
            g_LogCallback("[+] " + msg);
        }
        else {
            std::cout << "[+] " << msg << std::endl;
        }
    }

    void LogError(const std::string& msg) {
        if (g_LogCallback) {
            g_LogCallback("[-] " + msg);
        }
        else {
            std::cerr << "[-] " << msg << std::endl;
        }
    }
}
