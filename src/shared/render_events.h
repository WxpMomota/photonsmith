#pragma once
// render_events.h -- machine-readable events from the renderer to the GUI (and anything else that launches it).
//
// The GUI used to learn what went wrong in a render only by searching ray_tracer.exe's console text for words (render_output_parser.h) and by one
// exit code. Many causes share one code: a scene file that was not downloaded and a scene that was never ported both ended as "Scene not supported on
// GPU (use CPU mode)". An event is one line on stdout that says exactly what happened, in a form that needs no guessing:
//
//     @rt {"type":"error","code":"3","reason":"scene-file-missing","file":"pbrt_scenes/bitterli/spaceship/scene-v4.pbrt","message":"..."}
//
// The line starts with "@rt " followed by a flat JSON object whose values are all strings (numbers are written as strings too: one rule for both
// ends). "type" says what the event is; "message" is always an English sentence, so a program that does not know the event shows it as it is, and a
// person reading the raw log understands it. Older renderers print no events and older GUIs show an event line like any other line, so the two sides
// can be updated in any order.
//
// Event types:
//   error    code, reason, message, plus the parameters of the reason. Printed once, just before the renderer gives up. `reason` is a stable key
//            (kebab-case) the GUI turns into its own translated text and advice; the codes are error_codes.h's.
//
// Reasons (error):
//   scene-file-missing        file (as the scene names it), folder (where it goes), scene (id)   -- a scene that needs a download
//   scene-load-failed         file, detail (the loader's sentence), scene                       -- the file is there but cannot be used
//   scene-not-supported-on-gpu  scene                                                           -- the scene was never ported to this backend
//
// Header-only and Qt-free, like the other shared headers; tests/unit/render_events_tests.cpp.

#include "pbrt_asset_check.h"

#include <cstdio>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace render_events {

inline constexpr const char* kPrefix = "@rt ";

struct Event {
	std::string type;
	std::vector<std::pair<std::string, std::string>> fields;   // in the order written; "type" is not repeated here

	Event() = default;
	explicit Event(std::string t) : type(std::move(t)) {}
	Event& add(const std::string& key, const std::string& value) { fields.emplace_back(key, value); return *this; }
	// The value of `key`, or "" when the event has none.
	std::string get(const std::string& key) const {
		for (const auto& f : fields)
			if (f.first == key) return f.second;
		return std::string();
	}
	bool has(const std::string& key) const {
		for (const auto& f : fields)
			if (f.first == key) return true;
		return false;
	}
};

namespace detail {

inline void appendEscaped(std::string& out, const std::string& s) {
	out += '"';
	for (unsigned char c : s) {
		switch (c) {
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (c < 0x20) {   // other control characters: \u00XX
				char buf[8];
				std::snprintf(buf, sizeof(buf), "\\u%04x", c);
				out += buf;
			} else {
				out += static_cast<char>(c);   // UTF-8 passes through
			}
		}
	}
	out += '"';
}

// Reads a JSON string starting at s[i] == '"'; leaves i after the closing quote. False when it is not well formed.
inline bool readString(const std::string& s, std::size_t& i, std::string& out) {
	if (i >= s.size() || s[i] != '"') return false;
	++i;
	out.clear();
	while (i < s.size()) {
		const char c = s[i++];
		if (c == '"') return true;
		if (c != '\\') { out += c; continue; }
		if (i >= s.size()) return false;
		const char e = s[i++];
		switch (e) {
		case '"': out += '"'; break;
		case '\\': out += '\\'; break;
		case '/': out += '/'; break;
		case 'n': out += '\n'; break;
		case 'r': out += '\r'; break;
		case 't': out += '\t'; break;
		case 'b': out += '\b'; break;
		case 'f': out += '\f'; break;
		case 'u': {
			if (i + 4 > s.size()) return false;
			unsigned code = 0;
			for (int k = 0; k < 4; ++k) {
				const char h = s[i + static_cast<std::size_t>(k)];
				code <<= 4;
				if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
				else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
				else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
				else return false;
			}
			i += 4;
			if (code < 0x80) out += static_cast<char>(code);
			else if (code < 0x800) { out += static_cast<char>(0xC0 | (code >> 6)); out += static_cast<char>(0x80 | (code & 0x3F)); }
			else { out += static_cast<char>(0xE0 | (code >> 12)); out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F)); }
			break;
		}
		default: return false;
		}
	}
	return false;   // no closing quote
}

