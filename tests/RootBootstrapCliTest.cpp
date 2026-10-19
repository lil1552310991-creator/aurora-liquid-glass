// CLI contract test for the Aurora Root bootstrap.
//
// It compiles the pure parser/command builder from root-host/RootBootstrapCli.h
// and never touches a device: every assertion is about the argv the bootstrap
// would execute and about the exit-code contract documented in README.md.

#include "RootBootstrapCli.h"

#include <cassert>
#include <initializer_list>
#include <iostream>
#include <string>
#include <vector>

using namespace aurora::root;

namespace {

ParseResult parse(std::initializer_list<const char*> args) {
    std::vector<std::string> values;
    for (const char* arg : args) {
        values.emplace_back(arg);
    }
    return parseArguments(values);
}

std::string join(const std::vector<std::string>& parts) {
    std::string line;
    for (const std::string& part : parts) {
        if (!line.empty()) {
            line += ' ';
        }
        line += part;
    }
    return line;
}

bool contains(const std::vector<PlannedCommand>& plan, const std::string& label) {
    for (const PlannedCommand& command : plan) {
        if (command.label == label) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    // Legacy compatibility: no arguments keeps starting the APK overlay host.
    {
        const ParseResult result = parse({});
        assert(result.status == ParseStatus::Ok);
        assert(result.options.command == Command::Overlay);
        assert(result.options.backend == Backend::Default);
        assert(!result.options.showcaseGlass);
        assert(result.warnings.empty());
        assert(describeCommand(overlayStartCommand(result.options)) ==
               "/system/bin/am start -n com.aurora.runtime/.MainActivity --ez overlay true");
    }

    // Legacy --glass alias reaches the same APK extra as the explicit form.
    {
        const ParseResult legacy = parse({"--glass"});
        const ParseResult explicitForm = parse({"overlay", "--showcase", "glass"});
        assert(legacy.status == ParseStatus::Ok);
        assert(explicitForm.status == ParseStatus::Ok);
        assert(legacy.options.command == Command::Overlay);
        assert(explicitForm.options.command == Command::Overlay);
        assert(legacy.options.showcaseGlass && explicitForm.options.showcaseGlass);
        const std::string expected =
            "/system/bin/am start -n com.aurora.runtime/.MainActivity --ez overlay true"
            " --es showcase glass";
        assert(describeCommand(overlayStartCommand(legacy.options)) == expected);
        assert(describeCommand(overlayStartCommand(explicitForm.options)) == expected);
    }

    // New explicit overlay subcommand without showcase keeps the plain host.
    {
        const ParseResult result = parse({"overlay"});
        assert(result.status == ParseStatus::Ok);
        assert(result.options.command == Command::Overlay);
        assert(!result.options.showcaseGlass);
    }

    // --capture is accepted only by "overlay --showcase glass" and adds the
    // capture=true Activity extra next to the showcase extra.
    {
        const ParseResult result = parse({"overlay", "--showcase", "glass", "--capture"});
        assert(result.status == ParseStatus::Ok);
        assert(result.options.command == Command::Overlay);
        assert(result.options.showcaseGlass);
        assert(result.options.capture);
        assert(describeCommand(overlayStartCommand(result.options)) ==
               "/system/bin/am start -n com.aurora.runtime/.MainActivity --ez overlay true"
               " --es showcase glass --ez capture true");
    }
    {
        // Legacy --glass alias plus --capture reaches the same argv, and the
        // implicit default overlay command counts as the overlay subcommand.
        const ParseResult result = parse({"--glass", "--capture"});
        assert(result.status == ParseStatus::Ok);
        assert(result.options.command == Command::Overlay);
        assert(result.options.capture);
        assert(describeCommand(overlayStartCommand(result.options)) ==
               "/system/bin/am start -n com.aurora.runtime/.MainActivity --ez overlay true"
               " --es showcase glass --ez capture true");
    }
    {
        // Inline value spellings are rejected exactly like --glass/--dry-run.
        const ParseResult result = parse({"--capture=1"});
        assert(result.status == ParseStatus::Error);
        assert(result.error.find("--capture") != std::string::npos);
        assert(result.error.find("value") != std::string::npos);
    }
    {
        // --capture without the glass showcase is a usage error, not a silent no-op.
        const ParseResult result = parse({"--capture"});
        assert(result.status == ParseStatus::Error);
        assert(result.error.find("--capture") != std::string::npos);
        assert(result.error.find("showcase") != std::string::npos);
    }
    {
        const ParseResult result = parse({"overlay", "--capture"});
        assert(result.status == ParseStatus::Error);
        assert(result.error.find("--capture") != std::string::npos);
        assert(result.error.find("showcase") != std::string::npos);
    }
    {
        // --capture is overlay-only: activity, stop and status must reject it even
        // when the showcase extra is present.
        const ParseResult activity =
            parse({"activity", "--showcase", "glass", "--capture"});
        assert(activity.status == ParseStatus::Error);
        assert(activity.error.find("--capture") != std::string::npos);
        assert(activity.error.find("overlay") != std::string::npos);

        const ParseResult stop = parse({"stop", "--capture"});
        assert(stop.status == ParseStatus::Error);
        assert(stop.error.find("--capture") != std::string::npos);

        const ParseResult status = parse({"status", "--capture"});
        assert(status.status == ParseStatus::Error);
        assert(status.error.find("--capture") != std::string::npos);
    }
    {
        // The activity argv builder never carries the capture extra.
        Options options;
        options.command = Command::Activity;
        options.showcaseGlass = true;
        options.capture = true;
        const std::string command = describeCommand(activityStartCommand(options));
        assert(command.find("capture") == std::string::npos);
        assert(command.find("--es showcase glass") != std::string::npos);
    }
    {
        // --capture participates in --dry-run and in the documented plan order.
        const ParseResult result =
            parse({"overlay", "--showcase", "glass", "--capture", "--dry-run"});
        assert(result.status == ParseStatus::Ok);
        assert(result.options.dryRun);
        const std::vector<PlannedCommand> plan = plannedCommands(result.options);
        assert(plan.size() == 4);
        assert(plan[2].label == "start-overlay");
        const std::string start = join(plan[2].argv);
        assert(start.find("--ez capture true") != std::string::npos);
        assert(start.find("--es showcase glass") != std::string::npos);
    }
    {
        // Help documents the capture switch and the MediaProjection boundary.
        const std::string usage = usageText();
        assert(usage.find("--capture") != std::string::npos);
        assert(usage.find("MediaProjection") != std::string::npos);
        const ParseResult result = parse({"overlay", "--capture", "--help"});
        assert(result.status == ParseStatus::Help);
    }

    // Activity command with backend and glass showcase (inline value form too).
    {
        const ParseResult result = parse({"activity", "--backend", "vulkan", "--showcase=glass"});
        assert(result.status == ParseStatus::Ok);
        assert(result.options.command == Command::Activity);
        assert(result.options.backend == Backend::Vulkan);
        assert(result.options.showcaseGlass);
        assert(describeCommand(activityStartCommand(result.options)) ==
               "/system/bin/am start -n com.aurora.runtime/.MainActivity"
               " --es backend vulkan --es showcase glass");
    }

    // Legacy --activity --backend gles still selects the GLES activity host.
    {
        const ParseResult result = parse({"--activity", "--backend", "gles"});
        assert(result.status == ParseStatus::Ok);
        assert(result.options.command == Command::Activity);
        assert(result.options.backend == Backend::Gles);
        assert(describeCommand(activityStartCommand(result.options)) ==
               "/system/bin/am start -n com.aurora.runtime/.MainActivity --es backend gles");
    }

    // Legacy "--backend vulkan" alone used to be silently ignored by the overlay
    // path: it stays accepted, warns, and the value never reaches the argv.
    {
        const ParseResult result = parse({"--backend", "vulkan"});
        assert(result.status == ParseStatus::Ok);
        assert(result.options.command == Command::Overlay);
        assert(result.warnings.size() == 1);
        assert(result.warnings[0].find("overlay") != std::string::npos);
        const std::string command = describeCommand(overlayStartCommand(result.options));
        assert(command ==
               "/system/bin/am start -n com.aurora.runtime/.MainActivity --ez overlay true");
        assert(command.find("vulkan") == std::string::npos);
    }

    // stop and status keep their legacy flags and stay idempotent at CLI level.
    {
        const ParseResult stop = parse({"--stop"});
        assert(stop.status == ParseStatus::Ok);
        assert(stop.options.command == Command::Stop);
        assert(describeCommand(stopServiceCommand()) ==
               "/system/bin/am stopservice -n com.aurora.runtime/.AuroraOverlayService");
        assert(describeCommand(forceStopCommand()) ==
               "/system/bin/am force-stop com.aurora.runtime");
    }
    {
        const ParseResult status = parse({"status"});
        assert(status.status == ParseStatus::Ok);
        assert(status.options.command == Command::Status);
        assert(describeCommand(appOpsGetCommand()) ==
               "/system/bin/cmd appops get com.aurora.runtime android:system_alert_window");
        assert(describeCommand(serviceDumpCommand()) ==
               "/system/bin/dumpsys activity services com.aurora.runtime");
    }
    {
        const ParseResult repeated = parse({"stop", "stop"});
        assert(repeated.status == ParseStatus::Ok);
        assert(repeated.options.command == Command::Stop);
    }
    {
        const ParseResult repeated = parse({"status", "--status"});
        assert(repeated.status == ParseStatus::Ok);
        assert(repeated.options.command == Command::Status);
    }

    // Conflicting commands are rejected instead of silently picking a winner.
    {
        const ParseResult result = parse({"--stop", "status"});
        assert(result.status == ParseStatus::Error);
        assert(result.error.find("conflicting") != std::string::npos);
    }

    // Invalid and missing backend values are rejected.
    {
        const ParseResult result = parse({"--backend", "metal"});
        assert(result.status == ParseStatus::Error);
        assert(result.error.find("metal") != std::string::npos);
    }
    {
        const ParseResult result = parse({"--backend"});
        assert(result.status == ParseStatus::Error);
        assert(result.error.find("--backend") != std::string::npos);
    }
    {
        const ParseResult result = parse({"--backend="});
        assert(result.status == ParseStatus::Error);
    }

    // Unknown options and stray positional arguments are rejected.
    {
        const ParseResult result = parse({"--bogus"});
        assert(result.status == ParseStatus::Error);
        assert(result.error.find("--bogus") != std::string::npos);
    }
    {
        const ParseResult result = parse({"junk"});
        assert(result.status == ParseStatus::Error);
        assert(result.error.find("junk") != std::string::npos);
    }
    {
        const ParseResult result = parse({"overlay", "extra"});
        assert(result.status == ParseStatus::Error);
    }

    // Boolean flags must not be given values.
    {
        const ParseResult result = parse({"--glass=1"});
        assert(result.status == ParseStatus::Error);
        assert(result.error.find("--glass") != std::string::npos);
    }
    {
        const ParseResult result = parse({"--dry-run=1"});
        assert(result.status == ParseStatus::Error);
    }

    // Shell metacharacters can never reach an argv: values come from a whitelist.
    {
        const ParseResult result = parse({"--backend", "vulkan; rm -rf /data/local/tmp"});
        assert(result.status == ParseStatus::Error);
        assert(result.error.find("--backend") != std::string::npos);
    }
    {
        const ParseResult result = parse({"--showcase", "glass; id"});
        assert(result.status == ParseStatus::Error);
        assert(result.error.find("--showcase") != std::string::npos);
    }
    {
        const ParseResult result = parse({"--showcase=$(id)"});
        assert(result.status == ParseStatus::Error);
    }

    // --showcase accepts only the value the APK actually implements.
    {
        const ParseResult result = parse({"--showcase", "liquid"});
        assert(result.status == ParseStatus::Error);
    }
    {
        const ParseResult result = parse({"--showcase"});
        assert(result.status == ParseStatus::Error);
    }

    // --dry-run is accepted in both spellings and does not change the plan.
    {
        const ParseResult result = parse({"overlay", "--glass", "--dry-run"});
        assert(result.status == ParseStatus::Ok);
        assert(result.options.dryRun);
        assert(plannedCommands(result.options).size() == 4);
    }
    {
        const ParseResult result = parse({"--print-commands", "stop"});
        assert(result.status == ParseStatus::Ok);
        assert(result.options.dryRun);
    }

    // Help wins over validation errors and never executes anything.
    {
        const ParseResult result = parse({"--help"});
        assert(result.status == ParseStatus::Help);
        assert(result.options.command == Command::Help);
        assert(plannedCommands(result.options).empty());
    }
    {
        const ParseResult result = parse({"overlay", "--bogus", "--help"});
        assert(result.status == ParseStatus::Help);
    }
    {
        const ParseResult result = parse({"-h"});
        assert(result.status == ParseStatus::Help);
    }

    // Plans keep the documented execution order and exact helper argv.
    {
        const ParseResult result = parse({"overlay", "--showcase", "glass"});
        const std::vector<PlannedCommand> plan = plannedCommands(result.options);
        assert(plan.size() == 4);
        assert(plan[0].label == "appops-grant");
        assert(join(plan[0].argv) ==
               "/system/bin/cmd appops set com.aurora.runtime android:system_alert_window allow");
        assert(plan[1].label == "force-stop");
        assert(plan[2].label == "start-overlay");
        assert(join(plan[2].argv).find("--es showcase glass") != std::string::npos);
        assert(plan[3].label == "verify-service");
        assert(contains(plan, "verify-service"));
    }
    {
        const ParseResult result = parse({"activity", "--backend", "gles"});
        const std::vector<PlannedCommand> plan = plannedCommands(result.options);
        assert(plan.size() == 3);
        assert(plan[2].label == "start-activity");
        assert(contains(plan, "start-activity"));
    }
    {
        const ParseResult result = parse({"stop"});
        const std::vector<PlannedCommand> plan = plannedCommands(result.options);
        assert(plan.size() == 2);
        assert(plan[0].label == "stop-service");
        assert(plan[1].label == "force-stop");
    }
    {
        const ParseResult result = parse({"status"});
        const std::vector<PlannedCommand> plan = plannedCommands(result.options);
        assert(plan.size() == 2);
        assert(plan[0].label == "appops-get");
        assert(plan[1].label == "service-dump");
    }

    // Dry-run rendering quotes anything that is not a plain token.
    {
        assert(quoteArgument("plain-1.0/ok") == "plain-1.0/ok");
        assert(quoteArgument("android:system_alert_window") == "android:system_alert_window");
        assert(quoteArgument("a b") == "'a b'");
        assert(quoteArgument("it's") == "'it'\\''s'");
        assert(quoteArgument("") == "''");
    }

    // Diagnostic messages escape control characters so a hostile argument cannot
    // forge extra log lines.
    {
        assert(sanitizeForMessage("plain") == "plain");
        assert(sanitizeForMessage("a\nb") == "a?b");
        assert(sanitizeForMessage("a\r\nb\t") == "a??b?");
        const ParseResult result = parse({"--showcase", "glass\n--stop"});
        assert(result.status == ParseStatus::Error);
        assert(result.error.find('\n') == std::string::npos);
        assert(result.error.find('?') != std::string::npos);
    }

    // Status heuristics used to build the diagnosable summary lines.
    {
        assert(serviceDumpShowsOverlay(
            "ServiceRecord{d8f1 u0 com.aurora.runtime/.AuroraOverlayService}"));
        assert(!serviceDumpShowsOverlay("No services found in com.aurora.runtime"));
        assert(appOpsGetShowsAllowed("android:system_alert_window: allow"));
        assert(!appOpsGetShowsAllowed("android:system_alert_window: deny"));
        assert(!appOpsGetShowsAllowed("No operations."));
    }

    // Exit code contract stays stable for scripts.
    assert(exitcode::kSuccess == 0);
    assert(exitcode::kUsage == 2);
    assert(exitcode::kStartFailed == 3);
    assert(exitcode::kEnvironment == 4);

    std::cout << "RootBootstrapCliTest passed\n";
    return 0;
}
