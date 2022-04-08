#include <cpr/cpr.h>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fmt/os.h>
#include <chrono>
#include <exception>
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

static constexpr std::chrono::seconds kMaxDelay = std::chrono::minutes{5};

//  Most of the requests require specifying both the game and event in question.
// This error is returned if one is missing. This error will also be returned if
// the JSON sent to the endpoint is malformed and cannot be parsed.
static constexpr const char *kErrorMissingGameOrEvent = "Game or event string not specified";
// Same as the above, but for requests that require only the game name.
static constexpr const char *kErrorMissingGame = "Game string not specified";
//  Game and event strings are limited to the characters described.
static constexpr const char *kErrorInvalidGameOrEventCharacter = "Game or event string contains disallowed characters. Allowed are upper-case A-Z, 0-9, hyphen, and underscore";
// Same as above, but for requests which only take the game name as a parameter.
static constexpr const char *kErrorInvalidGameCharacter = "Game string contains disallowed characters. Allowed are upper-case A-Z, 0-9, hyphen, and underscore";
// The game_event request requires a data member describing the data the event
// should use when calculating the effects to apply.
static constexpr const char *kErrorMissingGameEventMember = "GameEvent data member is empty";
// There are limited anti-spam measures implemented into the API to prevent
// malicious use. This message will show up if one of them has been triggered.
static constexpr const char *kErrorTooManyRegistration = "Events for too many games have been registered recently, please try again later";
// This message is returned if the bind_game_event request is sent without the
// handlers key or if the array in the key is empty.
static constexpr const char *kErrorMissingHandler = "One or more handlers must be specified for binding";
// Some operations cannot be performed on events which are built-in to
// SteelSeries Engine 3. This includes binding and removing the events.
static constexpr const char *kErrorReservedEvent = "That event for that game is reserved";
// Same as above, but for requests which only take the game name. This includes
// removing a game.
static constexpr const char *kErrorReservedGame = "That game is reserved";
// This is returned when attempting to remove an event which does not exist.
static constexpr const char *kErrorUnknownEvent = "That event is not registered";
// This is returned when attempting to remove a game which does not exist.
static constexpr const char *kErrorUnknownGame = "That game is not registered";

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

class SseClock {
 public:
    enum class Status {
        OK,
        TEMPORARY,
        // Remove PERMANENT errors for now. With up to 5 min delay between
        // requests, we won't big flooding the logs or use a lot of CPU if we
        // keep trying anyway, and it allows for recovering if the error is
        // really a temporary bad state in SSE
        // PERMANENT
    };

 public:
    // Returns true if the address changed
    bool checkAddress() {
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        std::filesystem::path sse_prop_file = std::getenv("ProgramData");
        sse_prop_file.append(SsePropRpath);
        sse_prop_file = sse_prop_file.lexically_normal().make_preferred();

        nlohmann::json sse_prop;
        std::ifstream{sse_prop_file} >> sse_prop;

        sse_address_ = "http://" + sse_prop["address"].get<std::string>();

        if (sse_address_ != prev_address_) {
            logPrint("Using address: {}\n", sse_address_);
            prev_address_ = sse_address_;
            return true;
        }
        return false;
    }

    Status sendRequest(const char *path, const nlohmann::json &body, bool silent = false) {
        cpr::Response r = cpr::Post(
                cpr::Url{sse_address_ + path},
                cpr::Header{{"Content-Type", "application/json"}},
                cpr::Body(body.dump()),
                cpr::Timeout{500});
        if (r.error.code != cpr::ErrorCode::OK) {
            if (!silent) {
                logPrint("Error: {} - {}\n", static_cast<int>(r.error.code), r.error.message);
            }
            return Status::TEMPORARY;
        }

        if (r.status_code == 200) {
            return Status::OK;
        }

        // Error case
        if (silent) {
            // If we set to silent, we expected an error, so ignore it
            return Status::OK;
        }

        // Log the error and delay the next request
        logPrint("Url: {}\n", (sse_address_ + path));
        logPrint("   Status code: {} - {}\n", r.status_code, r.status_line);
        logPrint("   Content-type: {}\n", r.header["content-type"]);
        logPrint("   Body: {} \n", r.text);

        try {
            nlohmann::json response_body = nlohmann::json::parse(r.text);
            if (response_body.contains("error") && (response_body["error"] == kErrorTooManyRegistration)) {
                return Status::TEMPORARY;
            }
        } catch (const nlohmann::json::exception &e) {
            logPrint("JSON exception: {}\n", e.what());
        }
        return Status::TEMPORARY;  // PERMANENT;
    }