inline void skipSpace(const std::string& s, std::size_t& i) {
	while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
}

}  // namespace detail

// The event as one line (without the newline): "@rt {...}".
inline std::string toLine(const Event& e) {
	std::string out = kPrefix;
	out += "{\"type\":";
	detail::appendEscaped(out, e.type);
	for (const auto& f : e.fields) {
		out += ',';
		detail::appendEscaped(out, f.first);
		out += ':';
		detail::appendEscaped(out, f.second);
	}
	out += '}';
	return out;
}

// True when `line` is an event line; fills `out`. A line that starts with the prefix but is not well formed is not an event (false), so the caller
// shows it as an ordinary line.
inline bool parseLine(const std::string& line, Event& out) {
	if (line.compare(0, 4, kPrefix) != 0) return false;
	std::size_t i = 4;
	detail::skipSpace(line, i);
	if (i >= line.size() || line[i] != '{') return false;
	++i;
	Event parsed;
	bool haveType = false;
	detail::skipSpace(line, i);
	if (i < line.size() && line[i] == '}') return false;   // no "type"
	while (true) {
		detail::skipSpace(line, i);
		std::string key, value;
		if (!detail::readString(line, i, key)) return false;
		detail::skipSpace(line, i);
		if (i >= line.size() || line[i] != ':') return false;
		++i;
		detail::skipSpace(line, i);
		if (!detail::readString(line, i, value)) return false;
		if (key == "type") { parsed.type = value; haveType = true; }
		else parsed.fields.emplace_back(key, value);
		detail::skipSpace(line, i);
		if (i >= line.size()) return false;
		if (line[i] == ',') { ++i; continue; }
		if (line[i] == '}') { ++i; break; }
		return false;
	}
	detail::skipSpace(line, i);
	if (i != line.size() || !haveType) return false;
	out = std::move(parsed);
	return true;
}

// Prints the event to stdout on a line of its own (flushed: a renderer that then exits must not lose it).
inline void send(const Event& e) {
	std::cout << "\n" << toLine(e) << std::endl;   // (a leading newline: the progress counter ends its line with a bare '\r')
}

// An error event: the code (error_codes.h), the stable reason key, an English sentence, and the reason's parameters.
inline Event errorEvent(int code, const std::string& reason, const std::string& message) {
	Event e("error");
	e.add("code", std::to_string(code)).add("reason", reason).add("message", message);
	return e;
}

inline void emitError(int code, const std::string& reason, const std::string& message,
                      const std::vector<std::pair<std::string, std::string>>& params = {}) {
	Event e = errorEvent(code, reason, message);
	for (const auto& p : params) e.add(p.first, p.second);
	send(e);
}

// A scene whose files are not on disk (a downloadable scene before "Download missing files"): says which file and where it goes. `scenePath` is the
// path the registry gave the scene. Returns true when something was missing (and an event was printed); false when the files are all there.
inline bool emitSceneFilesMissing(int code, const std::string& sceneId, const std::string& scenePath) {
	const pbrt_asset_check::Result r = pbrt_asset_check::check(scenePath);
	if (r.missing.empty()) return false;
	const std::string first = r.missing.front();
	std::string message = "The scene's files are not on this computer: " + first;
	if (r.missing.size() > 1) message += " (and " + std::to_string(r.missing.size() - 1) + " more)";
	message += ". Use \"Download missing files\" for the scene in the Settings tab.";
	emitError(code, "scene-file-missing", message, {{"scene", sceneId}, {"file", first}, {"folder", r.folder}, {"missing", std::to_string(r.missing.size())}});
	return true;
}

}  // namespace render_events
