// Aurora Root bootstrap: starts and stops the Aurora APK host from a root shell.
//
// The binary is a launcher only. It runs supported ActivityManager/AppOps
// commands as argv (never through a shell) and reports diagnosable state. It does
// not implement native Surface, Input, IME or screen capture, and it does not use
// unverified private InputFlinger or capture bypass interfaces. The optional
// --capture flag only forwards capture=true so the APK itself asks Android for
// the system MediaProjection consent token.

#include "RootBootstrapCli.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <cerrno>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

using aurora::root::Command;
using aurora::root::Options;

// Native command results always separate "the helper ran" from "the helper could
// not be executed at all", which is what makes stop/status idempotent.
struct ExecOutcome {
    enum class Kind { Exited, NotStarted };
    Kind kind = Kind::NotStarted;
    int exitCode = -1;
};

// A shell cannot be used to interpret the returned code, so treat the shell's
// "command not found" code as an unavailable helper.
constexpr int kHelperNotFound = 127;

void note(const std::string& line) {
    std::cout << line << '\n';
}

void warn(const std::string& line) {
    std::cerr << "Aurora: warning: " << line << '\n';
}

void error(const std::string& line) {
    std::cerr << "Aurora: error: " << line << '\n';
}

#if defined(_WIN32)

// The desktop build exists for compile checks and --dry-run. Windows spawns the
// argv array directly as well, so no shell re-parses the arguments.
ExecOutcome runProcess(const std::vector<std::string>& argv) {
    std::vector<const char*> raw;
    raw.reserve(argv.size() + 1);
    for (const std::string& part : argv) {
        raw.push_back(part.c_str());
    }
    raw.push_back(nullptr);
    const intptr_t result = ::_spawnvp(_P_WAIT, raw[0], raw.data());
    if (result == -1) {
        return {ExecOutcome::Kind::NotStarted, -1};
    }
    return {ExecOutcome::Kind::Exited, static_cast<int>(result)};
}

// Output capture is only implemented for the Android/Linux target; status falls
// back to printing raw helper output.
bool captureProcess(const std::vector<std::string>&, std::string&, int&) {
    return false;
}

#else

ExecOutcome runProcess(const std::vector<std::string>& argv) {
    std::vector<char*> raw;
    raw.reserve(argv.size() + 1);
    for (const std::string& part : argv) {
        raw.push_back(const_cast<char*>(part.c_str()));
    }
    raw.push_back(nullptr);

    const pid_t child = ::fork();
    if (child < 0) {
        return {ExecOutcome::Kind::NotStarted, -1};
    }
    if (child == 0) {
        ::execv(raw[0], raw.data());
        ::_exit(kHelperNotFound);
    }

    int status = 0;
    while (::waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            return {ExecOutcome::Kind::NotStarted, -1};
        }
    }
    if (WIFEXITED(status)) {
        return {ExecOutcome::Kind::Exited, WEXITSTATUS(status)};
    }
    const int signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    return {ExecOutcome::Kind::Exited, 128 + signal};
}

bool captureProcess(const std::vector<std::string>& argv, std::string& output, int& exitCode) {
    int pipes[2] = {-1, -1};
    if (::pipe(pipes) != 0) {
        return false;
    }

    std::vector<char*> raw;
    raw.reserve(argv.size() + 1);
    for (const std::string& part : argv) {
        raw.push_back(const_cast<char*>(part.c_str()));
    }
    raw.push_back(nullptr);

    const pid_t child = ::fork();
    if (child < 0) {
        ::close(pipes[0]);
        ::close(pipes[1]);
        return false;
    }
    if (child == 0) {
        ::close(pipes[0]);
        ::dup2(pipes[1], STDOUT_FILENO);
        ::close(pipes[1]);
        ::execv(raw[0], raw.data());
        ::_exit(kHelperNotFound);
    }

    ::close(pipes[1]);
    char buffer[512];
    ssize_t count = 0;
    while ((count = ::read(pipes[0], buffer, sizeof(buffer))) > 0) {
        output.append(buffer, static_cast<std::size_t>(count));
    }
    ::close(pipes[0]);

    int status = 0;
    while (::waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            return false;
        }
    }
    exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    return true;
}

