#include "film_info.h"

#include <cstring>

#include "scene_metadata_client.h"

namespace film_info {

const std::vector<Film> &all() {
	static const std::vector<Film> films = parse(SceneMetadataClient::filmTable().toStdString());
	return films;
}

const Film *find(const char *key) {
	if (!key) return nullptr;
	for (const Film &f : all())
		if (f.key == key) return &f;
	return nullptr;
}

}  // namespace film_info
