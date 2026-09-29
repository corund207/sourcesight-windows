
#include "LogHelper.hpp"

#define ADD_COLOR_TO_STREAM(color) "\x1b[" << int(color) << "m"
#define RESET_STREAM_COLOR "\x1b[0m"
#define HEX(value) "0x" << std::hex << std::uppercase << DWORD64(value) << std::dec << std::nouppercase

bool LogHelper::Init()
{
    return GetInstance().InitImpl();
}

void LogHelper::Destroy()
{
    Logger::FlushQueue();
    Logger::Destroy();
}

void LogHelper::Free() {
	if (HWND console = GetConsoleWindow()) {
        FreeConsole();
        PostMessage(console, WM_CLOSE, 0, 0);
	}
}

bool LogHelper::InitImpl() {
    Logger::Init();

    if (auto handle = GetStdHandle(STD_OUTPUT_HANDLE); handle != nullptr)
    {
        SetConsoleTitleA(__DATE__);
        SetConsoleOutputCP(CP_UTF8);

        DWORD consoleMode=0;
        GetConsoleMode(handle, &consoleMode);

        // terminal like behaviour enable full color support
        consoleMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
        consoleMode &= ~(ENABLE_QUICK_EDIT_MODE);

        SetConsoleMode(handle, consoleMode);
    }

    m_ConsoleOut.open("CONOUT$", std::ios_base::out | std::ios_base::app);
    m_FileOut.open("sourcesight.log", std::ios_base::out | std::ios_base::trunc);

    Logger::AddSink([this](LogMessagePtr msg) {
#ifndef _DEBUG
        if (msg->Level() == eLogLevel::VERBOSE)
            return;
#endif
        std::string formatted = this->FormatConsole(msg);

        m_ConsoleOut << formatted;
        m_ConsoleOut.flush();

        if (m_FileOut.is_open()) {
            // strip ANSI color codes for the log file
            for (size_t i = 0; i < formatted.size(); ++i) {
                if (formatted[i] == '\x1b') {
                    while (i < formatted.size() && formatted[i] != 'm') ++i;
                    continue;
                }
                m_FileOut.put(formatted[i]);
            }
            m_FileOut.flush();
        }
    });

    return true;
}

std::string LogHelper::FormatConsole(const LogMessagePtr msg) {
        std::stringstream out;

#ifdef _DEBUG
        const auto timestamp = std::format("{0:%H:%M:%S}", msg->Timestamp());
#else
        const auto timestamp = std::format("{0:%H:%M:%S}", std::chrono::floor<std::chrono::seconds>(msg->Timestamp()));
#endif
		const auto& location = msg->Location();
		const auto level     = msg->Level();
		const auto color     = GetColor(level);

		const auto file = std::filesystem::path(location.file_name()).filename().string();

#ifdef _DEBUG
		out << ADD_COLOR_TO_STREAM(LogColor::GRAY) << "[" << timestamp << "]" << ADD_COLOR_TO_STREAM(color) << "[" << GetLevelStr(level) << "/" << file << ":"
		    << location.line() << "] " << RESET_STREAM_COLOR << msg->Message();
#else
        out << ADD_COLOR_TO_STREAM(LogColor::GRAY) << "[" << timestamp << "]" << ADD_COLOR_TO_STREAM(color) << "[" << GetLevelStr(level) << "] " << RESET_STREAM_COLOR << msg->Message();
#endif

		return out.str();
}

LogColor LogHelper::GetColor(const eLogLevel level)
{
    switch (level)
    {
    case eLogLevel::VERBOSE: return LogColor::BLUE;
    case eLogLevel::INFO: return LogColor::GREEN;
    case eLogLevel::WARNING: return LogColor::YELLOW;
    case eLogLevel::FATAL: return LogColor::RED;
    }
    return LogColor::WHITE;
}

const char* LogHelper::GetLevelStr(const eLogLevel level) {
    constexpr std::array<const char*, 4> levelStrings = {{"DBG", "INF", "WRN", "ERR"}};

    return levelStrings[level];
}
