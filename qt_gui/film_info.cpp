#include "film_info.h"

#include <cstring>

#include "scene_metadata_client.h"

namespace film_info {

const std::vector<Film> &all() {
	static std::vector<Film> films;
	static int read = -1;
	if (const int found = SceneMetadataClient::scenesFound(); found != read) {   // (a film file saved while the program runs)
		read = found;
		films = parse(SceneMetadataClient::filmTable().toStdString());
	}
	return films;
}

const Film *find(const char *key) {
	if (!key) return nullptr;
	for (const Film &f : all())
		if (f.key == key) return &f;
	return nullptr;
}

}  // namespace film_info
