#pragma once
// film_tts.h -- characters that speak with a real voice: a film file's line said by the computer's own text-to-speech, offline - on Windows the System.Speech voices (Settings
// > Time & language > Speech), on a Mac `say`, elsewhere espeak-ng - into a WAV kept in the film cache. The film then takes it as a recorded voice (film_file.h): the mouth
// follows its vowels (speech::mouthsOf), the music is ducked under its loudness, and its caption lasts as long as it is heard.
//
// A line's recording is named by everything it is made from (the words, the voice asked for, the pitch and the pace), so it is made once and every process - each frame's
// run, the soundtrack - reads the same one. Making one needs a speech engine started as a program of its own: the program that renders installs a `runner` for that
// (the launcher, with no console window: launcher/process.h); without one (the GUI's preview) a line that has no recording yet keeps its babble voice.
//
// The voice asked for: "system" (one of the computer's voices, chosen by the character's name among those installed, so two characters differ), or a word of a voice's name
// ("zira", "david", "samantha" - the first installed voice whose name has it).

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "film_cache.h"

namespace cartoon {
namespace tts {

// Starts a program (argv[0] and its arguments) and waits for it: its exit code, -1 when it could not be started. Installed by the program that renders.
using Runner = std::function<int(const std::vector<std::string> &)>;
inline Runner &runner() {
	static Runner r;
	return r;
}

inline std::filesystem::path folder() { return film_cache::filmDir("_voices"); }

namespace detail {
inline std::string lower(std::string s) {
	for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}
inline std::string hex(std::uint64_t h) {
	char buf[20];
	std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
	return buf;
}
inline std::uint64_t fnv(const std::string &s) {
	std::uint64_t h = 1469598103934665603ull;
	for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
	return h;
}
// A string in a PowerShell single-quoted literal.
inline std::string psQuoted(const std::string &s) {
	std::string out = "'";
	for (char c : s) out += c == '\'' ? std::string("''") : std::string(1, c);
	return out + "'";
}
inline std::string xmlEscaped(const std::string &s) {
	std::string out;
	for (char c : s) {
		if (c == '&') out += "&amp;";
		else if (c == '<') out += "&lt;";
		else if (c == '>') out += "&gt;";
		else if (c == '"') out += "&quot;";
		else out += c;
	}
	return out;
}
inline bool write(const std::filesystem::path &p, const std::string &text) {
	std::ofstream out(p, std::ios::binary | std::ios::trunc);
	out << text;
	return static_cast<bool>(out);
}
}  // namespace detail

// The computer's voices, by name (read once a process; empty without a runner or a speech engine).
inline const std::vector<std::string> &voices() {
	static std::mutex lock;
	static bool asked = false;
	static std::vector<std::string> names;
	const std::lock_guard<std::mutex> hold(lock);
	if (asked || !runner()) return names;
	asked = true;
	std::error_code ec;
	const std::filesystem::path dir = folder();
	std::filesystem::create_directories(dir, ec);
	const std::filesystem::path list = dir / ("installed-" + std::to_string(std::hash<std::string>()(film_cache::buildStamp())) + ".txt");
#ifdef _WIN32
	const std::filesystem::path script = dir / "list-voices.ps1";
	detail::write(script, "Add-Type -AssemblyName System.Speech\r\n$s = New-Object System.Speech.Synthesis.SpeechSynthesizer\r\n"
						  "$s.GetInstalledVoices() | Where-Object { $_.Enabled } | ForEach-Object { $_.VoiceInfo.Name } | Out-File -Encoding ascii " +
							  detail::psQuoted(list.string()) + "\r\n");
	const int rc = runner()({"powershell.exe", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", script.string()});
	std::filesystem::remove(script, ec);
	if (rc != 0) return names;
#elif defined(__APPLE__)
	if (runner()({"/bin/sh", "-c", "say -v '?' | sed -E 's/ {2,}.*//' > '" + list.string() + "'"}) != 0) return names;
#else
	if (runner()({"/bin/sh", "-c", "espeak-ng --voices | awk 'NR>1{print $5}' > '" + list.string() + "'"}) != 0) return names;
#endif
	std::ifstream in(list);
	for (std::string line; std::getline(in, line);) {
		while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
		if (!line.empty()) names.push_back(line);
	}
	return names;
}

// The installed voice for what is asked: "system" by `seed` among them all, else the first whose name has the word. "" when there is none.
inline std::string pick(const std::string &asked, unsigned seed) {
	const std::vector<std::string> &all = voices();
	if (all.empty()) return std::string();
	if (asked == "system") return all[seed % all.size()];
	for (const std::string &v : all)
		if (detail::lower(v).find(detail::lower(asked)) != std::string::npos) return v;
	return std::string();
}

// The recording of `text` said in the voice `asked` (see pick) for a character whose seed is `seed`, `pitch` semitones up and at `rate`: its path, made now when it is not
// made yet and a runner is installed. "" (and why) when there is none.
inline std::string wavFor(const std::string &text, const std::string &asked, unsigned seed, double pitch, double rate, std::string *why = nullptr) {
	char key[96];
	std::snprintf(key, sizeof key, "v1|%s|%u|%.2f|%.3f|", asked.c_str(), asked == "system" ? seed : 0u, pitch, rate);
	const std::filesystem::path dir = folder(), wav = dir / (detail::hex(detail::fnv(key + text)) + ".wav");
	std::error_code ec;
	if (std::filesystem::exists(wav, ec)) return wav.string();
	const auto fail = [&](const std::string &w) {
		if (why) *why = w;
		return std::string();
	};
	if (!runner()) return fail("no speech engine in this program (the line keeps its babble voice until a render makes its recording)");
	const std::string voice = pick(asked, seed);
	if (voice.empty()) return fail(voices().empty() ? "the computer has no text-to-speech voice" : "no voice of the computer is called \"" + asked + "\"");
	std::filesystem::create_directories(dir, ec);
	static std::mutex lock;   // (one at a time in a process; other processes write their own temporary file and rename it)
	const std::lock_guard<std::mutex> hold(lock);
	if (std::filesystem::exists(wav, ec)) return wav.string();
	const std::string stem = wav.stem().string() + "-" + std::to_string(std::random_device()()) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());   // (this call's own)
	const std::filesystem::path tmp = dir / (stem + ".tmp.wav");
	int rc = -1;
#ifdef _WIN32
	// SSML: the pitch as a share (System.Speech takes it), the pace as a share of the usual
	char prosody[96];
	std::snprintf(prosody, sizeof prosody, "<prosody pitch=\"%+.0f%%\" rate=\"%.0f%%\">", (std::pow(2.0, pitch / 12.0) - 1.0) * 100.0, rate * 100.0);
	const std::filesystem::path ssml = dir / (stem + ".xml"), script = dir / (stem + ".ps1");
	detail::write(ssml, "<speak version=\"1.0\" xmlns=\"http://www.w3.org/2001/10/synthesis\" xml:lang=\"en-US\">" + std::string(prosody) + detail::xmlEscaped(text) +
							"</prosody></speak>");
	detail::write(script, "Add-Type -AssemblyName System.Speech\r\n$s = New-Object System.Speech.Synthesis.SpeechSynthesizer\r\n$s.SelectVoice(" + detail::psQuoted(voice) +
							  ")\r\n$f = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(22050, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, "
							  "[System.Speech.AudioFormat.AudioChannel]::Mono)\r\n$s.SetOutputToWaveFile(" + detail::psQuoted(tmp.string()) +
							  ", $f)\r\n$s.SpeakSsml([IO.File]::ReadAllText(" + detail::psQuoted(ssml.string()) + ", [Text.Encoding]::UTF8))\r\n$s.Dispose()\r\n");
	rc = runner()({"powershell.exe", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", script.string()});
	std::filesystem::remove(ssml, ec);
	std::filesystem::remove(script, ec);
#else
	const std::filesystem::path words = dir / (stem + ".txt");
	detail::write(words, text);
#if defined(__APPLE__)
	rc = runner()({"say", "-v", voice, "-r", std::to_string(static_cast<int>(std::lround(175.0 * rate))), "--data-format=LEI16@22050", "-o", tmp.string(), "-f", words.string()});
#else
	rc = runner()({"espeak-ng", "-v", voice, "-s", std::to_string(static_cast<int>(std::lround(170.0 * rate))), "-p", std::to_string(std::clamp(static_cast<int>(50 + 4 * pitch), 0, 99)), "-w",
				   tmp.string(), "-f", words.string()});
#endif
	std::filesystem::remove(words, ec);
#endif
	if (rc != 0 || !std::filesystem::exists(tmp, ec) || std::filesystem::file_size(tmp, ec) < 1000) {
		std::filesystem::remove(tmp, ec);
		return fail("the speech engine could not say it (exit " + std::to_string(rc) + ")");
	}
	std::filesystem::rename(tmp, wav, ec);
	if (ec && !std::filesystem::exists(wav)) return fail("its recording could not be kept: " + ec.message());
	std::filesystem::remove(tmp, ec);
	return wav.string();
}

}  // namespace tts
}  // namespace cartoon
