import json
import os
import pathlib
import re
import signal
import subprocess
import sys
import time

PROTOCOL = "2025-06-18"
LOG_LINES = 80
MAX_LOG_LINES = 2000
MAX_WAIT_SECONDS = 55
BUILD_CPU_QUOTA = "400%"
BUILD_MEMORY_HIGH = "6G"
BUILD_JOBS = "4"
GPU_EVENTS = re.compile(
    r"amdgpu.*(fault|timeout|reset|hang)|i915.*(hang|reset|fault)|xe .*(hang|reset|timedout)"
    r"|NVRM: Xid|gpu hang|fence expiration|GPU reset", re.I)


class ToolError(Exception):
    pass


def tool(name, description, properties=None, required=()):
    return {"name": name, "description": description,
            "inputSchema": {"type": "object", "properties": properties or {}, "required": list(required),
                            "additionalProperties": False}}


def runtime_directory():
    return pathlib.Path(os.environ.get("XDG_RUNTIME_DIR") or f"/run/user/{os.getuid()}")


def state_directory(server):
    base = os.environ.get("XDG_STATE_HOME") or str(pathlib.Path.home() / ".local" / "state")
    return pathlib.Path(base) / server


class BuildLimits:
    def __init__(self, lock=None, cpu_quota=BUILD_CPU_QUOTA, memory_high=BUILD_MEMORY_HIGH, jobs=BUILD_JOBS):
        self.lock = pathlib.Path(lock) if lock else runtime_directory() / "lsw-build.lock"
        self.cpu_quota = cpu_quota
        self.memory_high = memory_high
        self.jobs = jobs

    def wrap(self, argv):
        return ["flock", str(self.lock), "systemd-run", "--user", "--scope", "-q", "-p", f"CPUQuota={self.cpu_quota}",
                "-p", f"MemoryHigh={self.memory_high}", "nice", "-n", "19", "ionice", "-c3", *argv]

    def environment(self):
        return {"JOBS": self.jobs, "CMAKE_BUILD_PARALLEL_LEVEL": self.jobs, "AV_GATE_JOBS": self.jobs}


