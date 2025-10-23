#define NOMINMAX  // Must be before windows.h
#include <windows.h>
#include <winhttp.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <ctime>
#include <regex>
#include <atomic>
#include <csignal>
#include <sstream>
#include <memory>
#include <algorithm>

#pragma comment(lib, "winhttp.lib")

// ============================================================================
// Configuration
// ============================================================================
struct Config {
    std::wstring targetUrl;
    int pollIntervalSeconds;
    int minIntervalSeconds;
    int maxIntervalSeconds;
    std::string logFile;
    std::string configFile;
    bool ignoreSSLErrors;
    bool verboseLogging;

    Config() :
        pollIntervalSeconds(6),
        minIntervalSeconds(5),
        maxIntervalSeconds(300),
        logFile("activity_log.txt"),
        configFile("config.ini"),
        ignoreSSLErrors(false),
        verboseLogging(false) {}
};

// ============================================================================
// Log Levels
// ============================================================================
enum class LogLevel {
    LOG_ERROR,
    LOG_WARNING,
    LOG_INFO,
    LOG_DEBUG
};

Config g_config;
std::atomic<bool> g_running(true);
LogLevel g_logLevel = LogLevel::LOG_INFO;

// ============================================================================
// RAII Handle Wrapper
// ============================================================================
class WinHttpHandle {
private:
    HINTERNET handle;

public:
    explicit WinHttpHandle(HINTERNET h = nullptr) : handle(h) {}

    ~WinHttpHandle() {
        if (handle) {
            WinHttpCloseHandle(handle);
            handle = nullptr;
        }
    }

    operator HINTERNET() const { return handle; }
    HINTERNET get() const { return handle; }
    bool isValid() const { return handle != nullptr; }

    HINTERNET release() {
        HINTERNET temp = handle;
        handle = nullptr;
        return temp;
    }

    // Prevent copying
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    // Allow moving
    WinHttpHandle(WinHttpHandle&& other) noexcept : handle(other.handle) {
        other.handle = nullptr;
    }

    WinHttpHandle& operator=(WinHttpHandle&& other) noexcept {
        if (this != &other) {
            if (handle) WinHttpCloseHandle(handle);
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }
};

// ============================================================================
// Console Handler
// ============================================================================
BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT || signal == CTRL_BREAK_EVENT) {
        g_running = false;
        std::cout << "\n[INFO] Shutting down gracefully...\n";
        return TRUE;
    }
    return FALSE;
}

// ============================================================================
// Utility Functions
// ============================================================================
std::string timestamp_now() {
    std::time_t now = std::time(nullptr);
    std::tm tm_now;
    localtime_s(&tm_now, &now);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_now);
    return std::string(buf);
}

void logMessage(LogLevel level, const std::string& message) {
    if (level > g_logLevel) return;

    std::string prefix;
    switch (level) {
    case LogLevel::LOG_ERROR:   prefix = "[ERROR] "; break;
    case LogLevel::LOG_WARNING: prefix = "[WARN]  "; break;
    case LogLevel::LOG_INFO:    prefix = "[INFO]  "; break;
    case LogLevel::LOG_DEBUG:   prefix = "[DEBUG] "; break;
    }

    std::cout << "[" << timestamp_now() << "] " << prefix << message << "\n";
}

std::wstring stringToWString(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size);
    // Remove null terminator
    if (!wstr.empty() && wstr.back() == L'\0') {
        wstr.pop_back();
    }
    return wstr;
}

std::string wstringToString(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size, nullptr, nullptr);
    // Remove null terminator
    if (!str.empty() && str.back() == '\0') {
        str.pop_back();
    }
    return str;
}