    Status init() {
        remove();

        nlohmann::json metadata = {
                {"game", kSseAppId},
                {"game_display_name", kSseDisplayName},
                {"icon_color_id", 6},
        };
        Status res = sendRequest("/game_metadata", metadata);
        if (res != Status::OK) {
            return res;
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

    Status remove() {
        nlohmann::json metadata = {{"game", kSseAppId}};
        return sendRequest("/remove_game", metadata, true);
    }

    Status send_event() {
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

    template <auto T, typename... Args>
    Status checked(Args &&...args) {
        try {
            return (this->*T)(std::forward<Args>(args)...);
        } catch (const std::exception &e) {
            logPrint("Exception {}: {}\n", typeid(e).name(), e.what());
            return Status::TEMPORARY;  // PERMANENT;
        } catch (...) {
            logPrint("Unknown exception\n");
            return Status::TEMPORARY;  // PERMANENT;
        }
    }

 private:
    std::string sse_address_{};
    std::string prev_address_{};
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static bool quit = false;

void signalHandler(int /*signum*/) {
    quit = true;
}

void terminateHandler() {
    logPrint("** Application terminating **\n");
    std::exception_ptr ptr = std::current_exception();
    try {
        std::rethrow_exception(ptr);
    } catch (const std::exception &e) {
        logPrint("Exception {}: {}\n", typeid(e).name(), e.what());
    } catch (...) {
        logPrint("Unknown exception\n");
    }
    quit = true;
}

BOOL ctrlHandler(DWORD fdwCtrlType) {
    switch (fdwCtrlType) {
            // Handle the CTRL-C signal.
        case CTRL_C_EVENT:
            logPrint("Ctrl-C received\n");
            quit = true;
            return TRUE;

            // CTRL-CLOSE: confirm that the user wants to exit.
        case CTRL_CLOSE_EVENT:
            logPrint("Ctrl-Close received\n");
            quit = true;
            return TRUE;

            // Pass other signals to the next handler.
        case CTRL_BREAK_EVENT:
            logPrint("Ctrl-break received\n");
            return FALSE;

        case CTRL_LOGOFF_EVENT:
            logPrint("Ctrl-logoff received\n");
            return FALSE;

        case CTRL_SHUTDOWN_EVENT:
            logPrint("Ctrl-shutdown received\n");
            return FALSE;

        default:
            return FALSE;
    }
}

int main(int /*argc*/, char * /*argv*/[]) {
    std::set_terminate([]() {
        terminateHandler();
    });

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    logPrint("=== Application start ===\n");

    // if (AttachConsole(ATTACH_PARENT_PROCESS) == TRUE) {
    //     logPrint("Attaching to parent console\n");
    //     // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    //     std::freopen("CON", "r", stdin);
    //     // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    //     std::freopen("CON", "w", stdout);
    //     // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    //     std::freopen("CON", "w", stderr);

    //     if (SetConsoleCtrlHandler(ctrlHandler, TRUE) == 0) {
    //         logPrint("Failed to attach Ctrl handler\n");
    //         fmt::print("Failed to attach Ctrl handler\n");
    //     }
    // } else {
    //     logPrint("No parent console\n");
    // }
    // Hide Windows' console
    ShowWindow(GetConsoleWindow(), 0);

    fmt::print("Logging to {}\n", tmpPath);

    SseClock clock;
    clock.checkAddress();

    enum {
        Registering,  // Need to connect to SSE next
        Updating,     // Need to update the SSE event
        Waiting,      // Waiting for the next update
        Delaying,     // Something went wrong but we need to wait before retrying
        Stopping,
    } state{Registering};
    std::chrono::steady_clock::time_point next_request{};
    std::chrono::seconds delay{};
    while ((!quit) && (state != Stopping)) {
        switch (state) {
            case Registering:
                switch (clock.checked<&SseClock::init>()) {
                    case SseClock::Status::OK:
                        // Don't reset the delay just yet. Wait until we have
                        // at least on successful update so that we don't flood
                        // the logs if the registration always succeeds but the
                        // update always fails.
                        state = Updating;
                        logPrint("Registration complete\n");
                        break;
                    case SseClock::Status::TEMPORARY:
                        delay = std::min(std::max(std::chrono::seconds{1}, delay * 2), kMaxDelay);
                        state = Delaying;
                        break;
                        // case SseClock::Status::PERMANENT:
                        //     state = Stopping;
                        //     break;
                }
                break;
            case Updating:
                switch (clock.checked<&SseClock::send_event>()) {
                    case SseClock::Status::OK:
                        delay = {};
                        state = Waiting;
                        break;
                    case SseClock::Status::TEMPORARY:
                        // case SseClock::Status::PERMANENT:
                        state = Registering;
                        break;
                }
                break;
            case Waiting: {
                auto wait = std::chrono::ceil<std::chrono::seconds>(std::chrono::steady_clock::now());
                std::this_thread::sleep_until(wait);
                state = Updating;
                break;
            }
            case Delaying: {
                if (next_request < std::chrono::steady_clock::now()) {
                    next_request = std::chrono::steady_clock::now() + delay;
                    logPrint("Delaying by {:%Mm%Ss} (@{:%H:%M:%S})\n", delay, std::chrono::system_clock::now() + delay);
                }

                // Sleep only for a short time so that
                // 1. we can check if the situation with SSE changed (i.e. was restarted)
                // 2. check if we were requested to quit
                auto wait = std::chrono::ceil<std::chrono::seconds>(std::chrono::steady_clock::now());
                std::this_thread::sleep_until(wait);

                if (clock.checkAddress()) {
                    // The address changed, SSE was restarted so reset the delay
                    delay = {};
                    state = Registering;
                } else {
                    state = ((next_request <= std::chrono::steady_clock::now())
                                    ? Registering
                                    : Delaying);
                }

                break;
            }
            case Stopping:
                // Shouldn't be here
                logPrint("Unexpected state\n");
                std::terminate();
                break;
        }
    }
    clock.remove();

    logPrint("=== Application shutting down ===\n");

    return 0;
}
