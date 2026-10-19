#ifndef AURORA_ROOT_HOST_ROOT_BOOTSTRAP_CLI_H
#define AURORA_ROOT_HOST_ROOT_BOOTSTRAP_CLI_H

// Pure command line contract for the Aurora Root bootstrap.
//
// This header contains argument parsing and system command construction only. It
// deliberately performs no process execution, so the CLI contract (subcommands,
// validation, exit codes, generated argv) can be unit tested on a desktop host
// without an Android device.
//
// The bootstrap starts and stops the Aurora APK host through supported
// ActivityManager/AppOps commands. It is not a native Surface, Input, IME or
// screen capture implementation.

#include <cstddef>
#include <string>
#include <vector>

namespace aurora::root {

// APK side contract. Keep in sync with app/src/main/AndroidManifest.xml and the
// Aurora Host/Runtime extras handled by MainActivity and AuroraOverlayService.
inline constexpr const char* kPackageName = "com.aurora.runtime";
inline constexpr const char* kActivityComponent = "com.aurora.runtime/.MainActivity";
inline constexpr const char* kOverlayServiceComponent = "com.aurora.runtime/.AuroraOverlayService";
inline constexpr const char* kOverlayAppOp = "android:system_alert_window";
inline constexpr const char* kGlassShowcase = "glass";
// Boolean APK extra read by MainActivity before it asks Android for a
// MediaProjection consent token. The bootstrap only forwards the flag.
inline constexpr const char* kCaptureExtraKey = "capture";

// Supported device helpers. Every command is executed as argv (never through a
// shell), so no caller supplied text is ever re-parsed by /system/bin/sh.
inline constexpr const char* kAmBinary = "/system/bin/am";
inline constexpr const char* kCmdBinary = "/system/bin/cmd";
inline constexpr const char* kDumpsysBinary = "/system/bin/dumpsys";

enum class Command {
    Overlay,   // start the APK overlay service through MainActivity
    Activity,  // start the normal APK activity host
    Stop,      // stop the overlay service and the app process
    Status,    // report overlay service and Overlay AppOp state
    Help,
};

enum class Backend { Default, Gles, Vulkan };

struct Options {
    // Legacy default: no arguments starts the APK overlay host.
    Command command = Command::Overlay;
    Backend backend = Backend::Default;
    bool showcaseGlass = false;
    // Requests the APK host to ask Android for MediaProjection screen capture.
    // Accepted only for "overlay --showcase glass"; parseArguments rejects every
    // other combination so the extra can never be silently dropped.
    bool capture = false;
    bool dryRun = false;
};

enum class ParseStatus { Ok, Help, Error };

struct ParseResult {
    ParseStatus status = ParseStatus::Error;
    Options options{};
    std::string error{};
    std::vector<std::string> warnings{};
};

struct PlannedCommand {
    std::string label;
    std::vector<std::string> argv;
};

// Exit codes are part of the CLI contract; README documents the same table.
namespace exitcode {
inline constexpr int kSuccess = 0;
inline constexpr int kFailure = 1;
inline constexpr int kUsage = 2;
inline constexpr int kStartFailed = 3;
inline constexpr int kEnvironment = 4;
}

inline const char* commandToken(Command command) {
    switch (command) {
        case Command::Overlay:
            return "overlay";
        case Command::Activity:
            return "activity";
        case Command::Stop:
            return "stop";
        case Command::Status:
            return "status";
        case Command::Help:
            return "help";
    }
    return "unknown";
}

inline const char* backendToken(Backend backend) {
    switch (backend) {
        case Backend::Gles:
            return "gles";
        case Backend::Vulkan:
            return "vulkan";
        case Backend::Default:
            return "";
    }
    return "";
}

// Quote a single argv element for human readable dry-run output. Values never
// reach a shell, but the printed form must stay unambiguous.
inline std::string quoteArgument(const std::string& value) {
    const std::string safe =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_./:=+-@%";
    if (!value.empty() && value.find_first_not_of(safe) == std::string::npos) {
        return value;
    }
    std::string quoted = "'";
    for (const char character : value) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted += character;
        }
    }
    quoted += "'";
    return quoted;
}