#endif

void printRawOutput(const std::string& label, const std::vector<std::string>& argv,
                    const std::string& output) {
    note("--- " + label + ": " + aurora::root::describeCommand(argv) + " ---");
    if (output.empty()) {
        note("(no output)");
    } else {
        std::cout << output;
        if (output.back() != '\n') {
            std::cout << '\n';
        }
    }
}

int runDryRun(const Options& options) {
    note(std::string("Aurora Root bootstrap dry run (") +
         aurora::root::commandToken(options.command) + ")");
    for (const aurora::root::PlannedCommand& command : aurora::root::plannedCommands(options)) {
        note("dry-run[" + command.label + "]: " + aurora::root::describeCommand(command.argv));
    }
    return aurora::root::exitcode::kSuccess;
}

// stop and status stay idempotent: running them again on a stopped host is a
// success with a diagnostic line, not an error.
int runStop() {
    note("Aurora Root bootstrap stop");
    bool environmentOk = true;

    const ExecOutcome stopService = runProcess(aurora::root::stopServiceCommand());
    if (stopService.kind == ExecOutcome::Kind::NotStarted ||
        stopService.exitCode == kHelperNotFound) {
        environmentOk = false;
        error(std::string("cannot execute ") + aurora::root::kAmBinary + "; stop was not performed");
    } else if (stopService.exitCode == 0) {
        note("overlay_service=stopped");
    } else {
        warn("overlay service was not running (stopservice exit " +
             std::to_string(stopService.exitCode) + "); repeating stop is harmless");
    }

    const ExecOutcome forceStop = runProcess(aurora::root::forceStopCommand());
    if (forceStop.kind == ExecOutcome::Kind::NotStarted ||
        forceStop.exitCode == kHelperNotFound) {
        environmentOk = false;
        error(std::string("cannot execute ") + aurora::root::kAmBinary +
              "; app process was not stopped");
    } else if (forceStop.exitCode != 0) {
        warn("force-stop reported exit " + std::to_string(forceStop.exitCode));
    }

    if (!environmentOk) {
        return aurora::root::exitcode::kEnvironment;
    }
    note("stop_result=ok");
    return aurora::root::exitcode::kSuccess;
}

int runStatus() {
    note("Aurora Root bootstrap status");

    const std::vector<std::string> appOpsCommand = aurora::root::appOpsGetCommand();
    const std::vector<std::string> serviceCommand = aurora::root::serviceDumpCommand();

    std::string appOpsOutput;
    int appOpsExit = -1;
    bool helperUnavailable = false;
    const bool appOpsCaptured = captureProcess(appOpsCommand, appOpsOutput, appOpsExit);
    if (appOpsCaptured) {
        if (appOpsExit == kHelperNotFound) {
            helperUnavailable = true;
            note("overlay_appop=unknown (helper unavailable)");
        } else {
            note(std::string("overlay_appop=") +
                 (aurora::root::appOpsGetShowsAllowed(appOpsOutput) ? "granted" : "not_granted"));
        }
    } else {
        note("overlay_appop=unknown (output capture unavailable)");
    }

    std::string serviceOutput;
    int serviceExit = -1;
    const bool serviceCaptured = captureProcess(serviceCommand, serviceOutput, serviceExit);
    if (serviceCaptured) {
        if (serviceExit == kHelperNotFound) {
            helperUnavailable = true;
            note("overlay_service=unknown (helper unavailable)");
        } else {
            note(std::string("overlay_service=") +
                 (aurora::root::serviceDumpShowsOverlay(serviceOutput) ? "running" : "stopped"));
        }
    } else {
        note("overlay_service=unknown (output capture unavailable)");
    }

    if (appOpsCaptured) {
        printRawOutput("appops-get", appOpsCommand, appOpsOutput);
    } else {
        note("--- appops-get: " + aurora::root::describeCommand(appOpsCommand) + " ---");
        runProcess(appOpsCommand);
    }
    if (serviceCaptured) {
        printRawOutput("service-dump", serviceCommand, serviceOutput);
    } else {
        note("--- service-dump: " + aurora::root::describeCommand(serviceCommand) + " ---");
        runProcess(serviceCommand);
    }

    if (helperUnavailable) {
        error("a required system helper is unavailable; status is incomplete");
        return aurora::root::exitcode::kEnvironment;
    }
    return aurora::root::exitcode::kSuccess;
}

