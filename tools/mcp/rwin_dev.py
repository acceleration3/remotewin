#!/usr/bin/env python3
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from mcp_server import BuildLimits, Jobs, JobTools, Server, ToolError, state_directory, tool  # noqa: E402

SERVER = {"name": "rwin-dev", "version": "1"}
SIDES = {
    "host": ("build", ["-G", "Ninja"]),
    "guest": ("build-guest", ["-G", "Ninja", "-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake", "-DRWIN_BUILD_TESTS=OFF"]),
}
REFUSED_TARGETS = {"remotewin-probe"}
QUICK_TIMEOUT = 120


class RwinDev(JobTools):
    def __init__(self):
        super().__init__(Jobs(state_directory("rwin-dev") / "jobs"))
        self.limits = BuildLimits()

    def tools(self):
        return [
            tool("status", "The remotewin checkout: branch and changes, which build trees are configured, and the "
                 "running jobs."),
            tool("build", "Configures (when needed) and builds one side as a job under the build lock, at the limits "
                 "LSW_BUILD_JOBS, LSW_BUILD_CPU_QUOTA and LSW_BUILD_MEMORY_HIGH set: "
                 "host is the Wayland client and its tests in build/, guest the mingw cross-build of "
                 "remotewin-guest.exe in build-guest/. target picks one target, such as remotewin-wgcbench.",
                 {"side": {"type": "string", "enum": sorted(SIDES)}, "target": {"type": "string"},
                  "reconfigure": {"type": "boolean"}}, ["side"]),
            tool("test", "Runs the host tests as a job: unit (every test but integration, what CI runs) or "
                 "integration (the resize and window cases against a headless kwin_wayland, about 25 s). case runs one "
                 "integration case; trace adds the host's trace log and the Wayland debug log to it.",
                 {"kind": {"type": "string", "enum": ["unit", "integration"]},
                  "case": {"type": "string", "description": "one case, such as drag-corner"},
                  "trace": {"type": "boolean"}}, ["kind"]),
            tool("integration_cases", "The integration cases tests/integration registers."),
            tool("ci", "What CI runs, as one job: the host build and unit tests, then the guest cross-build."),
            tool("window_states", "Compares what running mirrors say about their windows (Describe over D-Bus) with "
                 "what KWin shows, and reports geometry and stacking differences. It loads a KWin script into the "
                 "live session for a moment.",
                 {"pids": {"type": "array", "items": {"type": "integer"}}}),
            *self.job_tools(),
        ]

    def run(self, command, timeout=QUICK_TIMEOUT):
        result = subprocess.run(command, cwd=str(ROOT), capture_output=True, text=True, timeout=timeout)
        return result.returncode, (result.stdout + result.stderr).strip()

    def status(self):
        git = lambda *arguments: self.run(["git", *arguments])[1]
        return {"branch": git("rev-parse", "--abbrev-ref", "HEAD"), "head": git("log", "-1", "--format=%h %s")[:100],
                "changed_files": len(git("status", "--porcelain").splitlines()),
                "configured": {side: (ROOT / folder / "CMakeCache.txt").is_file() for side, (folder, _) in SIDES.items()},
                "running_jobs": [job for job in self.jobs.list() if job["state"] == "running"]}

    def steps_for(self, side, target=None, reconfigure=False):
        folder, options = SIDES[side]
        steps = []
        if reconfigure or not (ROOT / folder / "CMakeCache.txt").is_file():
            steps.append(["cmake", "-S", ".", "-B", folder, *options])
        steps.append(["cmake", "--build", folder] + (["--target", target] if target else []))
        return steps

    def start(self, name, steps, env=None):
        script = " && ".join(" ".join(f"'{part}'" for part in step) for step in steps)
        environment = dict(self.limits.environment())
        environment.update(env or {})
        return self.jobs.start(name, self.limits.wrap(["bash", "-c", script]), ROOT, environment, exclusive="build")

    def build(self, side, target=None, reconfigure=False):
        if target in REFUSED_TARGETS:
            raise ToolError(f"{target} belongs to the conformance matrix, which stays off unless the user asks")
        return self.start(f"build-{side}", self.steps_for(side, target, reconfigure))

    def integration_cases(self):
        text = (ROOT / "tests" / "integration" / "CMakeLists.txt").read_text()
        block = re.search(r"foreach\(case (.*?)\)", text, re.S)
        return block.group(1).split() if block else []

    def test(self, kind, case=None, trace=False):
        folder, _ = SIDES["host"]
        steps = self.steps_for("host")
        env = {"RWIN_INTEGRATION_HOST_LOG": "trace", "RWIN_INTEGRATION_WAYLAND_DEBUG": "1"} if trace else None
        if kind == "unit":
            steps.append(["ctest", "--test-dir", folder, "-LE", "integration", "--output-on-failure",
                          "-j", self.limits.jobs])
        elif case:
            if case not in self.integration_cases():
                raise ToolError(f"no integration case {case}; integration_cases lists them")
            steps.append([f"{folder}/tests/integration/rwin_resize_integration", case])
        else:
            steps.append(["ctest", "--test-dir", folder, "-L", "integration", "--output-on-failure", "-j4"])
        return self.start(f"test-{kind}" + (f"-{case}" if case else ""), steps, env)

    def ci(self):
        steps = self.steps_for("host")
        steps.append(["ctest", "--test-dir", SIDES["host"][0], "-LE", "integration", "--output-on-failure"])
        steps += self.steps_for("guest")
        return self.start("ci", steps)

    def window_states(self, pids=None):
        code, output = self.run(["python3", "tools/window_states.py", "--json", *[str(pid) for pid in pids or []]])
        if code not in (0, 1):
            raise ToolError(output)
        return output


def main():
    Server(RwinDev(), SERVER).serve()


if __name__ == "__main__":
    main()
