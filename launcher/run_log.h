#pragma once
// run_log.h -- a long run's own log: what it prints (std::cout and std::cerr) copied to a file beside its output, and what it printed as a problem gathered for a summary at its
// end. A video prints a line a frame - some 1,400 for a minute of film - and the warnings among them ("[score] ...", a frame that failed) scroll away, and the console is gone
// when the run ends; the frames' own runs keep their logs in the work folders (cartoon_render.h), but this program's lines were kept nowhere.
//
// A problem is a line printed to std::cerr, or one to std::cout that begins with "WARNING" or "ERROR". std::clog (a render's "Scanlines remaining" progress) is left alone.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <streambuf>
#include <string>
#include <vector>

namespace run_log {

// The problems a run printed, each distinct line once with how many times it was printed, in the order they were first printed.
struct Problems {
	std::vector<std::string> lines;
	std::map<std::string, int> count;
	void add(const std::string &line) {
		if (count[line]++ == 0) lines.push_back(line);
	}
	int total() const {
		int n = 0;
		for (const auto &[line, k] : count) n += k;
		return n;
	}
};

// Whether a finished line is a problem: every line of the error stream but blank ones, and the output stream's warnings and errors.
inline bool isProblem(const std::string &line, bool errorStream) {
	const std::size_t at = line.find_first_not_of(" \t\r\n");
	if (at == std::string::npos) return false;
	if (errorStream) return true;
	return line.compare(at, 7, "WARNING") == 0 || line.compare(at, 5, "ERROR") == 0;
}

// A stream buffer that passes everything on to the stream's own buffer and copies it to the log file; its finished lines that are problems are gathered. One lock for both
// streams: the threads of a video print from several places at once.
class Tee : public std::streambuf {
  public:
	Tee(std::streambuf *original, std::ofstream &file, std::mutex &lock, Problems &problems, bool errorStream)
		: original_(original), file_(file), lock_(lock), problems_(problems), errorStream_(errorStream) {}
	std::streambuf *original() const { return original_; }

  protected:
	int overflow(int c) override {
		if (c == traits_type::eof()) return traits_type::not_eof(c);
		const char ch = static_cast<char>(c);
		return xsputn(&ch, 1) == 1 ? c : traits_type::eof();
	}
	std::streamsize xsputn(const char *s, std::streamsize n) override {
		std::lock_guard<std::mutex> hold(lock_);
		const std::streamsize written = original_->sputn(s, n);
		file_.write(s, n);
		for (std::streamsize i = 0; i < n; ++i) {
			if (s[i] == '\n') {
				if (isProblem(line_, errorStream_)) problems_.add(line_);
				line_.clear();
			} else if (s[i] != '\r') {
				line_ += s[i];
			}
		}
		return written;
	}
	int sync() override {
		std::lock_guard<std::mutex> hold(lock_);
		file_.flush();
		return original_->pubsync();
	}

  private:
	std::streambuf *original_;
	std::ofstream &file_;
	std::mutex &lock_;
	Problems &problems_;
	bool errorStream_;
	std::string line_;
};

// While it lives, std::cout and std::cerr are also written to `path` (made anew); when it ends they are as they were, and the problems are summed up on the console and in the
// file: how many, and each distinct one (up to `shown`) with how many times it came.
class Capture {
  public:
	explicit Capture(const std::filesystem::path &path, std::size_t shown = 15) : path_(path), shown_(shown) {
		file_.open(path, std::ios::binary | std::ios::trunc);
		if (!file_) return;   // (no log: the run goes on as it would have)
		out_ = std::make_unique<Tee>(std::cout.rdbuf(), file_, lock_, problems_, false);
		err_ = std::make_unique<Tee>(std::cerr.rdbuf(), file_, lock_, problems_, true);
		std::cout.rdbuf(out_.get());
		std::cerr.rdbuf(err_.get());
	}
	Capture(const Capture &) = delete;
	Capture &operator=(const Capture &) = delete;
	~Capture() {
		if (!out_) return;
		std::cout.flush();
		std::cerr.flush();
		std::cout.rdbuf(out_->original());
		std::cerr.rdbuf(err_->original());
		const std::string text = summary();
		std::cout << text << std::flush;
		file_ << text;
	}
	const Problems &problems() const { return problems_; }
	bool open() const { return static_cast<bool>(out_); }

	// The closing lines: that none were reported, or how many and each distinct one.
	std::string summary() const {
		std::lock_guard<std::mutex> hold(lock_);
		std::string s = "\n";
		const int total = problems_.total();
		// (worded without "error" itself: the GUI's log shows a line that has the word in red - qt_gui/render_output_parser.h)
		if (total == 0) return s + "No problems were reported (this run's log: " + path_.string() + ").\n";
		s += std::to_string(total) + " problem(s) were reported" + (problems_.lines.size() < static_cast<std::size_t>(total) ? " (" + std::to_string(problems_.lines.size()) + " different)" : std::string()) +
			 "; the whole run is in " + path_.string() + ":\n";
		for (std::size_t i = 0; i < problems_.lines.size() && i < shown_; ++i) {
			const std::string &line = problems_.lines[i];
			const int k = problems_.count.at(line);
			s += "  " + line.substr(std::min(line.size(), line.find_first_not_of(" \t"))) + (k > 1 ? "  (x" + std::to_string(k) + ")" : std::string()) + "\n";
		}
		if (problems_.lines.size() > shown_) s += "  ... and " + std::to_string(problems_.lines.size() - shown_) + " more in the log.\n";
		return s;
	}

  private:
	std::filesystem::path path_;
	std::size_t shown_;
	std::ofstream file_;
	mutable std::mutex lock_;
	Problems problems_;
	std::unique_ptr<Tee> out_, err_;
};

}  // namespace run_log
