#include <cpr/cpr.h>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fmt/os.h>
#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <thread>  // for std::this_thread::sleep_until

static constexpr const char *kLogRpath = "./sseClock.log";
static constexpr const char *kLogRpathBak = "./sseClock.log.bak";
static constexpr auto kMaxLogSize = 10 * 1024 * 1024;

static constexpr const char *SsePropRpath = "./SteelSeries/SteelSeries Engine 3/coreProps.json";
static constexpr const char *kSseAppId = "CLOCK_DISPLAY";
static constexpr const char *kSseDisplayName = "Clock Display";
static constexpr const char *kSseEventId = "CLOCK";

// NOLINTNEXTLINE(concurrency-mt-unsafe)
static const char *const tmpPath = std::getenv("TMP");

template <typename... Args>
void logPrint(Args &&...args) {
    std::filesystem::path log_path = tmpPath;
    log_path.append(kLogRpath);
    log_path = log_path.lexically_normal().make_preferred();
    std::error_code ec;

    if (std::filesystem::exists(log_path, ec) && std::filesystem::file_size(log_path, ec) > kMaxLogSize) {
        {
            fmt::ostream log_stream = fmt::output_file(log_path.string(), fmt::file::APPEND | fmt::file::WRONLY);
            log_stream.print("{} - Log end\n", std::chrono::system_clock::now());
        }

        std::filesystem::path log_path_bak = tmpPath;
        log_path_bak.append(kLogRpathBak);
        log_path_bak = log_path_bak.lexically_normal().make_preferred();

        std::filesystem::rename(log_path, log_path_bak, ec);
    }

    if (!std::filesystem::exists(log_path, ec)) {
        fmt::ostream log_stream = fmt::output_file(log_path.string(), fmt::file::CREATE | fmt::file::WRONLY);
        log_stream.print("{} - Log start\n", std::chrono::system_clock::now());
    }
    fmt::ostream log_stream = fmt::output_file(log_path.string(), fmt::file::APPEND | fmt::file::WRONLY);
    log_stream.print("{} - ", std::chrono::system_clock::now());
    log_stream.print(std::forward<Args>(args)...);
}

bool sendRequest(const char *path, const nlohmann::json &body, bool silent = false) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    std::filesystem::path sse_prop_file = std::getenv("ProgramData");
    sse_prop_file.append(SsePropRpath);
    sse_prop_file = sse_prop_file.lexically_normal().make_preferred();

    nlohmann::json sse_prop;
    std::ifstream{sse_prop_file} >> sse_prop;

    std::string sse_address = "http://" + sse_prop["address"].get<std::string>();

    static std::string prev_address;
    if (sse_address != prev_address) {
        logPrint("Using address: {}\n", sse_address);
        prev_address = sse_address;
    }

    cpr::Response r = cpr::Post(
            cpr::Url{sse_address + path},
            cpr::Header{{"Content-Type", "application/json"}},
            cpr::Body(body.dump()),
            cpr::Timeout{500});
    if (r.error.code != cpr::ErrorCode::OK) {
        if (!silent) {
            logPrint("Error: {} - {}\n", static_cast<int>(r.error.code), r.error.message);
        }
        return false;
    }
    if (r.status_code != 200) {
        if (!silent) {
            logPrint("Status code: {} - {}\n", r.status_code, r.status_line);
            logPrint("Content-type: {}\n", r.header["content-type"]);
            logPrint("Body: {} \n", r.text);
        }
        return false;
    }
    return true;
}

bool remove();

bool init() {
    remove();

    nlohmann::json metadata = {
            {"game", kSseAppId},
            {"game_display_name", kSseDisplayName},
            {"icon_color_id", 6},
    };
    if (!sendRequest("/game_metadata", metadata)) {
        return false;
    }

    // now add some handlers for new event;
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
    nlohmann::json handlers = {
            {"game", kSseAppId},
            {"event", kSseEventId},
            {"handlers", event_handler},
    };
    return sendRequest("/bind_game_event", handlers);
}

bool remove() {
    nlohmann::json metadata = {{"game", kSseAppId}};
    return sendRequest("/remove_game", metadata, true);
}

bool send_event() {
    SYSTEMTIME date;
    GetSystemTime(&date);
    SYSTEMTIME time;
    GetLocalTime(&time);

    std::array<wchar_t, 256> date_str{};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,
            DATE_AUTOLAYOUT | DATE_SHORTDATE,
            &date,
            nullptr,
            date_str.data(),
            date_str.size(),
            nullptr);
    std::array<wchar_t, 256> time_str{};
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT,
            0,
            &time,
            nullptr,
            time_str.data(),
            time_str.size());

    nlohmann::json eventData = {
            {"game", kSseAppId},
            {"event", kSseEventId},
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

int main(int /*argc*/, char * /*argv*/[]) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    logPrint("=== Application start ===\n");

    if (AttachConsole(ATTACH_PARENT_PROCESS) == TRUE) {
        logPrint("Attaching to parent console\n");
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        std::freopen("CON", "r", stdin);
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        std::freopen("CON", "w", stdout);
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        std::freopen("CON", "w", stderr);
    } else {
        logPrint("No parent console\n");
    }

    fmt::print("Logging to {}\n", tmpPath);

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