// ============================================================================
// Configuration Management
// ============================================================================
bool loadConfig(Config& config) {
    std::ifstream file(config.configFile);
    if (!file.is_open()) {
        logMessage(LogLevel::LOG_WARNING, "Config file not found. Using defaults.");
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Remove comments and whitespace
        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }

        size_t equalPos = line.find('=');
        if (equalPos == std::string::npos) continue;

        std::string key = line.substr(0, equalPos);
        std::string value = line.substr(equalPos + 1);

        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t\r\n"));
        key.erase(key.find_last_not_of(" \t\r\n") + 1);
        value.erase(0, value.find_first_not_of(" \t\r\n"));
        value.erase(value.find_last_not_of(" \t\r\n") + 1);

        if (key == "url") {
            config.targetUrl = stringToWString(value);
        }
        else if (key == "poll_interval") {
            config.pollIntervalSeconds = std::stoi(value);
        }
        else if (key == "log_file") {
            config.logFile = value;
        }
        else if (key == "ignore_ssl_errors") {
            config.ignoreSSLErrors = (value == "true" || value == "1");
        }
        else if (key == "verbose") {
            config.verboseLogging = (value == "true" || value == "1");
        }
    }

    file.close();
    return true;
}

bool saveDefaultConfig(const Config& config) {
    std::ofstream file(config.configFile);
    if (!file.is_open()) {
        logMessage(LogLevel::LOG_ERROR, "Failed to create config file");
        return false;
    }

    file << "# Activity Watcher Configuration\n";
    file << "# URL to monitor\n";
    file << "url=https://forums.playdeadlock.com/members/yoshi.1/\n\n";
    file << "# Poll interval in seconds (minimum 5)\n";
    file << "poll_interval=10\n\n";
    file << "# Log file path\n";
    file << "log_file=activity_log.txt\n\n";
    file << "# Ignore SSL certificate errors (SECURITY RISK - use only for testing)\n";
    file << "ignore_ssl_errors=false\n\n";
    file << "# Enable verbose logging\n";
    file << "verbose=false\n";

    file.close();
    logMessage(LogLevel::LOG_INFO, "Created default config file: " + config.configFile);
    return true;
}

// ============================================================================
// HTTP Functions
// ============================================================================
std::string fetchPage(HINTERNET hSession, const URL_COMPONENTS& urlCompIn,
    const std::wstring& hostName, const std::wstring& urlPath) {
    std::string result;
    if (!hSession) return result;

    WinHttpHandle hConnect(WinHttpConnect(hSession, hostName.c_str(), urlCompIn.nPort, 0));
    if (!hConnect.isValid()) {
        logMessage(LogLevel::LOG_ERROR, "WinHttpConnect failed: " + std::to_string(GetLastError()));
        return result;
    }

    DWORD flags = (urlCompIn.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;

    WinHttpHandle hRequest(WinHttpOpenRequest(hConnect.get(), L"GET", urlPath.c_str(),
        NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags));

    if (!hRequest.isValid()) {
        logMessage(LogLevel::LOG_ERROR, "WinHttpOpenRequest failed: " + std::to_string(GetLastError()));
        return result;
    }

    // Add realistic User-Agent
    std::wstring ua = L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\n";
    WinHttpAddRequestHeaders(hRequest.get(), ua.c_str(), (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);

    // SECURITY: Only ignore SSL errors if explicitly configured (NOT RECOMMENDED)
    if (g_config.ignoreSSLErrors && urlCompIn.nScheme == INTERNET_SCHEME_HTTPS) {
        static bool sslWarningShown = false;
        if (!sslWarningShown) {
            logMessage(LogLevel::LOG_WARNING, "SSL certificate validation is DISABLED - This is a security risk!");
            sslWarningShown = true;
        }
        DWORD dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
            SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
            SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
            SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hRequest.get(), WINHTTP_OPTION_SECURITY_FLAGS, &dwFlags, sizeof(dwFlags));
    }

    BOOL ok = WinHttpSendRequest(hRequest.get(),
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0,
        0, 0) &&
        WinHttpReceiveResponse(hRequest.get(), NULL);

    if (!ok) {
        logMessage(LogLevel::LOG_ERROR, "Request/Receive failed: " + std::to_string(GetLastError()));
        return result;
    }

    // Check HTTP status code
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    if (WinHttpQueryHeaders(hRequest.get(),
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        NULL, &statusCode, &statusCodeSize, NULL)) {

        if (statusCode != 200) {
            logMessage(LogLevel::LOG_WARNING, "HTTP Status: " + std::to_string(statusCode));
            if (statusCode >= 400) {
                return result;  // Don't process error pages
            }
        }
        else {
            logMessage(LogLevel::LOG_DEBUG, "HTTP 200 OK");
        }
    }

    // Read response data
    DWORD bytesAvailable = 0;
    do {
        if (!WinHttpQueryDataAvailable(hRequest.get(), &bytesAvailable)) break;
        if (bytesAvailable == 0) break;

        std::vector<char> buffer(bytesAvailable + 1);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest.get(), buffer.data(), bytesAvailable, &bytesRead) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            result.append(buffer.data(), bytesRead);
        }
        else {
            break;
        }
    } while (bytesAvailable > 0);

    logMessage(LogLevel::LOG_DEBUG, "Fetched " + std::to_string(result.size()) + " bytes");
    return result;
}

