#pragma once
// process.h -- starting other programs: the launcher runs ffmpeg to assemble a video, and a cartoon video's frames are rendered by child runs of the launcher itself (cartoon_render.h).
// No shell is involved (so no quoting, percent or glob rules to get wrong), the arguments go to the program as they are.

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdlib.h>
#else
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

// Run a subprocess with the given argv directly (no shell, so there's no
// cmd.exe/sh quoting or percent/glob expansion to worry about - important
// since ffmpeg's "%04d" frame-pattern argument would otherwise be at the
// mercy of the shell's own parsing rules). Returns the child's exit code, or
// -1 if it could not be launched (e.g. the executable isn't found on PATH).
//
// Windows: CreateProcess re-tokenizes a single command-line string, so every
// argument needs real Windows command-line quoting - not just wrapping in a
// literal '"'..'"' pair, which mishandles a trailing backslash (common in a
// GUI-picked output directory, e.g. "C:\out\"): CreateProcess's parser
// treats an odd run of backslashes immediately before a '"' as escaping that
// quote rather than closing the argument, so the naive form silently merges
// this argument with the next one on the command line instead of failing
// loudly. win32_quote_arg() below implements Microsoft's own documented
// quoting algorithm (doubling backslashes that precede the closing quote or
// an embedded literal '"', and escaping any embedded '"') so every argument
// round-trips exactly regardless of its content.
#ifdef _WIN32
inline std::string win32_quote_arg(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos)
        return arg;   // no special characters - safe to pass through unquoted

    std::string out = "\"";
    for (auto it = arg.begin(); ; ++it) {
        int backslashes = 0;
        while (it != arg.end() && *it == '\\') { ++backslashes; ++it; }
        if (it == arg.end()) {
            // Trailing backslashes are followed by our own closing quote -
            // double them so they stay literal instead of escaping it.
            out.append(static_cast<size_t>(backslashes) * 2, '\\');
            break;
        } else if (*it == '"') {
            // Backslashes immediately before a literal quote must also be
            // doubled, then the quote itself is escaped.
            out.append(static_cast<size_t>(backslashes) * 2 + 1, '\\');
            out.push_back('"');
        } else {
            // An ordinary run of backslashes not before a quote is literal.
            out.append(static_cast<size_t>(backslashes), '\\');
            out.push_back(*it);
        }
    }
    out.push_back('"');
    return out;
}
#endif
//
// POSIX (macOS/Linux): posix_spawnp takes argv[] directly with no
// re-parsing, so no quoting is needed at all - the same argv vector is
// passed through byte-for-byte. posix_spawnp (not fork+exec) is used
// deliberately: this process already runs a background thread
// (BackgroundPngConverter below), and fork() in a multithreaded process only
// clones the calling thread, risking the child deadlocking on a libc lock
// another thread held at fork time. No file_actions/chdir setup is needed -
// the child inherits fds 0/1/2 and the CWD by default, matching
// CreateProcess's behavior above. A signal-killed child is mapped to
// 128+signal so it can't collide with this function's own -1 "launch
// failed" sentinel.
inline int run_subprocess(const std::vector<std::string>& argv) {
#ifdef _WIN32
    std::string cmdline;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i > 0) cmdline += ' ';
        cmdline += win32_quote_arg(argv[i]);
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    // CreateProcess may write into this buffer, so it can't be a literal/const string.
    std::vector<char> mutableCmd(cmdline.begin(), cmdline.end());
    mutableCmd.push_back('\0');

    BOOL ok = CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                              0, nullptr, nullptr, &si, &pi);
    if (!ok) {
        return -1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(exitCode);
#else
    if (argv.empty()) return -1;

    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);

    pid_t pid = 0;
    int spawnRc = posix_spawnp(&pid, cargv[0], nullptr, nullptr, cargv.data(), environ);
    if (spawnRc != 0) {
        return -1;
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
#endif
}

#ifdef _WIN32
// Every program this one starts (Child, below) belongs to a job object that kills its processes when the last handle to it closes, and the only handle is this process's own: so when this
// process ends, however it ends (the GUI's Stop kills it; a crash), the workers and frame runs it started end with it instead of rendering on with nobody to read their pictures.
inline HANDLE killOnExitJob() {
	static const HANDLE job = [] {
		HANDLE h = CreateJobObjectA(nullptr, nullptr);
		if (h) {
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
			info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			SetInformationJobObject(h, JobObjectExtendedLimitInformation, &info, sizeof(info));
		}
		return h;
	}();
	return job;
}
#endif

// A program started with one more environment variable (`envName`=`envValue`, when envName is not empty), that can be asked whether it is still running, and stopped. The child sees only
// its own environment: several children run at once (a cartoon video's frames), so a variable cannot be set in this process for one of them.
class Child {
  public:
	Child() = default;
	Child(const Child &) = delete;
	Child &operator=(const Child &) = delete;
	~Child() { release(); }

