/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "CrashLog.h"

#if defined(__linux__)

#include <csignal>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <execinfo.h>
#include <fcntl.h>
#include <link.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

namespace
{
    constexpr int MAX_FRAMES = 128;

    int crashFd = STDERR_FILENO;
    uintptr_t loadBase = 0;
    char revision[256] = "";

    // Only the main thread has this: another thread that overflows its stack dies with no trace.
    // The main thread is the one the world loop runs on.
    alignas(16) char alternateStack[64 * 1024];

    // Everything from here to the end of OnCrash runs inside a signal handler, so no allocating,
    // no locks and no fmt or LOG_* calls.
    void WriteText(char const* text)
    {
        [[maybe_unused]] ssize_t written = write(crashFd, text, strlen(text));
    }

    void WriteNumber(uint64_t value, unsigned base)
    {
        char digits[24];
        char* end = digits + sizeof(digits) - 1;
        char* cursor = end;
        *cursor = '\0';

        do
        {
            *--cursor = "0123456789abcdef"[value % base];
            value /= base;
        } while (value);

        if (base == 16)
        {
            *--cursor = 'x';
            *--cursor = '0';
        }

        WriteText(cursor);
    }

    void OnCrash(int sig, siginfo_t* info, void* context)
    {
        WriteText("=== crash: signal ");
        WriteNumber(uint64_t(sig), 10);
        WriteText(" code ");
        WriteNumber(uint64_t(int64_t(info->si_code)), 10);
        WriteText(" address ");
        WriteNumber(uint64_t(uintptr_t(info->si_addr)), 16);
        WriteText(" thread ");
        WriteNumber(uint64_t(syscall(SYS_gettid)), 10);
        WriteText(" time ");
        WriteNumber(uint64_t(time(nullptr)), 10);
        WriteText("\nrevision: ");
        WriteText(revision);
        WriteText("\nbase ");
        WriteNumber(uint64_t(loadBase), 16);
#if defined(__x86_64__)
        ucontext_t const* registers = static_cast<ucontext_t const*>(context);
        WriteText(" rip ");
        WriteNumber(uint64_t(registers->uc_mcontext.gregs[REG_RIP]), 16);
        WriteText(" rsp ");
        WriteNumber(uint64_t(registers->uc_mcontext.gregs[REG_RSP]), 16);
        WriteText("\n");

        // Fetching the instruction itself faulted: a call through a bad pointer. backtrace() dies
        // trying to read the code at rip, and the return address on top of the stack is all that's
        // left of the caller. Read first, so a bad rsp can't leave half a line for the next record.
        if (uint64_t(uintptr_t(info->si_addr)) == uint64_t(registers->uc_mcontext.gregs[REG_RIP]))
        {
            uint64_t const returnAddress = *reinterpret_cast<uint64_t const*>(registers->uc_mcontext.gregs[REG_RSP]);
            WriteText("return ");
            WriteNumber(returnAddress, 16);
            WriteText("\n");
        }
#else
        (void)context;
        WriteText("\n");
#endif

        void* frames[MAX_FRAMES];
        backtrace_symbols_fd(frames, backtrace(frames, MAX_FRAMES), crashFd);
        WriteText("=== end\n");

        // SA_RESETHAND already restored the default action. Returning runs the faulting instruction
        // again, so the kernel still logs its segfault line and the process dies with the same
        // signal, which is what restarts it. A signal sent by kill has nothing to run again, and
        // raising it would do nothing as PID 1 in a container, so exit with the same status instead.
        if (info->si_code <= 0)
            _exit(128 + sig);
    }

    int FindLoadBase(dl_phdr_info* info, size_t /*size*/, void* /*data*/)
    {
        // The main program is listed first
        loadBase = info->dlpi_addr;
        return 1;
    }
}

void Acore::CrashLog::Install(std::string const& logsDir, std::string const& version)
{
    std::string const path = logsDir + "Crash.log";
    int const fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd >= 0)
        crashFd = fd;

    strncpy(revision, version.c_str(), sizeof(revision) - 1);
    dl_iterate_phdr(&FindLoadBase, nullptr);

    // A dump of this process doesn't fit the host's core dump quota, and failing to store it kept
    // the server down for over two minutes. The kernel skips the dump at a limit of 1, even when
    // core_pattern is a pipe (0 doesn't do that), and also for crashes this handler never sees.
    // So a compose file must not set a hard limit of 0: it can't be raised from here.
    rlimit core = {};
    if (getrlimit(RLIMIT_CORE, &core) == 0 && core.rlim_max >= 1)
    {
        core.rlim_cur = 1;
        setrlimit(RLIMIT_CORE, &core);
    }

    // The first backtrace() call loads libgcc, which can't be done safely during a crash
    void* frames[2];
    backtrace(frames, 2);

    stack_t stack = {};
    stack.ss_sp = alternateStack;
    stack.ss_size = sizeof(alternateStack);
    sigaltstack(&stack, nullptr);

    struct sigaction action = {};
    action.sa_sigaction = &OnCrash;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_ONSTACK;
    sigemptyset(&action.sa_mask);

    for (int sig : { SIGSEGV, SIGBUS, SIGFPE, SIGILL })
        sigaction(sig, &action, nullptr);
}

#else

void Acore::CrashLog::Install(std::string const& /*logsDir*/, std::string const& /*version*/)
{
}

#endif