// ============================================================================
// HTML Parsing
// ============================================================================
std::string decodeHtmlEntities(const std::string& text) {



    std::string result = text;
    struct Entity { const char* code; const char* value; };
    Entity entities[] = {
        {"&middot;", "·"},
        {"&amp;", "&"},
        {"&lt;", "<"},
        {"&gt;", ">"},
        {"&quot;", "\""},
        {"&apos;", "'"}
        // أضف أي entity أخرى تحتاجها
    };

    for (auto& e : entities) {
        size_t pos = 0;
        while ((pos = result.find(e.code, pos)) != std::string::npos) {
            result.replace(pos, strlen(e.code), e.value);
            pos += strlen(e.value);
        }
    }
    return result;
}


std::string extractActivity(const std::string& html) {
    std::string dtTag = "<dt>Last seen</dt>";
    size_t dtPos = html.find(dtTag);
    if (dtPos == std::string::npos) return "";

    size_t ddStart = html.find("<dd", dtPos);
    if (ddStart == std::string::npos) return "";

    size_t closeTag = html.find('>', ddStart);
    size_t ddEnd = html.find("</dd>", closeTag);
    if (closeTag == std::string::npos || ddEnd == std::string::npos) return "";

    std::string content = html.substr(closeTag + 1, ddEnd - closeTag - 1);

    // Remove all HTML tags but keep the text content
    static const std::regex tag_re("<[^>]*>");
    std::string clean = std::regex_replace(content, tag_re, "");

    // Replace HTML entities with their actual characters
    clean = decodeHtmlEntities(clean);

    // Replace the middle dot with a space and clean up
    size_t dotPos = clean.find("·");
    if (dotPos != std::string::npos) {
        clean.replace(dotPos, 2, " "); // Replace "·" with space
    }

    // Remove extra whitespace
    auto l = clean.find_first_not_of(" \t\r\n");
    auto r = clean.find_last_not_of(" \t\r\n");
    if (l == std::string::npos) return "";
    clean = clean.substr(l, r - l + 1);

    // Clean up multiple spaces
    std::string finalResult;
    bool lastWasSpace = false;
    for (char c : clean) {
        if (c == ' ' || c == '\t') {
            if (!lastWasSpace) {
                finalResult += ' ';
                lastWasSpace = true;
            }
        }
        else {
            finalResult += c;
            lastWasSpace = false;
        }
    }

    return finalResult;
}