inline std::string describeCommand(const std::vector<std::string>& argv) {
    std::string line;
    for (const std::string& part : argv) {
        if (!line.empty()) {
            line += ' ';
        }
        line += quoteArgument(part);
    }
    return line;
}

// Escape control characters before caller supplied text is echoed into a
// diagnostic message, so a hostile argument cannot forge extra log lines.
inline std::string sanitizeForMessage(const std::string& value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    for (const char raw : value) {
        const unsigned char character = static_cast<unsigned char>(raw);
        sanitized += (character < 0x20 || character == 0x7f) ? '?' : raw;
    }
    return sanitized;
}

inline std::vector<std::string> overlayStartCommand(const Options& options) {
    std::vector<std::string> argv{kAmBinary, "start", "-n", kActivityComponent, "--ez", "overlay", "true"};
    if (options.showcaseGlass) {
        argv.insert(argv.end(), {"--es", "showcase", kGlassShowcase});
    }
    if (options.capture) {
        // The APK asks the user for the Android MediaProjection consent token.
        // The bootstrap never captures anything itself.
        argv.insert(argv.end(), {"--ez", kCaptureExtraKey, "true"});
    }
    return argv;
}

inline std::vector<std::string> activityStartCommand(const Options& options) {
    std::vector<std::string> argv{kAmBinary, "start", "-n", kActivityComponent};
    if (options.backend == Backend::Gles || options.backend == Backend::Vulkan) {
        argv.insert(argv.end(), {"--es", "backend", backendToken(options.backend)});
    }
    if (options.showcaseGlass) {
        argv.insert(argv.end(), {"--es", "showcase", kGlassShowcase});
    }
    return argv;
}

inline std::vector<std::string> appOpsGrantCommand() {
    return {kCmdBinary, "appops", "set", kPackageName, kOverlayAppOp, "allow"};
}

inline std::vector<std::string> appOpsGetCommand() {
    return {kCmdBinary, "appops", "get", kPackageName, kOverlayAppOp};
}

inline std::vector<std::string> forceStopCommand() {
    return {kAmBinary, "force-stop", kPackageName};
}

inline std::vector<std::string> stopServiceCommand() {
    return {kAmBinary, "stopservice", "-n", kOverlayServiceComponent};
}

inline std::vector<std::string> serviceDumpCommand() {
    return {kDumpsysBinary, "activity", "services", kPackageName};
}

// Status heuristics over captured helper output. Both are intentionally narrow:
// a false negative degrades to the raw output which is printed alongside.
inline bool serviceDumpShowsOverlay(const std::string& dump) {
    return dump.find("AuroraOverlayService") != std::string::npos;
}

inline bool appOpsGetShowsAllowed(const std::string& dump) {
    return dump.find("allow") != std::string::npos;
}

// The exact helper invocations a command performs, in execution order. Used by
// --dry-run and by the CLI contract test.
inline std::vector<PlannedCommand> plannedCommands(const Options& options) {
    std::vector<PlannedCommand> plan;
    switch (options.command) {
        case Command::Overlay:
            plan.push_back({"appops-grant", appOpsGrantCommand()});
            plan.push_back({"force-stop", forceStopCommand()});
            plan.push_back({"start-overlay", overlayStartCommand(options)});
            plan.push_back({"verify-service", serviceDumpCommand()});
            break;
        case Command::Activity:
            plan.push_back({"appops-grant", appOpsGrantCommand()});
            plan.push_back({"force-stop", forceStopCommand()});
            plan.push_back({"start-activity", activityStartCommand(options)});
            break;
        case Command::Stop:
            plan.push_back({"stop-service", stopServiceCommand()});
            plan.push_back({"force-stop", forceStopCommand()});
            break;
        case Command::Status:
            plan.push_back({"appops-get", appOpsGetCommand()});
            plan.push_back({"service-dump", serviceDumpCommand()});
            break;
        case Command::Help:
            break;
    }
    return plan;
}

