#pragma once
// shot_samples.h -- the shots' sample factors a film's frames are rendered with (--noise-target: noise_budget.h measures them, and every frame's run is handed the file:
// --shot-samples): each shot's start and the factor its frames' samples are multiplied by, one shot a line.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace shot_samples {

constexpr const char *kFile = "shot_samples.txt";

struct Table {
	std::vector<double> starts, factors;
	double at(double t) const {
		double f = 1.0;
		for (std::size_t i = 0; i < starts.size(); ++i)
			if (t >= starts[i] - 1e-9) f = factors[i];
		return f;
	}
};

inline bool save(const Table &t, const std::filesystem::path &path) {
	std::ofstream out(path, std::ios::binary);
	for (std::size_t i = 0; i < t.starts.size(); ++i) out << t.starts[i] << ' ' << t.factors[i] << '\n';
	return static_cast<bool>(out);
}
// The table in a file, read once a program; an empty one (every factor 1) when there is none.
inline const Table &tableAt(const std::string &path) {
	static std::mutex lock;
	static std::map<std::string, std::shared_ptr<Table>> kept;
	const std::lock_guard<std::mutex> hold(lock);
	std::shared_ptr<Table> &t = kept[path];
	if (!t) {
		t = std::make_shared<Table>();
		std::ifstream in(path);
		double s = 0.0, f = 1.0;
		while (in >> s >> f) t->starts.push_back(s), t->factors.push_back(std::clamp(f, 0.05, 20.0));
	}
	return *t;
}
// The factor of film time t in the file `path` ("": 1).
inline double factorAt(const std::string &path, double t) { return path.empty() ? 1.0 : tableAt(path).at(t); }

}  // namespace shot_samples