	// Starts `exe args...`; false when it could not be started.
	bool start(const std::string &exe, const std::vector<std::string> &args, const std::string &envName, const std::string &envValue) {
		release();
		exitCode_ = 1;
		finished_ = false;
#ifdef _WIN32
		std::string cmd = win32_quote_arg(exe);
		for (const std::string &a : args) cmd += " " + win32_quote_arg(a);
		std::string block;
		for (char **e = _environ; e && *e; ++e) {
			const std::string entry(*e);
			const bool same = !envName.empty() && entry.size() > envName.size() && _strnicmp(entry.c_str(), envName.c_str(), envName.size()) == 0 && entry[envName.size()] == '=';
			if (!same) block.append(entry).push_back('\0');
		}
		if (!envName.empty()) block.append(envName + "=" + envValue).push_back('\0');
		block.push_back('\0');
		STARTUPINFOA si{};
		si.cb = sizeof(si);
		PROCESS_INFORMATION pi{};
		std::vector<char> mutableCmd(cmd.begin(), cmd.end());
		mutableCmd.push_back('\0');
		// Started suspended and put in the kill-on-exit job BEFORE it runs a single instruction, so that nothing it starts can be outside the job.
		if (!CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, block.data(), nullptr, &si, &pi)) return false;
		process_ = pi.hProcess;
		if (HANDLE job = killOnExitJob()) {
			if (!AssignProcessToJobObject(job, process_)) {
				// (e.g. this program is itself in a job that does not allow another: the child is then an ordinary one, which a killed launcher leaves running, so say so, once)
				static bool told = false;
				if (!told) {
					told = true;
					std::cerr << "WARNING: could not put " << exe << " in the group of programs that end with this one (error " << GetLastError()
							  << "): if this program is stopped, the programs it started may keep running." << std::endl;
				}
			}
		}
		if (ResumeThread(pi.hThread) == static_cast<DWORD>(-1)) {
			TerminateProcess(pi.hProcess, 1);
			CloseHandle(pi.hThread);
			CloseHandle(process_);
			process_ = nullptr;
			return false;
		}
		CloseHandle(pi.hThread);
		return true;
#else
		std::vector<std::string> argvStore = {exe};
		argvStore.insert(argvStore.end(), args.begin(), args.end());
		std::vector<char *> argv;
		for (std::string &a : argvStore) argv.push_back(&a[0]);
		argv.push_back(nullptr);
		std::vector<std::string> envStore;
		for (char **e = environ; e && *e; ++e) {
			const std::string entry(*e);
			if (envName.empty() || entry.compare(0, envName.size() + 1, envName + "=") != 0) envStore.push_back(entry);
		}
		if (!envName.empty()) envStore.push_back(envName + "=" + envValue);
		std::vector<char *> envp;
		for (std::string &e : envStore) envp.push_back(&e[0]);
		envp.push_back(nullptr);
		pid_ = 0;
		if (posix_spawn(&pid_, exe.c_str(), nullptr, nullptr, argv.data(), envp.data()) != 0) {
			pid_ = 0;
			return false;
		}
		return true;
#endif
	}

	// True while the program has not ended (never started: false). Does not wait.
	bool running() {
		if (finished_) return false;
#ifdef _WIN32
		if (!process_) return false;
		if (WaitForSingleObject(process_, 0) == WAIT_TIMEOUT) return true;
		DWORD code = 1;
		GetExitCodeProcess(process_, &code);
		exitCode_ = static_cast<int>(code);
#else
		if (pid_ <= 0) return false;
		int status = 0;
		const pid_t done = waitpid(pid_, &status, WNOHANG);
		if (done == 0) return true;
		exitCode_ = done == pid_ && WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
		finished_ = true;
		return false;
	}

	// Waits up to `seconds` (0: as long as it takes) for the program to end; true when it has.
	bool wait(int seconds) {
		const auto start = std::chrono::steady_clock::now();
		while (running()) {
			if (seconds > 0 && std::chrono::steady_clock::now() - start > std::chrono::seconds(seconds)) return false;
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		return true;
	}

	// Stops the program (it does not get to clean up) and waits for it to be gone.
	void kill() {
		if (!running()) return;
#ifdef _WIN32
		TerminateProcess(process_, 1);
		WaitForSingleObject(process_, 10000);
#else
		::kill(pid_, SIGKILL);
		int status = 0;
		waitpid(pid_, &status, 0);
#endif
		exitCode_ = 1;
		finished_ = true;
	}

	int exitCode() const { return exitCode_; }

  private:
	void release() {
#ifdef _WIN32
		if (process_) {
			CloseHandle(process_);
			process_ = nullptr;
		}
#endif
	}
#ifdef _WIN32
	HANDLE process_ = nullptr;
#else
	pid_t pid_ = 0;
#endif
	int exitCode_ = 1;
	bool finished_ = true;
};

// Runs `exe args...` with the extra environment variable (see Child) and waits for it; when it has not finished after `timeoutSeconds` (0: wait as long as it takes) the process is killed and
// `*timedOut` set. Returns its exit code, or -1 when it could not be started.
inline int runChild(const std::string &exe, const std::vector<std::string> &args, const std::string &envName, const std::string &envValue, int timeoutSeconds, bool *timedOut = nullptr) {
	if (timedOut) *timedOut = false;
	Child child;
	if (!child.start(exe, args, envName, envValue)) return -1;
	if (!child.wait(timeoutSeconds)) {
		child.kill();
		if (timedOut) *timedOut = true;
		return 1;
	}
	return child.exitCode();
}