inline std::string usageText() {
    return std::string(
        "Aurora Root bootstrap (starts/stops the Aurora APK host from a root shell)\n"
        "\n"
        "Usage:\n"
        "  aurora_root_bootstrap [overlay] [--showcase glass] [--capture] [--dry-run]\n"
        "  aurora_root_bootstrap activity [--backend gles|vulkan] [--showcase glass] [--dry-run]\n"
        "  aurora_root_bootstrap stop\n"
        "  aurora_root_bootstrap status\n"
        "  aurora_root_bootstrap help\n"
        "\n"
        "Commands:\n"
        "  overlay    grant the Overlay AppOp and start the Aurora overlay service (default)\n"
        "  activity   start the normal Aurora activity host instead of the overlay service\n"
        "  stop       stop the overlay service and the app process (idempotent)\n"
        "  status     report overlay service and Overlay AppOp state (idempotent)\n"
        "\n"
        "Options:\n"
        "  --showcase glass   start the Showcase with the glass demo (APK extra showcase=glass)\n"
        "  --glass            legacy alias of --showcase glass\n"
        "  --capture          request Android MediaProjection capture (APK extra capture=true)\n"
        "                     valid only for: overlay --showcase glass\n"
        "  --backend VALUE    activity backend: gles or vulkan (ignored by the overlay host)\n"
        "  --dry-run          print the exact commands without executing them\n"
        "  -h, --help         show this help\n"
        "\n"
        "Legacy flags --stop, --status, --activity and --glass keep working.\n"
        "\n"
        "--capture makes the APK show the Android system MediaProjection consent dialog;\n"
        "it is not a Root screen-capture bypass. Without that user authorization the\n"
        "overlay still starts, just without a screen capture source. --capture is\n"
        "rejected unless the command is overlay with --showcase glass.\n"
        "\n"
        "Exit codes:\n"
        "  0  success\n"
        "  2  usage error: unknown option, invalid value or conflicting commands\n"
        "  3  the requested host could not be started (am start failed)\n"
        "  4  a required system helper could not be executed\n"
        "\n"
        "start exits 0 once am start is accepted and stop/status exit 0 whenever the\n"
        "helpers ran; read the overlay_service= and overlay_appop= lines for the\n"
        "actual state instead of the exit code.\n"
        "\n"
        "This binary only bootstraps the APK host. Native Surface, Input, IME and\n"
        "screen capture stay in the Aurora Host/Runtime inside the APK.\n");
}

