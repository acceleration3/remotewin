#include "logging/frame_timing.hpp"

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

int main() {
    const auto folder = std::filesystem::temp_directory_path() /
        ("rwin-frame-timing-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(folder);
    ::setenv("RWIN_TIMING_DIR", folder.c_str(), 1);
    {
        rwin::FrameTiming timing("test");
        timing.note("capture", 3, 123456, 2);
        for (int i = 0; i < 65540; ++i) timing.note("draw", 4);
    }
    ::unsetenv("RWIN_TIMING_DIR");
    int status = 1;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        std::ifstream file(entry.path());
        std::string line;
        std::getline(file, line);
        if (line != "event,local_us,frame,source_100ns,detail") break;
        std::getline(file, line);
        if (line.find(",3,123456,2") == std::string::npos) break;
        int rows = 1;
        std::string last;
        while (std::getline(file, line)) {
            last = line;
            ++rows;
        }
        status = rows == 65537 && last == "overflow,0,0,0,5" ? 0 : 1;
        std::filesystem::remove(entry.path());
    }
    std::filesystem::remove(folder);
    return status;
}
