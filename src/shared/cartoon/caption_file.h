#pragma once
// caption_file.h -- a film's words in a text file (UTF-8), so that a line is fixed, or the film given another language, without rebuilding anything: the captions are read
// when the video is made (launcher/film_captions.h sets them in a font at the frame's size; the video also gets them as an .srt file).
//
//     # A style: a name, then key=value settings (any left out keep their defaults).
//     style zh   font=simsun.ttc size=0.057 colour=1,0.965,0.886 weight=0.5
//     style en   font=palai.ttf size=0.032 colour=0.925,0.894,0.831
//     # A caption: start and end (seconds), optional settings, then its lines after '|', each "style: text".
//     caption 9.0 15.5 fade=1 centre=0.86 width=0.625 | zh: 豫章故郡，洪都新府。 | en: The ancient commandery of Yuzhang is now called Hongzhou.
//
// Settings of a style: font (a file name: the film's own fonts folder, the system's, then the program's are searched; or a full path), index (the face of a .ttc), size (a share
// of the frame's height), colour (r,g,b from 0 to 1), weight (extra pixels at 1080 lines). Settings of a caption: fade (seconds), centre (its middle, a share of the way down),
// width (at most this share of the frame's width; longer lines wrap at their spaces), gap (between lines, a share of a line's height). '#' starts a comment line; a line that
// cannot be read is reported and left out.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "cartoon_lib.h"

namespace cartoon {
namespace caption_file {

inline std::string trim(const std::string &s) {
	const std::size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos) return std::string();
	const std::size_t b = s.find_last_not_of(" \t\r\n");
	return s.substr(a, b - a + 1);
}

// "key=value" settings of a style into `line` (unknown keys are reported).
inline void applyStyle(const std::string &setting, CaptionLine &line, std::string &problem) {
	const std::size_t eq = setting.find('=');
	if (eq == std::string::npos) {
		problem = "\"" + setting + "\" is not key=value";
		return;
	}
	const std::string key = setting.substr(0, eq), value = setting.substr(eq + 1);
	if (key == "font") line.font = value;
	else if (key == "index") line.fontIndex = std::atoi(value.c_str());
	else if (key == "size") line.size = std::atof(value.c_str());
	else if (key == "weight") line.weight = std::atof(value.c_str());
	else if (key == "colour" || key == "color") {
		double c[3] = {1.0, 1.0, 1.0};
		std::stringstream in(value);
		std::string part;
		for (int i = 0; i < 3 && std::getline(in, part, ','); ++i) c[i] = std::atof(part.c_str());
		line.colour = {c[0], c[1], c[2]};
	} else problem = "unknown style setting \"" + key + "\"";
}

// The captions of a caption file's text. `problems` gets a line for each line that could not be read (with its number).
inline std::vector<Caption> parse(const std::string &text, std::vector<std::string> *problems = nullptr) {
	std::vector<Caption> captions;
	std::map<std::string, CaptionLine> styles;
	std::istringstream in(text);
	std::string raw;
	int number = 0;
	const auto report = [&](const std::string &what) {
		if (problems) problems->push_back("line " + std::to_string(number) + ": " + what);
	};
	while (std::getline(in, raw)) {
		++number;
		if (number == 1 && raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF && static_cast<unsigned char>(raw[1]) == 0xBB && static_cast<unsigned char>(raw[2]) == 0xBF)
			raw = raw.substr(3);   // (a byte-order mark)
		const std::string line = trim(raw);
		if (line.empty() || line[0] == '#') continue;
		std::istringstream words(line);
		std::string kind;
		words >> kind;
		if (kind == "style") {
			std::string name, setting, problem;
			words >> name;
			CaptionLine style;
			while (words >> setting) applyStyle(setting, style, problem);
			if (name.empty()) report("a style needs a name");
			else styles[name] = style;
			if (!problem.empty()) report(problem);
		} else if (kind == "caption") {
			const std::size_t bar = line.find('|');
			std::istringstream head(line.substr(0, bar == std::string::npos ? line.size() : bar));
			std::string word;
			head >> word;   // ("caption")
			Caption c{0.0, 0.0, std::string()};
			if (!(head >> c.t0 >> c.t1) || c.t1 <= c.t0) {
				report("a caption needs its start and end (seconds, the end after the start)");
				continue;
			}
			std::string problem;
			while (head >> word) {
				const std::size_t eq = word.find('=');
				const std::string key = word.substr(0, eq), value = eq == std::string::npos ? std::string() : word.substr(eq + 1);
				if (key == "fade") c.fade = std::atof(value.c_str());
				else if (key == "centre" || key == "center") c.centreY = std::atof(value.c_str());
				else if (key == "width") c.width = std::atof(value.c_str());
				else if (key == "gap") c.lineGap = std::atof(value.c_str());
				else problem = "unknown caption setting \"" + key + "\"";
			}
			if (!problem.empty()) report(problem);
			for (std::size_t at = bar; at != std::string::npos;) {
				const std::size_t next = line.find('|', at + 1);
				const std::string part = trim(line.substr(at + 1, next == std::string::npos ? std::string::npos : next - at - 1));
				at = next;
				if (part.empty()) continue;
				const std::size_t colon = part.find(':');
				const std::string styleName = colon == std::string::npos ? std::string() : trim(part.substr(0, colon));
				if (colon == std::string::npos || !styles.count(styleName)) {
					report("\"" + part + "\" does not start with a style defined above (\"name: text\")");
					continue;
				}
				CaptionLine l = styles[styleName];
				l.text = trim(part.substr(colon + 1));
				c.lines.push_back(l);
			}
			if (c.lines.empty()) report("a caption with no lines");
			else captions.push_back(c);
		} else {
			report("\"" + kind + "\" is neither a style nor a caption");
		}
	}
	return captions;
}

// The captions of the file at `path` ("" or a missing file: none). Lines that cannot be read are reported once, on stderr.
inline std::vector<Caption> read(const std::string &path) {
	if (path.empty()) return {};
	std::ifstream f(path, std::ios::binary);
	if (!f) return {};
	const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	std::vector<std::string> problems;
	std::vector<Caption> captions = parse(text, &problems);
	for (const std::string &p : problems) std::cerr << "Warning: " << path << ", " << p << std::endl;
	return captions;
}

}  // namespace caption_file
}  // namespace cartoon
