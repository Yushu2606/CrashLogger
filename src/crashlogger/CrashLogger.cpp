#include "crashlogger/CrashLogger.h"

#include <consoleapi2.h>
#include <cstdio>
#include <iostream>
#include <string>

#include "cxxopts.hpp"
#include "spdlog/sinks/stdout_color_sinks.h"

#include "crashlogger/CxxOptAdder.h"
#include "crashlogger/Debugger.h"
#include "crashlogger/Logger.h"
#include "crashlogger/StringUtils.h"

#include <windows.h>

#include <psapi.h>
#include <tlhelp32.h>
#include <winnls.h>

DWORD GetParentProcessID() {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);
    if (!Process32First(hSnapshot, &pe)) {
        CloseHandle(hSnapshot);
        return 0;
    }
    DWORD pid  = GetCurrentProcessId();
    DWORD ppid = 0;
    do {
        if (pe.th32ProcessID == pid) {
            ppid = pe.th32ParentProcessID;
            break;
        }
    } while (Process32Next(hSnapshot, &pe));
    CloseHandle(hSnapshot);
    return ppid;
}

bool LegacyParseArgs(int argc, char** argv, int& pid) {
    using crashlogger::StringUtils::a2u8;
    if (argc != 2) {
        return false;
    }
    try {
        pid = std::stoi(argv[1]);
        if (pid <= 0) {
            return false;
        }
        return true;
    } catch (const std::invalid_argument&) {
        return false;
    }
}

void ModernParseArgs(int argc, char** argv, int& pid) {
    using crashlogger::StringUtils::a2u8;
    cxxopts::Options options("CrashLogger", "A crash logger for Minecraft Bedrock Server");
    options.allow_unrecognised_options();
    options.set_width(-1);
    CxxOptsAdder(options)
        .add("h,help", "Print this help message")
        .add("v,version", "Print version information")
        .add("s,silent", "Silent mode, no console output except for crash report and error messages")
        .add("p,pid", "The PID of the process to be attached", cxxopts::value<int>()->default_value("-1"));

    auto result = options.parse(argc, argv);
    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        exit(0);
    }
    if (result.count("version")) {
        std::cout << "CrashLogger " CRASHLOGGER_VERSION << std::endl;
        exit(0);
    }
    pid = result["pid"].as<int>();

    crashlogger::SilentMode = result["silent"].as<bool>();
}

int main(int argc, char** argv) {
    using crashlogger::Logger::pLogger;

    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    pLogger = spdlog::stdout_color_mt("CrashLogger");
    pLogger->set_pattern("%H:%M:%S.%e [%^%l%$] %v");

    if (argc == 1) {
        pLogger->error("Do not execute this process directly.");
        pLogger->error("You should pass a PID as a command line argument which is of the process to be attached.");
        pLogger->error("Press any key to exit...");
        getchar();
        return 1;
    }

    int  pid;
    bool legacy = LegacyParseArgs(argc, argv, pid);
    if (!legacy) {
        ModernParseArgs(argc, argv, pid);
    }

    if (pid <= 0) {
        pLogger->error("Invalid PID.");
        getchar();
        return 1;
    }

    if (GetParentProcessID() == pid || crashlogger::SilentMode) {
        crashlogger::SilentMode = true;
        pLogger->set_level(spdlog::level::err);
    }

    if (legacy) {
        pLogger->warn("Legacy argument parsing is deprecated. Please use the new way to specify arguments.");
    }

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (hProcess == nullptr) {
        pLogger->error("Failed to open the target process! Error Code: {}", GetLastError());
        return -1;
    }
    pLogger->info("CrashLogger has successfully attached to the process. PID: {}", pid);
    crashlogger::Debugger::DebuggerMain(hProcess);
    return 0;
}