class Jobs:
    def __init__(self, directory):
        self.directory = pathlib.Path(directory)

    def start(self, name, argv, cwd, env=None, exclusive=None):
        if exclusive:
            for job in self.list():
                if job["state"] == "running" and job.get("exclusive") == exclusive:
                    raise ToolError(f"job {job['id']} is already running {exclusive}; wait for it or cancel it")
        identity = time.strftime("%m%d-%H%M%S") + "-" + re.sub(r"[^A-Za-z0-9_.-]", "-", name)
        folder = self.directory / identity
        suffix = 1
        while folder.exists():
            suffix += 1
            folder = self.directory / f"{identity}-{suffix}"
        folder.mkdir(parents=True)
        environment = dict(os.environ)
        environment.update(env or {})
        runner = ['bash', '-c', '"${@:2}"; echo $? > "$1"', "job", str(folder / "exit"), *argv]
        with open(folder / "log", "wb") as log:
            process = subprocess.Popen(runner, cwd=str(cwd), env=environment, stdin=subprocess.DEVNULL,
                                       stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        meta = {"id": folder.name, "name": name, "argv": [str(part) for part in argv], "cwd": str(cwd),
                "pid": process.pid, "started": time.time(), "exclusive": exclusive}
        (folder / "meta.json").write_text(json.dumps(meta))
        return self.status(folder.name)

    def folder(self, identity):
        folder = self.directory / identity
        if not (folder / "meta.json").is_file():
            raise ToolError(f"no job {identity}; list_jobs shows the known ones")
        return folder

    def status(self, identity, lines=0):
        folder = self.folder(identity)
        meta = json.loads((folder / "meta.json").read_text())
        exit_file = folder / "exit"
        if exit_file.is_file() and exit_file.read_text().strip():
            code = int(exit_file.read_text().strip())
            state = "succeeded" if code == 0 else "failed"
            ended = exit_file.stat().st_mtime
        elif self.alive(meta["pid"]):
            code, state, ended = None, "running", time.time()
        elif (folder / "cancelled").is_file():
            code, state, ended = None, "cancelled", (folder / "cancelled").stat().st_mtime
        else:
            code, state, ended = None, "lost", (folder / "log").stat().st_mtime
        status = {"id": meta["id"], "name": meta["name"], "state": state, "exit_code": code,
                  "seconds": round(ended - meta["started"]), "command": " ".join(meta["argv"]),
                  "log": str(folder / "log"), "exclusive": meta.get("exclusive")}
        if lines:
            status["tail"] = self.tail(folder, lines)
        return status

    def alive(self, pid):
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return False
        except PermissionError:
            return True
        stat = pathlib.Path(f"/proc/{pid}/stat")
        return stat.exists() and stat.read_text().split(") ", 1)[-1][:1] != "Z"

    def tail(self, folder, lines, grep=None):
        text = (folder / "log").read_text(errors="replace").splitlines()
        if grep:
            pattern = re.compile(grep, re.I)
            text = [line for line in text if pattern.search(line)]
        return "\n".join(text[-min(lines, MAX_LOG_LINES):])

    def log(self, identity, lines=LOG_LINES, grep=None):
        return self.tail(self.folder(identity), lines, grep)

    def wait(self, identity, seconds=MAX_WAIT_SECONDS, lines=20):
        deadline = time.time() + min(seconds, MAX_WAIT_SECONDS)
        status = self.status(identity)
        while status["state"] == "running" and time.time() < deadline:
            time.sleep(1)
            status = self.status(identity)
        return self.status(identity, lines)

    def cancel(self, identity):
        folder = self.folder(identity)
        meta = json.loads((folder / "meta.json").read_text())
        if self.status(identity)["state"] == "running":
            (folder / "cancelled").touch()
            try:
                os.killpg(meta["pid"], signal.SIGTERM)
            except ProcessLookupError:
                pass
        return self.wait(identity, 10)

    def list(self, limit=20):
        if not self.directory.is_dir():
            return []
        folders = sorted((entry for entry in self.directory.iterdir() if (entry / "meta.json").is_file()),
                         key=lambda entry: entry.stat().st_mtime, reverse=True)
        return [self.status(entry.name) for entry in folders[:limit]]


class KernelLog:
    def gpu_events(self, since="1 hour ago", host=None):
        command = ["journalctl", "-k", "--no-pager", "-o", "short-iso", "--since", since]
        if host:
            command = ["ssh", "-o", "BatchMode=yes", host, " ".join(f"'{part}'" for part in command)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if result.returncode != 0 and not result.stdout:
            raise ToolError(f"journalctl failed: {result.stderr.strip()}")
        events = [line for line in result.stdout.splitlines() if GPU_EVENTS.search(line)]
        return {"since": since, "host": host or "local", "events": events[-200:], "count": len(events)}


class JobTools:
    def __init__(self, jobs, kernel=None):
        self.jobs = jobs
        self.kernel = kernel or KernelLog()

    def job_tools(self):
        identity = {"job": {"type": "string", "description": "the job id a start call returned"}}
        return [
            tool("list_jobs", "Background jobs this server started, newest first, with their state.",
                 {"limit": {"type": "integer"}}),
            tool("job_status", "State, exit code, duration and the last lines of a job's log.",
                 {**identity, "lines": {"type": "integer"}}, ["job"]),
            tool("job_log", "The last lines of a job's log, optionally only lines matching a regular expression.",
                 {**identity, "lines": {"type": "integer"}, "grep": {"type": "string"}}, ["job"]),
            tool("wait_job", "Waits up to 55 seconds for a job to end and returns its status and log tail. Call it "
                 "again while the job is still running.", {**identity, "seconds": {"type": "integer"}}, ["job"]),
            tool("cancel_job", "Stops a running job and everything it started.", identity, ["job"]),
            tool("gpu_kernel_events", "Host GPU faults, ring resets, hangs and Xid errors from the kernel log. Run it "
                 "after any GPU work: one event means stop and treat it as a driver bug.",
                 {"since": {"type": "string", "description": "journalctl --since, default '1 hour ago'"},
                  "host": {"type": "string", "description": "ssh target of a remote test host"}}),
        ]

    def list_jobs(self, limit=20):
        return self.jobs.list(limit)

    def job_status(self, job, lines=20):
        return self.jobs.status(job, lines)

    def job_log(self, job, lines=LOG_LINES, grep=None):
        return self.jobs.log(job, lines, grep)

    def wait_job(self, job, seconds=MAX_WAIT_SECONDS):
        return self.jobs.wait(job, seconds)

    def cancel_job(self, job):
        return self.jobs.cancel(job)

    def gpu_kernel_events(self, since="1 hour ago", host=None):
        return self.kernel.gpu_events(since, host)


class Server:
    def __init__(self, toolset, identity, reader=None, writer=None):
        self.toolset = toolset
        self.identity = identity
        self.reader = reader or sys.stdin
        self.writer = writer or sys.stdout

    def serve(self):
        for line in self.reader:
            line = line.strip()
            if not line:
                continue
            answer = self.handle(line)
            if answer is not None:
                self.writer.write(json.dumps(answer) + "\n")
                self.writer.flush()

    def handle(self, line):
        try:
            message = json.loads(line)
        except ValueError:
            return error_reply(None, -32700, "parse error")
        if not isinstance(message, dict) or message.get("jsonrpc") != "2.0" or "method" not in message:
            return error_reply(message.get("id") if isinstance(message, dict) else None, -32600,
                               "invalid request")
        method = message["method"]
        params = message.get("params") or {}
        if "id" not in message:
            return None
        identity = message["id"]
        if method == "initialize":
            requested = params.get("protocolVersion") if isinstance(params, dict) else None
            return reply(identity, {"protocolVersion": requested or PROTOCOL, "capabilities": {"tools": {}},
                                    "serverInfo": self.identity})
        if method == "ping":
            return reply(identity, {})
        if method == "tools/list":
            return reply(identity, {"tools": self.toolset.tools()})
        if method == "tools/call":
            return self.call(identity, params)
        return error_reply(identity, -32601, f"method not found: {method}")

    def call(self, identity, params):
        name = params.get("name") if isinstance(params, dict) else None
        arguments = (params.get("arguments") or {}) if isinstance(params, dict) else {}
        known = {entry["name"]: entry for entry in self.toolset.tools()}
        if name not in known:
            return error_reply(identity, -32602, f"unknown tool: {name}")
        if not isinstance(arguments, dict):
            return error_reply(identity, -32602, f"{name}: arguments are an object")
        schema = known[name]["inputSchema"]
        missing = [field for field in schema["required"] if field not in arguments]
        unexpected = [field for field in arguments if field not in schema["properties"]]
        if missing or unexpected:
            return error_reply(identity, -32602, f"{name}: missing {missing}, unexpected {unexpected}")
        try:
            value = getattr(self.toolset, name)(**arguments)
        except ToolError as error:
            return reply(identity, content(str(error), True))
        except Exception as error:
            return reply(identity, content(f"{name} failed: {type(error).__name__}: {error}", True))
        text = value if isinstance(value, str) else json.dumps(value, indent=1)
        return reply(identity, content(text, False))


def content(text, failed):
    return {"content": [{"type": "text", "text": text}], "isError": failed}


def reply(identity, result):
    return {"jsonrpc": "2.0", "id": identity, "result": result}


def error_reply(identity, code, text):
    return {"jsonrpc": "2.0", "id": identity, "error": {"code": code, "message": text}}