// ============================================================================
// Logging
// ============================================================================
bool logActivity(const std::string& activity) {
    std::string ts = timestamp_now();
    std::ofstream file(g_config.logFile, std::ios::app);
    if (!file.is_open()) {
        logMessage(LogLevel::LOG_ERROR, "Failed to open log file: " + g_config.logFile);
        return false;
    }

    file << "[" << ts << "] " << activity << std::endl;
    file.close();
    return true;
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char* argv[]) {

    // Set up console handler for graceful shutdown
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    std::cout << "========================================\n";
    std::cout << "  Activity Watcher v2.0\n";
    std::cout << "========================================\n\n";

    // Load or create configuration
    if (!loadConfig(g_config)) {
        saveDefaultConfig(g_config);
        loadConfig(g_config);
    }

    // Command line argument overrides config
    if (argc > 1) {
        g_config.targetUrl = stringToWString(argv[1]);
        logMessage(LogLevel::LOG_INFO, "Using URL from command line");
    }

    // Set log level
    if (g_config.verboseLogging) {
        g_logLevel = LogLevel::LOG_DEBUG;
    }

    // Validate configuration
    if (g_config.targetUrl.empty()) {
        logMessage(LogLevel::LOG_ERROR, "No URL specified. Please configure url in " + g_config.configFile);
        return 1;
    }

    if (g_config.pollIntervalSeconds < g_config.minIntervalSeconds) {
        std::ostringstream oss;
        oss << "Poll interval too low, setting to minimum: " << g_config.minIntervalSeconds << "s";
        logMessage(LogLevel::LOG_WARNING, oss.str());
        g_config.pollIntervalSeconds = g_config.minIntervalSeconds;
    }

    // Parse URL
    URL_COMPONENTS urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    std::wstring hostName(256, L'\0');
    std::wstring urlPath(1024, L'\0');

    urlComp.lpszHostName = &hostName[0];
    urlComp.dwHostNameLength = static_cast<DWORD>(hostName.size());
    urlComp.lpszUrlPath = &urlPath[0];
    urlComp.dwUrlPathLength = static_cast<DWORD>(urlPath.size());

    if (!WinHttpCrackUrl(g_config.targetUrl.c_str(),
        static_cast<DWORD>(g_config.targetUrl.size()), 0, &urlComp)) {
        logMessage(LogLevel::LOG_ERROR, "Failed to parse URL: " + std::to_string(GetLastError()));
        return 1;
    }

    hostName.resize(urlComp.dwHostNameLength);
    urlPath.resize(urlComp.dwUrlPathLength);

    // Open HTTP session
    WinHttpHandle hSession(WinHttpOpen(L"ActivityWatcher/2.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0));

    if (!hSession.isValid()) {
        logMessage(LogLevel::LOG_ERROR, "WinHttpOpen failed: " + std::to_string(GetLastError()));
        return 1;
    }

    // Set timeouts (in milliseconds)
    int timeout = 30000;  // 30 seconds
    WinHttpSetOption(hSession.get(), WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession.get(), WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    std::string lastActivity;
    std::string urlStr = wstringToString(g_config.targetUrl);

    std::ostringstream oss;
    oss << "Monitoring: " << urlStr;
    logMessage(LogLevel::LOG_INFO, oss.str());

    oss.str("");
    oss << "Poll interval: " << g_config.pollIntervalSeconds << "s";
    logMessage(LogLevel::LOG_INFO, oss.str());

    logMessage(LogLevel::LOG_INFO, "Log file: " + g_config.logFile);
    logMessage(LogLevel::LOG_INFO, "Press Ctrl+C to stop\n");

    // Main monitoring loop
    std::chrono::seconds interval(g_config.pollIntervalSeconds);
    int consecutiveFailures = 0;
    const int maxFailures = 10;

    while (g_running) {
        std::string html = fetchPage(hSession.get(), urlComp, hostName, urlPath);

        if (html.empty()) {
            ++consecutiveFailures;

            if (consecutiveFailures >= maxFailures) {
                logMessage(LogLevel::LOG_ERROR, "Too many consecutive failures. Stopping.");
                break;
            }

            // Exponential backoff on failures
            int multiplier = 1 << std::min(consecutiveFailures, 6);
            int nextSeconds = std::min(
                static_cast<int>(interval.count()) * multiplier,
                g_config.maxIntervalSeconds
            );
            std::chrono::seconds backoff(nextSeconds);

            std::ostringstream oss2;
            oss2 << "Failed to fetch page (attempt " << consecutiveFailures
                << "/" << maxFailures << "). Retrying in " << backoff.count() << "s";
            logMessage(LogLevel::LOG_WARNING, oss2.str());

            // Sleep with early exit capability
            for (int i = 0; i < backoff.count() && g_running; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            continue;
        }

        // Reset failure counter on success
        if (consecutiveFailures > 0) {
            logMessage(LogLevel::LOG_INFO, "Connection restored");
            consecutiveFailures = 0;
        }

        std::string currentActivity = extractActivity(html);

        if (!currentActivity.empty() && currentActivity != lastActivity) {
            lastActivity = currentActivity;

            if (logActivity(currentActivity)) {
                logMessage(LogLevel::LOG_INFO, "Activity updated: " + currentActivity);
            }
        }
        else if (!currentActivity.empty()) {
            logMessage(LogLevel::LOG_DEBUG, "No change in activity");
        }

        // Sleep with early exit capability
        for (int i = 0; i < interval.count() && g_running; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    logMessage(LogLevel::LOG_INFO, "Shutdown complete");
    return 0;
}