#include <cpr/cpr.h>
#include <datetimeapi.h>
#include <fmt/format.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>

static constexpr const char *sse_prop_rpath = "./SteelSeries/SteelSeries Engine 3/coreProps.json";
static constexpr const char *game = "CLOCK_DISPLAY";
static constexpr const char *name = "Clock Display";
static constexpr const char *event = "CLOCK";

bool sendRequest(const char *path, const nlohmann::json &body, bool silent = false) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    std::filesystem::path sse_prop_file = std::getenv("ProgramData");
    sse_prop_file.append(sse_prop_rpath);
    sse_prop_file = sse_prop_file.lexically_normal().make_preferred();

    nlohmann::json sse_prop;
    std::ifstream{sse_prop_file} >> sse_prop;

    std::string sse_address = "http://" + sse_prop["address"].get<std::string>();

    cpr::Response r = cpr::Post(
            cpr::Url{sse_address + path},
            cpr::Header{{"Content-Type", "application/json"}},
            cpr::Body(body.dump()),
            cpr::Timeout{500});
    if (r.error.code != cpr::ErrorCode::OK) {
        if (!silent) {
            fmt::print("Error: {} - {}\n", static_cast<int>(r.error.code), r.error.message);
        }
        return false;
    }
    if (r.status_code != 200) {
        if (!silent) {
            fmt::print("Status code: {} - {}\n", r.status_code, r.text);
            fmt::print("Content-type: {}\n", r.header["content-type"]);
            fmt::print("Body: {} \n", r.text);
        }
        return false;
    }
    return true;
}

bool remove();

bool init() {
    remove();

    nlohmann::json metadata = {
            {"game", game},
            {"game_display_name", name},
            {"icon_color_id", 6},
    };
    if (!sendRequest("/game_metadata", metadata)) {
        return false;
    }

    // now add some handlers for new event;
#if 1
    nlohmann::json event_handler = {
            {
                    {"device-type", "screened"},
                    {"zone", "one"},
                    {"mode", "screen"},
                    {"datas",
                            {
                                    {
                                            {"icon-id", 15},
                                            {"lines",
                                                    {
                                                            {
                                                                    {"has-text", true},
                                                                    {"context-frame-key", "date"},
                                                            },
                                                            {
                                                                    {"has-text", true},
                                                                    {"context-frame-key", "time"},
                                                            },
                                                    }},
                                    },
                            }},
            },
    };
#else
    nlohmann::json event_handler = {
            {
                    {"device-type", "screened"},
                    {"zone", "one"},
                    {"mode", "screen"},
                    {
                            "datas",
                            {
                                    {
                                            {"has-text", true},
                                            // {"context-frame-key", "first-line"},
                                            {"suffix", ""},
                                            {"icon-id", 15},
                                    },
                            },
                    },
            },
    };
#endif
    nlohmann::json handlers = {
            {"game", game},
            {"event", event},
            {"handlers", event_handler},
    };
    return sendRequest("/bind_game_event", handlers);
}

bool remove() {
    nlohmann::json metadata = {{"game", game}};
    return sendRequest("/remove_game", metadata, true);
}

bool send_event() {
    SYSTEMTIME date;
    GetSystemTime(&date);
    SYSTEMTIME time;
    GetLocalTime(&time);

    std::array<wchar_t, 256> date_str{};
    int res = GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,
            DATE_AUTOLAYOUT | DATE_SHORTDATE,
            &date,
            nullptr,
            date_str.data(),
            date_str.size(),
            nullptr);
    std::array<wchar_t, 256> time_str{};
    res = GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT,
            0,
            &time,
            nullptr,
            time_str.data(),
            time_str.size());

    nlohmann::json eventData = {
            {"game", game},
            {"event", event},
            {"data",
                    {
                            {"value", time_str.data()},
                            {"frame",
                                    {
                                            {"date", date_str.data()},
                                            {"time", time_str.data()},
                                    }},

                    }},
    };
    return sendRequest("/game_event", eventData);
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static bool quit = false;

void signalHandler(int /*signum*/) {
    quit = true;
}

// int main(int /*argc*/, char ** /*argv*/) {
#ifdef _WIN328
#    define WIN32_LEAN_AND_MEAN
#    include "windows.h"
INT WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR strCmdLine, INT)
#else
int main(int /*argc*/, char * /*argv*/[])
#endif
{
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    bool connected = false;
    while (!quit) {
        if (!connected) {
            connected = init();
        }
        if (connected) {
            connected = send_event();
        }
        auto next = std::chrono::ceil<std::chrono::seconds>(std::chrono::steady_clock::now());
        std::this_thread::sleep_until(next);
    }
    remove();
    return 0;
}