// Parse the CLI. Help wins over every other validation error, matching common
// CLI behaviour, and legacy flags keep their historical meaning.
inline ParseResult parseArguments(const std::vector<std::string>& args) {
    ParseResult result;
    Options options;
    Command command = Command::Overlay;
    bool commandSet = false;

    for (const std::string& arg : args) {
        if (arg == "-h" || arg == "--help" || arg == "help") {
            result.status = ParseStatus::Help;
            result.options.command = Command::Help;
            return result;
        }
    }

    const auto fail = [&result](const std::string& message) {
        result.status = ParseStatus::Error;
        result.error = message;
    };

    const auto setCommand = [&](Command candidate, const std::string& shown) {
        if (commandSet && command != candidate) {
            fail("conflicting commands: " + std::string(commandToken(command)) + " and " + shown);
            return false;
        }
        command = candidate;
        commandSet = true;
        return true;
    };

    const auto rejectInlineValue = [&fail](const std::string& name) {
        fail("option " + sanitizeForMessage(name) + " does not take a value");
    };

    for (std::size_t index = 0; index < args.size(); ++index) {
        const std::string& arg = args[index];
        std::string name = arg;
        std::string inlineValue;
        bool hasInlineValue = false;
        if (arg.size() > 2 && arg[0] == '-' && arg[1] == '-') {
            const std::size_t equals = arg.find('=');
            if (equals != std::string::npos) {
                name = arg.substr(0, equals);
                inlineValue = arg.substr(equals + 1);
                hasInlineValue = true;
            }
        }

        if (arg == "overlay") {
            if (!setCommand(Command::Overlay, "overlay")) {
                return result;
            }
            continue;
        }
        if (arg == "activity" || name == "--activity") {
            if (hasInlineValue) {
                rejectInlineValue(name);
                return result;
            }
            if (!setCommand(Command::Activity, "activity")) {
                return result;
            }
            continue;
        }
        if (arg == "stop" || name == "--stop") {
            if (hasInlineValue) {
                rejectInlineValue(name);
                return result;
            }
            if (!setCommand(Command::Stop, "stop")) {
                return result;
            }
            continue;
        }
        if (arg == "status" || name == "--status") {
            if (hasInlineValue) {
                rejectInlineValue(name);
                return result;
            }
            if (!setCommand(Command::Status, "status")) {
                return result;
            }
            continue;
        }
        if (name == "--backend") {
            std::string value;
            if (hasInlineValue) {
                value = inlineValue;
            } else if (index + 1 < args.size()) {
                value = args[++index];
            } else {
                fail("missing value for --backend (expected gles or vulkan)");
                return result;
            }
            if (value == "gles") {
                options.backend = Backend::Gles;
            } else if (value == "vulkan") {
                options.backend = Backend::Vulkan;
            } else {
                fail("invalid value for --backend: '" + sanitizeForMessage(value) +
                     "' (expected gles or vulkan)");
                return result;
            }
            continue;
        }
        if (name == "--showcase") {
            std::string value;
            if (hasInlineValue) {
                value = inlineValue;
            } else if (index + 1 < args.size()) {
                value = args[++index];
            } else {
                fail("missing value for --showcase (expected glass)");
                return result;
            }
            if (value != kGlassShowcase) {
                fail("invalid value for --showcase: '" + sanitizeForMessage(value) +
                     "' (expected glass)");
                return result;
            }
            options.showcaseGlass = true;
            continue;
        }
        if (name == "--glass") {
            if (hasInlineValue) {
                rejectInlineValue(name);
                return result;
            }
            options.showcaseGlass = true;
            continue;
        }
        if (name == "--capture") {
            if (hasInlineValue) {
                rejectInlineValue(name);
                return result;
            }
            options.capture = true;
            continue;
        }
        if (name == "--dry-run" || name == "--print-commands") {
            if (hasInlineValue) {
                rejectInlineValue(name);
                return result;
            }
            options.dryRun = true;
            continue;
        }
        if (name == "--overlay") {
            if (hasInlineValue) {
                rejectInlineValue(name);
                return result;
            }
            if (!setCommand(Command::Overlay, "overlay")) {
                return result;
            }
            continue;
        }

        if (!arg.empty() && arg[0] == '-') {
            fail("unknown option: " + sanitizeForMessage(arg));
        } else {
            fail("unknown argument: " + sanitizeForMessage(arg));
        }
        return result;
    }

    options.command = command;

    // --capture flips the APK into the Android MediaProjection consent flow, which
    // only the overlay host with the glass showcase implements. Every other
    // combination is a usage error instead of a silently ignored flag.
    if (options.capture) {
        if (options.command != Command::Overlay) {
            fail(std::string("--capture is only supported by the overlay command, not ") +
                 commandToken(options.command));
            return result;
        }
        if (!options.showcaseGlass) {
            fail("--capture requires --showcase glass; use: overlay --showcase glass --capture");
            return result;
        }
    }

    if (options.backend != Backend::Default) {
        if (options.command == Command::Overlay) {
            result.warnings.push_back(
                "--backend is ignored by the overlay host, which always uses GLES3");
        } else if (options.command != Command::Activity) {
            result.warnings.push_back(std::string("--backend is ignored by the ") +
                                      commandToken(options.command) + " command");
        }
    }
    if (options.showcaseGlass &&
        (options.command == Command::Stop || options.command == Command::Status)) {
        result.warnings.push_back(std::string("--showcase/--glass is ignored by the ") +
                                  commandToken(options.command) + " command");
    }

    result.status = ParseStatus::Ok;
    result.options = options;
    return result;
}

}  // namespace aurora::root

#endif  // AURORA_ROOT_HOST_ROOT_BOOTSTRAP_CLI_H