bool waitForOverlayService() {
    constexpr int kAttempts = 10;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        std::string output;
        int exitCode = -1;
        if (!captureProcess(aurora::root::serviceDumpCommand(), output, exitCode)) {
            return false;
        }
        if (exitCode == 0 && aurora::root::serviceDumpShowsOverlay(output)) {
            return true;
        }
        if (attempt + 1 < kAttempts) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
    }
    return false;
}

int runStart(const Options& options) {
    const bool overlay = options.command == Command::Overlay;
    note(std::string("Aurora Root bootstrap start (") +
         aurora::root::commandToken(options.command) + ")");
    if (options.capture) {
        note("capture_extra=requested (the APK asks Android for MediaProjection consent)");
    }

    const ExecOutcome appOps = runProcess(aurora::root::appOpsGrantCommand());
    if (appOps.kind == ExecOutcome::Kind::NotStarted || appOps.exitCode == kHelperNotFound) {
        warn(std::string("cannot change the overlay AppOp with ") + aurora::root::kCmdBinary +
             "; using the existing device authorization");
    } else if (appOps.exitCode != 0) {
        warn("overlay AppOp was not changed (exit " + std::to_string(appOps.exitCode) +
             "); using the existing device authorization");
    } else {
        note("overlay_appop=granted");
    }

    const ExecOutcome forceStop = runProcess(aurora::root::forceStopCommand());
    if (forceStop.kind == ExecOutcome::Kind::NotStarted ||
        forceStop.exitCode == kHelperNotFound) {
        error(std::string("cannot execute ") + aurora::root::kAmBinary);
        return aurora::root::exitcode::kEnvironment;
    }

    const std::vector<std::string> startCommand =
        overlay ? aurora::root::overlayStartCommand(options)
                : aurora::root::activityStartCommand(options);
    const ExecOutcome start = runProcess(startCommand);
    if (start.kind == ExecOutcome::Kind::NotStarted || start.exitCode == kHelperNotFound) {
        error(std::string("cannot execute ") + aurora::root::kAmBinary);
        return aurora::root::exitcode::kEnvironment;
    }
    if (start.exitCode != 0) {
        error("unable to start the requested host (am start exit " +
              std::to_string(start.exitCode) + ")");
        return aurora::root::exitcode::kStartFailed;
    }

    if (!overlay) {
        note("activity_host=requested");
        return aurora::root::exitcode::kSuccess;
    }

    note("start_command=accepted");
    if (waitForOverlayService()) {
        note("overlay_service=running");
    } else {
        note("overlay_service=not_detected");
        warn("overlay service was not detected yet; re-check with the status command");
    }
    return aurora::root::exitcode::kSuccess;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args;
    for (int index = 1; index < argc; ++index) {
        args.emplace_back(argv[index]);
    }

    const aurora::root::ParseResult parsed = aurora::root::parseArguments(args);
    for (const std::string& warning : parsed.warnings) {
        warn(warning);
    }

    if (parsed.status == aurora::root::ParseStatus::Help) {
        std::cout << aurora::root::usageText();
        return aurora::root::exitcode::kSuccess;
    }
    if (parsed.status == aurora::root::ParseStatus::Error) {
        error(parsed.error);
        std::cerr << aurora::root::usageText();
        return aurora::root::exitcode::kUsage;
    }

    const Options& options = parsed.options;
    if (options.dryRun) {
        return runDryRun(options);
    }

    switch (options.command) {
        case aurora::root::Command::Overlay:
        case aurora::root::Command::Activity:
            return runStart(options);
        case aurora::root::Command::Stop:
            return runStop();
        case aurora::root::Command::Status:
            return runStatus();
        case aurora::root::Command::Help:
            break;
    }
    error("unhandled command");
    return aurora::root::exitcode::kFailure;
}
