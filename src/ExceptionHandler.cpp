#include <windows.h>
#include <DbgHelp.h>
#include <ShlObj.h>
#include <filesystem>
#include <spdlog/spdlog.h>

#include <utility/Module.hpp>
#include <utility/Scan.hpp>
#include <utility/Patch.hpp>

#include "Framework.hpp"

#include "ExceptionHandler.hpp"

LONG WINAPI framework::global_exception_handler(struct _EXCEPTION_POINTERS* ei) {
    spdlog::flush_on(spdlog::level::err);

    spdlog::error("Exception occurred: {:x}", ei->ExceptionRecord->ExceptionCode);
    spdlog::error("RIP: {:x}", ei->ContextRecord->Rip);
    spdlog::error("RSP: {:x}", ei->ContextRecord->Rsp);
    spdlog::error("RCX: {:x}", ei->ContextRecord->Rcx);
    spdlog::error("RDX: {:x}", ei->ContextRecord->Rdx);
    spdlog::error("R8: {:x}", ei->ContextRecord->R8);
    spdlog::error("R9: {:x}", ei->ContextRecord->R9);
    spdlog::error("R10: {:x}", ei->ContextRecord->R10);
    spdlog::error("R11: {:x}", ei->ContextRecord->R11);
    spdlog::error("R12: {:x}", ei->ContextRecord->R12);
    spdlog::error("R13: {:x}", ei->ContextRecord->R13);
    spdlog::error("R14: {:x}", ei->ContextRecord->R14);
    spdlog::error("R15: {:x}", ei->ContextRecord->R15);
    spdlog::error("RAX: {:x}", ei->ContextRecord->Rax);
    spdlog::error("RBX: {:x}", ei->ContextRecord->Rbx);
    spdlog::error("RBP: {:x}", ei->ContextRecord->Rbp);
    spdlog::error("RSI: {:x}", ei->ContextRecord->Rsi);
    spdlog::error("RDI: {:x}", ei->ContextRecord->Rdi);
    spdlog::error("EFLAGS: {:x}", ei->ContextRecord->EFlags);
    spdlog::error("CS: {:x}", ei->ContextRecord->SegCs);
    spdlog::error("DS: {:x}", ei->ContextRecord->SegDs);
    spdlog::error("ES: {:x}", ei->ContextRecord->SegEs);
    spdlog::error("FS: {:x}", ei->ContextRecord->SegFs);
    spdlog::error("GS: {:x}", ei->ContextRecord->SegGs);
    spdlog::error("SS: {:x}", ei->ContextRecord->SegSs);

    const auto module_within = utility::get_module_within(ei->ContextRecord->Rip);

    if (module_within) {
        const auto module_path = utility::get_module_path(*module_within);

        if (module_path) {
            spdlog::error("Module: {:x} {}", (uintptr_t)*module_within, *module_path);
        } else {
            spdlog::error("Module: Unknown");
        }
    } else {
        spdlog::error("Module: Unknown");
    }

    auto dbghelp = LoadLibrary("dbghelp.dll");

    if (dbghelp) {
        const auto final_path = Framework::get_persistent_dir("crash.dmp").string();

        spdlog::error("Attempting to write dump to {}", final_path);

        auto f = CreateFile(final_path.c_str(), 
            GENERIC_WRITE, 
            FILE_SHARE_WRITE, 
            nullptr, 
            CREATE_ALWAYS, 
            FILE_ATTRIBUTE_NORMAL, 
            nullptr
        );

        if (!f || f == INVALID_HANDLE_VALUE) {  
            spdlog::error("Exception occurred, but could not create dump file");
            return EXCEPTION_CONTINUE_SEARCH;
        }

        MINIDUMP_EXCEPTION_INFORMATION ei_info{
            GetCurrentThreadId(),
            ei,
            FALSE
        };

        auto minidump_write_dump = (decltype(MiniDumpWriteDump)*)GetProcAddress(dbghelp, "MiniDumpWriteDump");

        minidump_write_dump(GetCurrentProcess(), 
            GetCurrentProcessId(),
            f,
            MINIDUMP_TYPE::MiniDumpNormal, 
            &ei_info, 
            nullptr, 
            nullptr
        );

        CloseHandle(f);
    } else {
        spdlog::error("Exception occurred, but could not load dbghelp.dll");
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

// First-chance vectored handler that logs and dumps BEFORE the game's own
// crashpad/SEH machinery sees the exception. Helps diagnose crashes that
// don't make it through SetUnhandledExceptionFilter (e.g. CrashpadHandler
// catches them first, or the game has its own __try/__except wrapper).
LONG WINAPI framework::vectored_exception_handler(struct _EXCEPTION_POINTERS* ei) {
    const auto code = ei->ExceptionRecord->ExceptionCode;

    // Only catch fatal / interesting exceptions, not the swarm of routine
    // C++ exceptions and DLL load notifications.
    const bool is_fatal =
        code == EXCEPTION_ACCESS_VIOLATION ||
        code == EXCEPTION_ILLEGAL_INSTRUCTION ||
        code == EXCEPTION_PRIV_INSTRUCTION ||
        code == EXCEPTION_STACK_OVERFLOW ||
        code == EXCEPTION_INVALID_HANDLE ||
        code == EXCEPTION_NONCONTINUABLE_EXCEPTION ||
        code == 0xC0000420 || // STATUS_ASSERTION_FAILURE
        code == 0xC0000005;   // duplicate of EXCEPTION_ACCESS_VIOLATION but be explicit

    if (!is_fatal) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Skip exceptions that originate inside KERNEL32/NTDLL — those are
    // typically internal probe-and-recover exceptions handled by SEH chains
    // (e.g. RtlpUnhandledExceptionFilter) and aren't the real fatal crash.
    {
        const auto mod_within_p = utility::get_module_within(ei->ContextRecord->Rip);
        if (mod_within_p) {
            const auto mp = utility::get_module_path(*mod_within_p);
            if (mp) {
                std::string lower = *mp;
                for (auto& c : lower) c = (char)::tolower((unsigned char)c);
                if (lower.find("\\kernel32.dll") != std::string::npos ||
                    lower.find("\\kernelbase.dll") != std::string::npos ||
                    lower.find("\\ntdll.dll") != std::string::npos ||
                    lower.find("\\msvcr") != std::string::npos ||
                    lower.find("\\ucrtbase.dll") != std::string::npos)
                {
                    return EXCEPTION_CONTINUE_SEARCH;
                }
            }
        }
    }

    static std::atomic<int> dump_count{0};
    const int n_dumps = dump_count.fetch_add(1, std::memory_order_relaxed);
    // Log up to 8 fatal exceptions; only the first one writes a minidump.
    if (n_dumps >= 8) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    spdlog::flush_on(spdlog::level::err);
    spdlog::error("[VEH] FIRST-CHANCE FATAL EXCEPTION code=0x{:x}", code);
    spdlog::error("[VEH] RIP=0x{:x} RSP=0x{:x}", ei->ContextRecord->Rip, ei->ContextRecord->Rsp);
    spdlog::error("[VEH] RAX=0x{:x} RBX=0x{:x} RCX=0x{:x} RDX=0x{:x}",
        ei->ContextRecord->Rax, ei->ContextRecord->Rbx, ei->ContextRecord->Rcx, ei->ContextRecord->Rdx);
    spdlog::error("[VEH] R8=0x{:x} R9=0x{:x} R10=0x{:x} R11=0x{:x}",
        ei->ContextRecord->R8, ei->ContextRecord->R9, ei->ContextRecord->R10, ei->ContextRecord->R11);
    spdlog::error("[VEH] R12=0x{:x} R13=0x{:x} R14=0x{:x} R15=0x{:x}",
        ei->ContextRecord->R12, ei->ContextRecord->R13, ei->ContextRecord->R14, ei->ContextRecord->R15);
    spdlog::error("[VEH] RBP=0x{:x} RSI=0x{:x} RDI=0x{:x}",
        ei->ContextRecord->Rbp, ei->ContextRecord->Rsi, ei->ContextRecord->Rdi);

    if (code == EXCEPTION_ACCESS_VIOLATION || code == 0xC0000005) {
        const auto av_type = ei->ExceptionRecord->ExceptionInformation[0];
        const auto av_addr = ei->ExceptionRecord->ExceptionInformation[1];
        spdlog::error("[VEH] AccessViolation type={} (0=read, 1=write, 8=DEP) at addr=0x{:x}",
            av_type, av_addr);
    }

    // Identify which module RIP is in
    const auto module_within = utility::get_module_within(ei->ContextRecord->Rip);
    if (module_within) {
        const auto module_path = utility::get_module_path(*module_within);
        const auto offset = ei->ContextRecord->Rip - (uintptr_t)*module_within;
        spdlog::error("[VEH] RIP module: {:x} {} +0x{:x}",
            (uintptr_t)*module_within,
            module_path ? *module_path : std::string{"<unknown>"},
            offset);
    }

    // Stack walk: read the first 32 stack qwords and try to identify which
    // are in executable modules (= likely return addresses).
    spdlog::error("[VEH] Stack walk from RSP:");
    auto rsp = (const uintptr_t*)ei->ContextRecord->Rsp;
    for (int i = 0; i < 32 && !IsBadReadPtr(rsp + i, sizeof(uintptr_t)); ++i) {
        const auto val = rsp[i];
        const auto mod = utility::get_module_within(val);
        if (mod) {
            const auto mod_path = utility::get_module_path(*mod);
            const auto off = val - (uintptr_t)*mod;
            spdlog::error("[VEH]   [rsp+{:#x}] 0x{:x}  -> {} +0x{:x}",
                i * 8, val,
                mod_path ? *mod_path : std::string{"<unknown>"},
                off);
        } else {
            spdlog::error("[VEH]   [rsp+{:#x}] 0x{:x}", i * 8, val);
        }
    }

    // Only write a minidump for the FIRST captured exception (writing a
    // full dump is slow and we don't want to corrupt the first useful one).
    if (n_dumps != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Write minidump
    auto dbghelp = LoadLibrary("dbghelp.dll");
    if (dbghelp) {
        const auto final_path = Framework::get_persistent_dir("crash_veh.dmp").string();
        spdlog::error("[VEH] Writing dump to {}", final_path);

        auto f = CreateFile(final_path.c_str(),
            GENERIC_WRITE, FILE_SHARE_WRITE, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (f && f != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION ei_info{
                GetCurrentThreadId(), ei, FALSE
            };
            auto write_dump = (decltype(MiniDumpWriteDump)*)GetProcAddress(dbghelp, "MiniDumpWriteDump");
            if (write_dump) {
                write_dump(GetCurrentProcess(),
                    GetCurrentProcessId(), f,
                    (MINIDUMP_TYPE)(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithDataSegs),
                    &ei_info, nullptr, nullptr);
            }
            CloseHandle(f);
        }
    }

    return EXCEPTION_CONTINUE_SEARCH;  // Let normal SEH chain handle / kill process
}

void framework::setup_exception_handler() {
    SetUnhandledExceptionFilter(global_exception_handler);
    // VEH is gated behind UEVR_ENABLE_VEH_FIRST_CHANCE=1. Default off because
    // catching first-chance exceptions can disrupt games that rely on SEH for
    // probe-and-recover patterns (e.g. SN2's CrashpadHandler).
    wchar_t enable_value[16]{};
    const auto enable_len = GetEnvironmentVariableW(L"UEVR_ENABLE_VEH_FIRST_CHANCE", enable_value, (DWORD)std::size(enable_value));
    const bool enable_veh = enable_len > 0 && enable_len < std::size(enable_value) &&
        enable_value[0] != L'\0' && enable_value[0] != L'0' &&
        enable_value[0] != L'f' && enable_value[0] != L'F';
    if (enable_veh) {
        AddVectoredExceptionHandler(1, vectored_exception_handler);
    }
}
