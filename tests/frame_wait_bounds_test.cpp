// Actual replay parsing and ring block, with only a pre-index range/setting observer
// inserted by the Python driver. No Vulkan, GPU, timing waits or game data.
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

static int expected_ahead;
static unsigned observations;
static std::vector<uint64_t> waited;
static std::vector<std::string> logs;
[[maybe_unused]] static void logf(const char* format, ...) {
    char text[256];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    logs.emplace_back(text);
}
static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static void check_ahead(int ahead, std::size_t capacity) {
    ++observations;
    require(ahead <= 0 || static_cast<std::size_t>(ahead) <= capacity,
            "positive frame wait exceeds ring capacity before indexing");
    require((ahead > 0 ? ahead : 0) == expected_ahead, "unexpected effective parsed setting");
}
static void wait_value(uint64_t value) { waited.push_back(value); }
static void replay_frame(uint64_t presents, uint64_t value) {
#include "frame_wait_actual.inc"
}
static void setting(const char* value) {
#ifdef _WIN32
    require(_putenv_s("RX_VK_WAIT_GPU", value ? value : "") == 0, "authored child environment failed");
#else
    require((value ? setenv("RX_VK_WAIT_GPU", value, 1) : unsetenv("RX_VK_WAIT_GPU")) == 0,
            "authored child environment failed");
#endif
}

int main(int argc, char** argv) {
    const std::string mode = argc == 2 ? argv[1] : "";
    try {
        const char* input = nullptr;
        if (mode == "unset") expected_ahead = 0;
        else if (mode == "empty") input = "", expected_ahead = 0;
        else if (mode == "zero") input = "0", expected_ahead = 0;
        else if (mode == "negative") input = "-7", expected_ahead = 0;
        else if (mode == "one") input = "1", expected_ahead = 1;
        else if (mode == "two") input = "2", expected_ahead = 2;
        else if (mode == "three") input = "3", expected_ahead = 3;
        else if (mode == "four") input = "4", expected_ahead = 4;
        else if (mode == "five") input = "5", expected_ahead = 5;
        else if (mode == "six") input = "6", expected_ahead = 6;
        else if (mode == "seven") input = "7", expected_ahead = 7;
        else if (mode == "eight") input = "8", expected_ahead = 8;
        else if (mode == "nine") input = "9", expected_ahead = 8;
        else if (mode == "prefix") input = " \t+3frames", expected_ahead = 3;
        else if (mode == "malformed") input = "frames", expected_ahead = 0;
        else if (mode == "int_max") input = "2147483647", expected_ahead = 8;
        else if (mode == "positive_overflow") input = "999999999999999999999999999999999999999999999999999999999999", expected_ahead = 8;
        else if (mode == "negative_overflow") input = "-999999999999999999999999999999999999999999999999999999999999", expected_ahead = 0;
        else throw std::runtime_error("unknown fixture case");
        setting(input);
        for (uint64_t frame = 0; frame < 40; ++frame) {
            // A later environment change must not reinitialize the production ring policy.
            if (frame == 5) setting(expected_ahead ? "0" : "8");
            const auto before = waited.size();
            replay_frame(frame, 101 + frame);
            const bool should_wait = expected_ahead > 0 && frame >= static_cast<unsigned>(expected_ahead);
            require(waited.size() == before + (should_wait ? 1 : 0), "changed wait count");
            if (should_wait)
                require(waited.back() == 101 + frame - expected_ahead, "changed ring wait ordering");
        }
        require(observations == 40, "pre-index observer did not see every frame");
        const bool clamped = mode == "nine" || mode == "int_max" || mode == "positive_overflow";
        require(logs.size() == (clamped ? 1u : 0u), "clamped setting diagnostic must appear once, valid settings stay quiet");
        if (clamped)
            require(logs[0].find("RX_VK_WAIT_GPU=") != std::string::npos && logs[0].find("using 8") != std::string::npos,
                    "clamped setting diagnostic omits the option or effective ring capacity");
        std::printf("PASS %s\n", mode.c_str());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL %s: %s\n", mode.c_str(), e.what());
        return 1;
    }
}
