// film_world.cpp -- a film made from a file (film_file.h): the world round its scene at a moment (sky, land, water, nature, fire, the weather) and its nature's sound
// placed where it comes from - DataFilm's members. Compiled once, in cpu_renderer.

#include "film_file.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC   // (its functions static to this file: the launcher has its own copy)
#include "../../external/stb_truetype.h"

namespace cartoon {
namespace explain {

namespace {
// The font a label's words are set in, read once: Noto Sans SC, which comes with the program (qt_gui/fonts; its Latin and its Chinese letters), so a label looks the same on
// every machine; else the system's Arial. Null when there is neither (the labels are then drawn without their cards).
const stbtt_fontinfo *labelFont() {
	static std::vector<unsigned char> data;
	static stbtt_fontinfo info{};
	static const bool ok = [] {
		namespace fs = std::filesystem;
		std::vector<fs::path> tries;
		if (const std::string own = film_cache::asset("fonts/NotoSansSC-Regular.ttf"); !own.empty()) tries.push_back(own);
		for (const char *d : {"qt_gui/fonts", "../qt_gui/fonts", "../../qt_gui/fonts", "fonts"}) tries.push_back(fs::path(d) / "NotoSansSC-Regular.ttf");
#if defined(_WIN32)
		tries.push_back(fs::path(pbrt_asset_check::environmentValue("WINDIR").empty() ? "C:/Windows" : pbrt_asset_check::environmentValue("WINDIR")) / "Fonts/arial.ttf");
#elif defined(__APPLE__)
		tries.push_back("/System/Library/Fonts/Supplemental/Arial.ttf");
		tries.push_back("/Library/Fonts/Arial.ttf");
#else
		tries.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
#endif
		std::error_code ec;
		for (const fs::path &p : tries) {
			if (!fs::exists(p, ec)) continue;
			std::ifstream f(p, std::ios::binary);
			data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
			if (!data.empty() && stbtt_InitFont(&info, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0))) return true;
		}
		std::cerr << "WARNING: no font for the films' labels (qt_gui/fonts/NotoSansSC-Regular.ttf): they are drawn without their words." << std::endl;
		return false;
	}();
	return ok ? &info : nullptr;
}
std::vector<int> codePointsOf(const std::string &s) {   // (UTF-8; a broken byte is skipped)
	std::vector<int> out;
	for (std::size_t i = 0; i < s.size();) {
		const unsigned char c = static_cast<unsigned char>(s[i]);
		const int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
		if (n < 0 || i + static_cast<std::size_t>(n) >= s.size() + (n == 0 ? 1u : 0u)) {
			++i;
			continue;
		}
		int cp = n == 0 ? c : c & (0x3F >> n);
		for (int k = 1; k <= n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0x3F);
		out.push_back(cp);
		i += static_cast<std::size_t>(n) + 1;
	}
	return out;
}
}  // namespace

std::string cardPicture(const std::string &film, const std::string &text, Vec3 colour, double &aspect) {
	const stbtt_fontinfo *font = labelFont();
	if (!font) return std::string();
	const std::vector<int> cps = codePointsOf(text);
	const float px = 56.0f, scale = stbtt_ScaleForPixelHeight(font, px);
	int ascent = 0, descent = 0, gapUnused = 0;
	stbtt_GetFontVMetrics(font, &ascent, &descent, &gapUnused);
	double width = 0.0;
	for (std::size_t k = 0; k < cps.size(); ++k) {
		int advance = 0, bearing = 0;
		stbtt_GetCodepointHMetrics(font, cps[k], &advance, &bearing);
		width += advance * scale + (k + 1 < cps.size() ? stbtt_GetCodepointKernAdvance(font, cps[k], cps[k + 1]) * scale : 0.0);
	}
	const int pad = 26, border = 3;
	const int W = static_cast<int>(std::ceil(width)) + 2 * pad, H = static_cast<int>(std::ceil((ascent - descent) * scale)) + 2 * pad - 12;
	aspect = static_cast<double>(W) / H;
	std::uint64_t h = 1469598103934665603ull;   // (the picture's name: FNV-1a of the words and the colour)
	char tint[64];
	std::snprintf(tint, sizeof tint, "|%.3f,%.3f,%.3f", colour.x, colour.y, colour.z);
	for (const char c : text + tint) h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ull;
	char name[48];
	std::snprintf(name, sizeof name, "label_%016llx", static_cast<unsigned long long>(h));
	return film_sky::picture(film, name, W, H, [&](std::vector<float> &rgb) {
		std::vector<float> ink(static_cast<std::size_t>(W) * H, 0.0f);
		double x = pad;
		const int baseline = pad - 6 + static_cast<int>(std::lround(ascent * scale));
		for (std::size_t k = 0; k < cps.size(); ++k) {
			int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
			const float shift = static_cast<float>(x - std::floor(x));
			stbtt_GetCodepointBitmapBoxSubpixel(font, cps[k], scale, scale, shift, 0.0f, &x0, &y0, &x1, &y1);
			const int gw = x1 - x0, gh = y1 - y0;
			if (gw > 0 && gh > 0) {
				std::vector<unsigned char> glyph(static_cast<std::size_t>(gw) * gh);
				stbtt_MakeCodepointBitmapSubpixel(font, glyph.data(), gw, gh, gw, scale, scale, shift, 0.0f, cps[k]);
				for (int gy = 0; gy < gh; ++gy)
					for (int gx = 0; gx < gw; ++gx) {
						const int X = static_cast<int>(std::floor(x)) + x0 + gx, Y = baseline + y0 + gy;
						if (X >= 0 && X < W && Y >= 0 && Y < H) ink[static_cast<std::size_t>(Y) * W + X] = std::max(ink[static_cast<std::size_t>(Y) * W + X], glyph[static_cast<std::size_t>(gy) * gw + gx] / 255.0f);
					}
			}
			int advance = 0, bearing = 0;
			stbtt_GetCodepointHMetrics(font, cps[k], &advance, &bearing);
			x += advance * scale + (k + 1 < cps.size() ? stbtt_GetCodepointKernAdvance(font, cps[k], cps[k + 1]) * scale : 0.0);
		}
		const float back[3] = {0.012f, 0.016f, 0.03f}, edge[3] = {static_cast<float>(colour.x), static_cast<float>(colour.y), static_cast<float>(colour.z)};
		for (int y = 0; y < H; ++y)
			for (int xx = 0; xx < W; ++xx) {
				const bool rim = xx < border || y < border || xx >= W - border || y >= H - border;
				const float a = ink[static_cast<std::size_t>(y) * W + xx];
				for (int c = 0; c < 3; ++c) rgb[(static_cast<std::size_t>(y) * W + xx) * 3 + c] = rim ? edge[c] : back[c] + (0.95f - back[c]) * a;
			}
	});
}

}  // namespace explain
}  // namespace cartoon

namespace cartoon {
namespace film_file {

// Round 9's spectacles in a frame at film time t (nature's `nt`): tornadoes (film_wind.h), ice (film_ice.h), living light (film_glow.h) and the sea's giants
// (film_giants.h). `exposure`: the frame's (living light is divided by it); `natureAt`: nature's time at a film time.
static std::string spectacleText(const Spec &spec, const std::string &header, double t, double nt, double exposure, const std::function<double(double)> &natureAt,
							  const terrain::Weather &weather) {
	std::string out;
	const double light = 1.0 / std::max(1.0, exposure);   // (as bright on the screen however far the night camera has opened up)
	water::Sea lit = spec.seas.empty() ? water::Sea() : spec.seas[0];
	lit.glow = lit.glow * light;
	if (spec.closeup.on && spec.hasTerrain) {   // the ground close up (film_ground.h): in front of the camera, with the footprints on it
		Vec3 eye, target;
		double fov = 40.0;
		film_subjects::cameraOf(header, eye, target, fov);
		const Vec3 e{-eye.x, eye.y, eye.z}, f = normalize(Vec3{-target.x + eye.x, 0.0, target.z - eye.z} + Vec3{1e-9, 0.0, 0.0});
		const std::shared_ptr<const terrain::Field> land = terrain::fieldOf(spec.terrain);
		const auto groundAt = [&](double x, double z) { return land->inside(x, z) ? land->heightAt(x, z) : spec.terrain.y; };
		const std::string &tk = spec.terrain.kind;
		const Vec3 mid = e + f * (0.35 * spec.closeup.size);   // (on a coast, sand on the beach only: within a few metres of the sea's level)
		const bool beach = tk == "coast" && !spec.seas.empty() && groundAt(mid.x, mid.z) < spec.seas[0].level + 2.5;
		const std::string kind = weather.snow > 0.3 ? "snow" : (tk == "dunes" || beach || spec.biome == "desert") ? "sand" : "earth";
		const bool soft = kind != "earth" || weather.wet > 0.5;
		std::vector<ground::Print> prints;
		for (const ground::Track &k : spec.tracks)
			for (ground::Print p : ground::trackPrints(k, t)) p.depth *= soft ? 1.0 : 0.15, prints.push_back(p);
		for (const wildlife::Herd &h : spec.herds) {   // a herd's prints along the ways its animals have come over the last half minute
			const animals::Species *sp = animals::species(h.kind);
			if (!sp) continue;
			for (int i = 0; i < h.count; ++i)
				for (int k = 0; k < 90; ++k) {
					const double tb = t - 0.35 * k;
					if (tb < 0.0) break;
					const wildlife::Animal a = wildlife::animalAt(h, *sp, i, natureAt(tb), land.get());
					if (a.clip.empty() || std::hypot(a.at.x - e.x, a.at.z - e.z) > spec.closeup.size) continue;
					const double side = k % 2 ? 1.0 : -1.0, off = 0.12 * sp->length / 0.95;
					for (const double along : {0.35 * sp->length, -0.35 * sp->length}) {
						ground::Print p;
						p.x = a.at.x + a.dir.x * along - a.dir.z * side * off, p.z = a.at.z + a.dir.z * along + a.dir.x * side * off;
						p.dx = a.dir.x, p.dz = a.dir.z, p.kind = h.kind == "wolves" ? "paws" : "hooves", p.depth = (soft ? 0.03 : 0.004) * (h.kind == "horses" ? 1.3 : 1.0);
						prints.push_back(p);
					}
				}
		}
		const double shore = (!spec.seas.empty() && tk == "coast") ? spec.seas[0].level : -1e30;
		out += ground::closeupText(spec.key.empty() ? std::string("film") : spec.key, spec.closeup, e.x + f.x * 0.35 * spec.closeup.size, e.z + f.z * 0.35 * spec.closeup.size, kind, prints, groundAt,
								   [&](double x, double z) { return terrain::colourAt(*land, x, z); }, kind == "snow" ? weather.snow : 0.0, weather.wet, shore, spec.terrain.key(), spec.terrain.seed);
	}
	if (spec.hasSky && std::any_of(spec.falls.begin(), spec.falls.end(), [](const water::Fall &f) { return f.rainbow > 0.0 && !f.frozen; })) {   // rainbows in the falls' spray
		Vec3 eye, target;
		double fov = 40.0, el = 0.0, facing = 0.0;
		film_subjects::cameraOf(header, eye, target, fov);
		dressing::sunAt(spec.sky, dressing::hourAt(spec.sky, t, spec.storyEnd), el, facing);
		const double d2r = kPi / 180.0;
		const Vec3 sun{std::cos(el * d2r) * std::sin(facing * d2r), std::sin(el * d2r), std::cos(el * d2r) * std::cos(facing * d2r)};
		for (const water::Fall &f : spec.falls)
			if (f.rainbow > 0.0 && !f.frozen)
				out += caustics::mistbowText({-eye.x, eye.y, eye.z}, sun, 3.0 * smoothstep(0.0, 6.0, el) * (1.0 - 0.9 * spec.storm.levelAt(t)),
											 water::fallFoot(f) + Vec3{0.0, 0.15 * f.drop, 0.0}, 0.3 * f.drop + 0.6 * f.width, f.rainbow);
	}
	if (!spec.tornadoes.empty()) {   // tornadoes (film_wind.h)
		const std::shared_ptr<const terrain::Field> ground = spec.hasTerrain ? terrain::fieldOf(spec.terrain) : nullptr;
		const auto groundAt = [&](double x, double z) { return ground && ground->inside(x, z) ? ground->heightAt(x, z) : (ground ? spec.terrain.y : 0.0); };
		for (std::size_t i = 0; i < spec.tornadoes.size(); ++i) out += wind::tornadoText(spec.tornadoes[i], static_cast<int>(i), t, nt, groundAt, spec.seas.empty() ? -1e30 : spec.seas[0].level);
	}
	if (!spec.glaciers.empty() || !spec.icebergs.empty() || spec.seaIce.on) {   // ice (film_ice.h)
		const std::shared_ptr<const terrain::Field> ground = spec.hasTerrain ? terrain::fieldOf(spec.terrain) : nullptr;
		const auto groundAt = [&](double x, double z) { return ground && ground->inside(x, z) ? ground->heightAt(x, z) : (ground ? spec.terrain.y : 0.0); };
		const double level = spec.seas.empty() ? -1e30 : spec.seas[0].level;
		for (std::size_t i = 0; i < spec.glaciers.size(); ++i) {
			const ice::Glacier &g = spec.glaciers[i];
			double cut = 0.0;   // (each calving takes a slab off the front: the slab falls, the front stands back by its depth)
			for (double c : g.calving) {
				if (t < c) {
					out += ice::slabText(g, cut, -1.0, groundAt, level, nt);   // (the next slab, still standing)
					break;
				}
				if (t < c + 60.0) out += ice::slabText(g, cut, t - c, groundAt, level, nt);
				if (t >= c + ice::kCalveFall && !spec.seas.empty()) {
					const Vec3 at = ice::calveSplash(g, cut, groundAt, level);
					const double since = t - c - ice::kCalveFall, H = g.thickness();
					out += giants::sprayText(at, since, 3000, 0.55 * std::sqrt(9.81 * H), 0.004 * g.width, 1201 + static_cast<int>(i) * 31 + static_cast<int>(cut), false, lit.glowing());
					out += giants::foamText(lit, at, since, 0.5 * g.width, nt, 1301 + static_cast<int>(cut));
				}
				cut += ice::slabDepth(g);
			}
			out += ice::glacierText(g, cut + (g.calving.empty() || t >= g.calving.back() ? 0.0 : ice::slabDepth(g)), groundAt, level);
		}
		for (const ice::Iceberg &b : spec.icebergs) out += ice::bergText(b, nt, level > -1e29 ? level : 0.0);
		if (spec.seaIce.on && !spec.seas.empty()) {
			Vec3 eye, target;
			double fov = 40.0;
			film_subjects::cameraOf(header, eye, target, fov);
			out += ice::seaIceText(spec.seaIce, spec.seas[0], nt, {-eye.x, eye.y, eye.z});
		}
	}
	if (!spec.fireflies.empty() || !spec.fungi.empty()) {   // living light (film_glow.h)
		const std::shared_ptr<const terrain::Field> ground = spec.hasTerrain ? terrain::fieldOf(spec.terrain) : nullptr;
		const auto groundAt = [&](double x, double z) { return ground && ground->inside(x, z) ? ground->heightAt(x, z) : (ground ? spec.terrain.y : 0.0); };
				for (const glow::Fireflies &f : spec.fireflies) out += glow::firefliesText(f, nt, groundAt, light);
		for (const glow::Fungi &f : spec.fungi) out += glow::fungiText(f, nt, groundAt, light);
	}
	if ((!spec.whales.empty() || !spec.pods.empty()) && !spec.seas.empty()) {   // the sea's giants (film_giants.h)
		water::Sea sea = spec.seas[0];
		sea.glow = sea.glow * (light);
		out += giants::giantsText(spec.whales, spec.pods, sea, spec.hasUnderwater, t, nt, natureAt);
	}
	return out;
}

// Round 10's universe in a frame at film time t (nature's `nt`): the set in space (film_space.h), the bodies in it (film_cosmos.h) and the craft and people travelling
// among them (film_voyage.h). (The planets are worldAt's: an eclipse needs them all.)
static std::string universeText(const Spec &spec, const std::string &header, double t, double nt) {
	std::string out;
	const std::string film = spec.key.empty() ? std::string("film") : spec.key;
	if (spec.space.on) {   // (the set in space: film_space.h)
		Vec3 eye, target;
		double fov = 40.0;
		film_subjects::cameraOf(header, eye, target, fov);
		out += space::spaceText(film, spec.space, space::narrowOnTheSun(spec.space, eye, target, fov), eye);
		if (spec.space.planetshine > 0.0 && spec.space.sun)   // (film_voyage.h)
			out += cosmos::planetshineText(spec.planets, nt, {-eye.x, eye.y, eye.z}, space::sunDirection(spec.space), spec.space.planetshine * spec.space.sunStrength);
	}
	if (!spec.blackHoles.empty()) {   // (film_cosmos.h: drawn for where the camera is)
		Vec3 eye, target;
		double fov = 40.0;
		film_subjects::cameraOf(header, eye, target, fov);
		for (const cosmos::BlackHole &b : spec.blackHoles) out += cosmos::blackHoleText(b, nt, {-eye.x, eye.y, eye.z});
	}
	for (std::size_t k = 0; k < spec.shots.size(); ++k) {   // a jump to hyperspace under way (film_voyage.h)
		const Shot &shot = spec.shots[k];
		const double end = k + 1 < spec.shots.size() ? spec.shots[k + 1].start : spec.length;
		if (shot.spaceCam.kind == cosmos::SpaceCam::Hyperspace && t >= shot.start && t < end) out += cosmos::hyperspaceText(shot.spaceCam, (t - shot.start) / std::max(1e-6, end - shot.start));
	}
	for (const cosmos::Astronaut &a : spec.astronauts) {   // (film_voyage.h: a tether to a ship goes to where the ship is now)
		Vec3 to = a.tether;
		for (const cosmos::Ship &sh : spec.ships)
			if (sh.name == a.tetherShip) {
				const Vec3 p = transformPoint(cosmos::shipPlacement(sh, nt), {-0.2, 0.0, 0.0});
				to = {-p.x, p.y, p.z};
			}
		out += cosmos::astronautText(a, nt, a.tethered ? &to : nullptr);
	}
	for (const cosmos::Ship &sh : spec.ships) out += cosmos::shipText(sh, nt);   // (film_voyage.h)
	if (!spec.battle.empty()) out += battle::battleText(spec.battle, spec.ships, nt);   // (film_battle.h)
	for (const cosmos::Descent &d : spec.descents) out += cosmos::descentText(d, nt);
	for (const cosmos::Remnant &m : spec.remnants) out += cosmos::remnantText(film, m, nt);   // (film_cosmos.h)
	for (const cosmos::Belt &b : spec.belts) out += cosmos::beltText(b, nt);
	for (const cosmos::Comet &c : spec.comets) out += cosmos::cometText(film, c, nt, space::sunDirection(spec.space) * -1.0);
	for (const cosmos::Star &st : spec.stars) out += cosmos::starText(film, st, nt);
	for (const cosmos::Galaxy &g : spec.galaxies) out += cosmos::galaxyText(film, g);
	for (const cosmos::Nebula &n : spec.nebulae) out += cosmos::nebulaText(film, n);
	return out;
}

// Under the sea (film_underwater.h, round 14's film_undersea.h, film_deepsea.h, film_seabed.h): the water round the camera - the camera in it, or at its surface half above
// it, or above it - the light coming down through it and the mirror beyond Snell's window, what drifts, grows, swims and bubbles in it, and the seabed's places. `out` is the
// frame so far (its Camera line: the water's medium goes before it); the look's words for a camera at the waterline are added last of all (worldAt).
static std::string underTheSea(const Spec &spec, std::string out, const std::string &header, double t, double nt, const std::vector<school::School> &schools) {
	Vec3 eye, target;
	double fov = 40.0;
	film_subjects::cameraOf(header, eye, target, fov);
	const Vec3 eyeP{-eye.x, eye.y, eye.z}, targetP{-target.x, target.y, target.z};   // (pbrt's)
	const water::Sea &sea = spec.seas[0];
	const undersea::Waterline wl = undersea::waterlineOf(sea, nt, eyeP);
	const std::size_t camera = out.find("Camera \"");
	if (camera != std::string::npos) {   // (a sphere so big its top is the surface - or, at the waterline, the plane through the lens where the line crosses its dome: the water below it)
		const Vec3 centre = wl.where == undersea::Waterline::At ? undersea::waterlineCentre(eyeP, targetP, wl.elevation, 3000.0) : Vec3{eye.x, sea.level - 3000.0, eye.z};
		out = out.substr(0, camera) + underwater::beforeCamera(spec.underwater, wl.where == undersea::Waterline::Under) + out.substr(camera) + underwater::boundary(centre, 3000.0);
	}
	const std::shared_ptr<const terrain::Field> ground = spec.hasTerrain ? terrain::fieldOf(spec.terrain) : nullptr;
	const double floorY = sea.level - 15.0;
	const auto groundAt = [&](double x, double z) { return ground && ground->inside(x, z) ? ground->heightAt(x, z) : (ground ? spec.terrain.y : floorY); };
	const std::string film = spec.key.empty() ? std::string("film") : spec.key;
	if (!ground) {   // (no terrain: a plain sandy floor 15 m down)
		char buf[320];
		std::snprintf(buf, sizeof buf, "AttributeBegin\n  Material \"diffuse\" \"rgb reflectance\" [ 0.5 0.44 0.32 ]\n  Shape \"trianglemesh\" \"integer indices\" [ 0 1 2 0 2 3 ] \"point3 P\" [ -2000 %.3f -2000  2000 %.3f -2000  2000 %.3f 2000  -2000 %.3f 2000 ]\nAttributeEnd\n",
					  floorY, floorY, floorY, floorY);
		out += buf;
	}
	const double bedDepth = spec.tracedCaustics ? std::max(0.0, sea.level - groundAt(-eye.x, eye.z)) : -1.0;   // (`caustics traced`: the net for the bed under the camera)
	// (round 15: under a sky the net and its shafts are the sun's caustic slide - film_sealight.h, underTheSeaLight - and the sand under it is plain; traced, or with no
	// sky, they are painted as before)
	const bool slide = !spec.tracedCaustics && spec.hasSky;
	out += underwater::causticText(film, nt, -eye.x, eye.z, groundAt, bedDepth, spec.underwater.ripples, slide ? 1e9 : sea.level - groundAt(-eye.x, eye.z));
	if (!slide) out += underwater::shaftsText(film, nt, eyeP, sea.level, bedDepth);
	const Vec3 flow = spec.current.velocity(nt), drift = spec.current.drift(nt);
	if (wl.where != undersea::Waterline::Above) {
		out += underwater::snowText(t, eye, drift, wl.where == undersea::Waterline::At ? wl.surface : 1e30);
		out += undersea::windowText(sea, nt, eyeP);   // (round 14: the mirror beyond Snell's window)
	}
	if (spec.underwater.lamp > 0.0) out += undersea::lampText(spec.underwater.lamp, eyeP, targetP, spec.underwater.lampBlue) + undersea::backscatterText(t, eye, drift);
	// what stands on the seabed stands on its sand (the caustics' decal, 4 cm over the ground): a starfish or a nudibranch under it would be hidden
	const auto bedAt = [&](double x, double z) { return groundAt(x, z) + 0.04; };
	for (const underwater::Kelp &k : spec.kelps) out += underwater::kelpText(k, nt, bedAt, flow);
	for (const underwater::Coral &c : spec.corals) out += underwater::coralText(c, bedAt);
	out += undersea::text(undersea::lifted(spec.undersea, groundAt), nt, sea.level, spec.current, eyeP);
	out += deepsea::text(film, deepsea::lifted(spec.deep, bedAt), nt, sea.level, spec.current);
	out += seabed::text(film, spec.seabed, nt, bedAt, flow, eyeP);
	// round 15: divers, schools, the big animals, reefs, small creatures, kelp forests and the night (film_diver.h, film_school.h, film_reef.h)
	const std::vector<diver::Diver> divers = diver::lifted(spec.divers, groundAt);
	for (const diver::Diver &d : divers) out += diver::diverText(d, nt, sea.level, spec.current);
	for (std::size_t i = 0; i < schools.size(); ++i) out += school::schoolText(schools[i], static_cast<int>(i), nt);
	const std::vector<school::BigFish> big = school::lifted(spec.bigFish, groundAt);
	for (const school::BigFish &b : big) out += school::bigText(b, nt);
	if (spec.reefLife.any()) {
		std::vector<std::function<Vec3(double)>> moving;   // (what lights the water up at night: divers, swimmers, the big animals, a school's leading fish)
		if (spec.reefLife.night.on) {
			for (const diver::Diver &d : divers) moving.push_back([d](double tt) { return diver::diverAt(d, tt) + Vec3{0.0, -0.1, 0.0}; });
			const deepsea::Life life = deepsea::lifted(spec.deep, groundAt);
			for (const deepsea::Swimmers &g : life.swimmers)
				for (int i = 0; i < g.count; ++i)
					moving.push_back([g, i, &sea](double tt) {
						Vec3 f;
						double roll = 0.0;
						return deepsea::swimmerAt(g, i, tt, f, roll, sea.level);
					});
			for (const school::BigFish &b : big)
				for (int i = 0; i < b.count; ++i)
					moving.push_back([b, i](double tt) {
						Vec3 f;
						return school::bigAt(b, i, tt, f);
					});
			for (const school::School &sc : schools)
				for (int i = 0; i < std::min(sc.count, 60); ++i) moving.push_back([&sc, i](double tt) { return school::fishAt(sc, i, tt); });
		}
		out += reef::text(film, spec.reefLife, spec.corals, nt, sea.level, bedAt, spec.current, moving);
	}
	return out;
}
// The light under the sea once the frame has its sun (round 15, film_sealight.h): the caustic net over everything in the water, the sun's glints up through the surface
// with their stars, rain landing on the surface seen from below; and the look's words for a camera at the waterline (film_undersea.h).
static std::string underTheSeaLight(const Spec &spec, std::string out, const std::string &header, double t, double nt, double seabed) {
	Vec3 eye, target;
	double fov = 40.0;
	film_subjects::cameraOf(header, eye, target, fov);
	const Vec3 eyeP{-eye.x, eye.y, eye.z}, targetP{-target.x, target.y, target.z};
	const water::Sea &sea = spec.seas[0];
	const undersea::Waterline wl = undersea::waterlineOf(sea, nt, eyeP);
	if (wl.where != undersea::Waterline::Above) {
		if (!spec.tracedCaustics && spec.hasSky) out = sealight::withCaustics(out, spec.key.empty() ? std::string("film") : spec.key, nt, eyeP, sea.level, sea.level - seabed);
		const std::string glints = sealight::glintsText(out, nt, eyeP, sea.level);
		if (!glints.empty()) out = night::withWords(out + glints, sealight::glintLook());
		double rain = 0.0;   // (a `weather rain` line, or a rainy storm, as heavy as it is now)
		for (const particles::Weather &w : spec.weather)
			if (w.kind == "rain" && t >= w.t0 && t <= w.t1) rain = std::max(rain, std::min(1.0, w.amount));
		if (spec.storm.on && spec.storm.kind == "rain") rain = std::max(rain, std::min(1.0, spec.storm.levelAt(t)));
		out += sealight::rainText(rain, nt, eyeP, sea.level);
	}
	if (wl.where == undersea::Waterline::At) out = night::withWords(out, undersea::waterlineLook(eyeP, targetP, fov, wl.elevation, t));
	return out;
}
double DataFilm::seabedAt(double x, double z) const {
	const std::shared_ptr<const terrain::Field> ground = spec.hasTerrain ? terrain::fieldOf(spec.terrain) : nullptr;
	const double floorY = spec.seas.empty() ? 0.0 : spec.seas[0].level - 15.0;
	return ground && ground->inside(x, z) ? ground->heightAt(x, z) : (ground ? spec.terrain.y : floorY);
}
const std::vector<school::School> &DataFilm::schoolsSwum() const {
	std::lock_guard<std::mutex> hold(swum_.lock);
	if (swum_.done) return swum_.schools;
	swum_.done = true;
	swum_.schools = spec.schools;
	const std::vector<diver::Diver> divers = diver::lifted(spec.divers, [this](double x, double z) { return seabedAt(x, z); });
	const auto avoid = [this, &divers](double t) {   // (the camera and the divers: the school keeps clear of them)
		std::vector<Vec3> out;
		Vec3 eye, target;
		double fov = 40.0;
		if (film_subjects::cameraOf(headerAt(std::min(t, spec.length)), eye, target, fov)) out.push_back({-eye.x, eye.y, eye.z});
		for (const diver::Diver &d : divers) out.push_back(diver::diverAt(d, t));
		return out;
	};
	for (school::School &sc : swum_.schools) {
		sc.at.y += seabedAt(sc.at.x, sc.at.z);
		school::prepare(sc, natureAt(spec.length) + 1.0, avoid);
	}
	return swum_.schools;
}
// How much more `exposure auto` opens up for a camera under the water, by its depth (film_undersea.h depthExposure); 1 for any other.
static double underTheSeaExposure(const Spec &spec, const std::string &header) {
	if (!spec.hasUnderwater || !spec.exposureAuto || spec.seas.empty()) return 1.0;
	Vec3 eye, target;
	double fov = 40.0;
	if (!film_subjects::cameraOf(header, eye, target, fov)) return 1.0;
	return undersea::depthExposure(spec.underwater, spec.seas[0].level - eye.y);
}

std::string DataFilm::worldAt(const std::string &given, double t) const {
	const double nt = natureAt(t);   // (nature's time: the clouds, the water, the aurora, the wind, the animals; `timelapse`, `slowmo`)
	const std::string header = spec.hasUnderwater ? undersea::floated(given, spec.seas[0], nt, !spec.shots.empty() && spec.shots[shotAt(t)].waterline) : given;   // (a camera at the surface floats on it: film_undersea.h)
	std::string out = film_camera::exposed(header, exposureAt(t) * underTheSeaExposure(spec, header));
	if (spec.hasUnderwater) out = underTheSea(spec, out, header, t, nt, schoolsSwum());   // under the sea: the water round the camera, the light coming down through it, what lives in it
	if (!spec.fires.empty() || !spec.lavas.empty() || !spec.volcanoes.empty() || !spec.wildfires.empty()) {   // fire, lava and smoke (film_fire.h)
		const std::shared_ptr<const terrain::Field> ground = spec.hasTerrain ? terrain::fieldOf(spec.terrain) : nullptr;
		const auto groundAt = [&](double x, double z) { return ground && ground->inside(x, z) ? ground->heightAt(x, z) : (ground ? spec.terrain.y : 0.0); };
		const std::string film = spec.key.empty() ? std::string("film") : spec.key;
		for (std::size_t i = 0; i < spec.fires.size(); ++i) {
			const fire::Fire &f = spec.fires[i];
			if (t < f.t0 || t > f.t1) continue;
			out += fire::flameText("flame_" + std::to_string(i), f.at, f.size, nt, f.seed);
			const Vec3 drift = spec.wind.on ? spec.wind.dir() * (4.0 * f.size * std::min(1.5, spec.wind.speed / 6.0)) : Vec3{0.0, 0.0, 0.0};   // (bent away by the wind)
			if (f.smoke) out += fire::smokeText("smoke_" + std::to_string(i), f.at + Vec3{0.0, 1.25 * f.size, 0.0}, 2.5 * f.size, 8.0 * f.size, nt, f.seed, 0.6, drift);
		}
		const double lavaGlow = 3.0 / std::pow(std::max(1.0, exposureAt(t)), 0.85);   // (exposed for the lava as the night camera opens up: orange, not white)
		for (const fire::Lava &l : spec.lavas) out += fire::lavaText(film, l, nt, lavaGlow);
		for (std::size_t i = 0; i < spec.volcanoes.size(); ++i) {   // an eruption: the plume of ash over the crater, the crater's glow
			const fire::Volcano &v = spec.volcanoes[i];
			if (t < v.t0 || t > v.t1) continue;
			const double top = groundAt(v.x, v.z);
			const double wa = v.wind * kPi / 180.0;
			if (v.column > 0.0)   // a column climbing kilometres, spreading into its umbrella
				out += fire::ashColumnText("ashcolumn_" + std::to_string(i), {v.x, v.rimY, v.z}, 0.15 * v.radius, v.column, t - v.t0, nt, {std::sin(wa), 0.0, std::cos(wa)}, 31 + static_cast<int>(i),
										   3e4 * v.height / 400.0);
			else
				out += fire::smokeText("plume_" + std::to_string(i), {v.x, top + 40.0, v.z}, 1.1 * v.radius, 2.4 * v.height, nt, 31 + static_cast<int>(i), 3.0, {0.4 * v.radius, 0.0, 0.0});
			for (std::size_t k = 0; k < v.flows.size(); ++k)   // pyroclastic flows racing down its flanks (their grids end below the column's or the plume's)
				out += fire::flowText("pyroflow_" + std::to_string(i) + "_" + std::to_string(k), v.flows[k], t - v.flows[k].t, nt, groundAt, v.column > 0.0 ? v.rimY : top + 40.0, v.radius,
									  v.height, 91 + static_cast<int>(k));
			char buf[200];
			std::snprintf(buf, sizeof buf, "LightSource \"point\" \"point3 from\" [ %.3f %.3f %.3f ] \"rgb I\" [ %.1f %.1f %.1f ]\n", v.x, top - 15.0, v.z, 6e4 * v.height / 400.0, 2.2e4 * v.height / 400.0,
						  4e3 * v.height / 400.0);   // (down in the crater: it lights the plume's base from below, not its whole height)
			out += buf;
		}
		for (std::size_t i = 0; i < spec.wildfires.size(); ++i) {   // the front: a line of flames across the land, sweeping along with the wind, smoke rising behind it
			const fire::Wildfire &w = spec.wildfires[i];
			if (t < w.t0 || t > w.t1) continue;
			const double a = w.wind * kPi / 180.0, ux = std::sin(a), uz = std::cos(a);
			const double cx = 0.5 * (w.x0 + w.x1), cz = 0.5 * (w.z0 + w.z1), span = 0.5 * (std::abs(ux) * (w.x1 - w.x0) + std::abs(uz) * (w.z1 - w.z0));
			const double along = -span + 2.0 * span * (t - w.t0) / std::max(1e-6, w.t1 - w.t0);
			const double width = std::abs(uz) * (w.x1 - w.x0) + std::abs(ux) * (w.z1 - w.z0);
			const int flames = std::clamp(static_cast<int>(width / 10.0), 2, 48);
			for (int k = 0; k < flames; ++k) {
				const double across = (k + 0.5) / flames - 0.5;
				const double wob = 6.0 * (nature::noise2(k * 0.7, t * 0.2, 41) - 0.5);
				const double x = cx + ux * (along + wob) + uz * across * width, z = cz + uz * (along + wob) - ux * across * width;
				const double size = 7.0 + 5.0 * hash01(k, static_cast<int>(i) + 51);
				out += fire::flameText("wild_" + std::to_string(i) + "_" + std::to_string(k), {x, groundAt(x, z), z}, size, nt + k * 0.37, 61 + k, 14);
			}
			const int plumes = 5;
			for (int k = 0; k < plumes; ++k) {   // smoke behind the front: plumes of their own sizes and places, leaning with the wind
				const double across = (k + 0.2 + 0.6 * hash01(k, 81)) / plumes - 0.5, back = 30.0 + 40.0 * hash01(k, 83), size = 0.7 + 0.6 * hash01(k, 85);
				const double x = cx + ux * (along - back) + uz * across * width, z = cz + uz * (along - back) - ux * across * width;
				out += fire::smokeText("wildsmoke_" + std::to_string(i) + "_" + std::to_string(k), {x, groundAt(x, z) + 16.0, z}, 0.2 * width * size, 0.45 * width * size, nt, 71 + k, 2.0,
									   {ux * 0.25 * width * size, 0.0, uz * 0.25 * width * size});
			}
		}
	}
	if (spec.fog.on) {   // fog in the valleys: the camera's own medium, lying in a layer (height_fog.h); lifting, it thins away and sinks
		const double lift = spec.fog.liftT1 > spec.fog.liftT0 ? smoothstep(spec.fog.liftT0, spec.fog.liftT1, t) : 0.0;
		const double density = spec.fog.density * (1.0 - lift);
		const std::size_t camera = out.find("Camera \"");
		if (density > 1e-6 && camera != std::string::npos) {
			char medium[400];
			std::snprintf(medium, sizeof medium,
						  "MakeNamedMedium \"valleyfog\" \"string type\" [ \"heightfog\" ] \"rgb sigma_a\" [ %.6f %.6f %.6f ] \"rgb sigma_s\" [ %.6f %.6f %.6f ] \"float g\" [ 0.55 ] "
						  "\"float top\" [ %.4f ] \"float thickness\" [ %.4f ]\nMediumInterface \"\" \"valleyfog\"\n",
						  0.02 * density, 0.02 * density, 0.02 * density, density, density, 1.03 * density, spec.fog.top - lift * 3.0 * spec.fog.thickness, spec.fog.thickness);
			out = out.substr(0, camera) + medium + out.substr(camera);
		}
	}
	if (spec.storm.on && !spec.air.on && !spec.fog.on && !spec.hasMist && !spec.hasUnderwater) {   // the storm's murk round the camera: a layer of rain, snow or dust
		const double level = spec.storm.levelAt(t);
		const std::size_t camera = out.find("Camera \"");
		if (level > 0.01 && camera != std::string::npos) {
			Vec3 eye, target;
			double fov = 40.0;
			film_subjects::cameraOf(header, eye, target, fov);
			const std::shared_ptr<const terrain::Field> land = spec.hasTerrain ? terrain::fieldOf(spec.terrain) : nullptr;
			const double ground = land ? land->heightAt(-eye.x, eye.z) : 0.0;
			const double d = level * (spec.storm.kind == "rain" ? 0.0025 : spec.storm.kind == "snow" ? 0.035 : 0.05);
			const Vec3 tint = spec.storm.kind == "sand" ? Vec3{1.0, 0.74, 0.46} : Vec3{0.95, 0.97, 1.0};
			const double absorb = spec.storm.kind == "sand" ? 0.25 * d : 0.02 * d;
			char medium[480];
			std::snprintf(medium, sizeof medium,
						  "MakeNamedMedium \"storm\" \"string type\" [ \"heightfog\" ] \"rgb sigma_a\" [ %.6f %.6f %.6f ] \"rgb sigma_s\" [ %.6f %.6f %.6f ] \"float g\" [ 0.5 ] "
						  "\"float top\" [ %.3f ] \"float thickness\" [ 25 ] \"float radius\" [ 400 ]\nMediumInterface \"\" \"storm\"\n",
						  absorb, absorb, absorb * 1.2, d * tint.x, d * tint.y, d * tint.z, ground + 120.0);
			out = out.substr(0, camera) + medium + out.substr(camera);
		}
	}
	if (spec.air.on) {   // the air: the camera's own medium, the atmosphere's scattering thinning with height (height_fog.h's layer far above its "top": an exponential)
		const std::size_t camera = out.find("Camera \"");
		if (camera != std::string::npos) out = out.substr(0, camera) + airMedium() + out.substr(camera);
	}
	if (spec.hasMist) {   // a mist round the camera (nature_lib.h: the camera's own medium, ended by a sphere round it, so the sun and sky outside light it)
		Vec3 eye, target;
		double fov = 40.0;
		film_subjects::cameraOf(header, eye, target, fov);
		nature::Mist m = spec.mist;
		m.centre = eye;
		const std::size_t camera = out.find("Camera \"");
		if (camera != std::string::npos) out = out.substr(0, camera) + nature::mistBeforeCamera(m) + out.substr(camera) + nature::mistBoundary(m);
	}
	out += universeText(spec, header, t, nt) + aboardAt(header, t, nt);   // (round 10: space and what is in it; round 11: what is aboard, footprints, drawings)
	if (!spec.planets.empty()) {   // planets and their moons, each where it is now (film_planets.h); an eclipse when one covers the sun
		const std::string film = spec.key.empty() ? std::string("film") : spec.key;
		for (std::size_t i = 0; i < spec.planets.size(); ++i) {
			planets::Planet p = spec.planets[i];
			p.at = planets::centreAt(spec.planets, i, nt);
			out += planets::planetText(film, p, nt, space::sunDirection(spec.space));
		}
		if (spec.space.on && spec.space.sun) {
			Vec3 eye, target;
			double fov = 40.0;
			film_subjects::cameraOf(header, eye, target, fov);
			out += planets::eclipseText(film, spec.planets, nt, {-eye.x, eye.y, eye.z}, space::sunDirection(spec.space), spec.space.sunSize);
		}
	}
	if (spec.hasSky) {
		double rainbow = 0.0;   // (eased in and out over two seconds)
		for (const Spec::Rainbow &r : spec.rainbows) rainbow = std::max(rainbow, r.strength * smoothstep(r.t0, r.t0 + 2.0, t) * (1.0 - smoothstep(r.t1 - 2.0, r.t1, t)));
		dressing::SkyStorm storm;
		storm.level = spec.storm.levelAt(t), storm.kind = spec.storm.kind;
		dressing::SkyOptics optics = spec.optics;
		optics.exposure = exposureAt(t);
		out += "# the sun and sky at " + std::to_string(dressing::hourAt(spec.sky, t, spec.storyEnd)) + " h (film_dressing.h)\n" + dressing::skyText(spec.key, spec.sky, t, spec.storyEnd, rainbow, storm, optics);
	}
	out += dressingAt(t);
	if (!spec.weather.empty() || !spec.emitters.empty()) {   // weather round the camera, and particles (film_particles.h)
		Vec3 eye, target;
		double fov = 40.0;
		film_subjects::cameraOf(header, eye, target, fov);
		const Vec3 at{-eye.x, eye.y, eye.z};   // (pbrt's)
		const std::shared_ptr<const terrain::Field> land = spec.hasTerrain ? terrain::fieldOf(spec.terrain) : nullptr;
		for (particles::Weather w : spec.weather) {
			if (spec.hasUnderwater && at.y < spec.seas[0].level) break;   // (under the water rain is its rings on the surface: film_sealight.h)
			if (w.onLand && land) w.ground = land->heightAt(at.x, at.z);
			if (w.windX != 0.0 || w.windZ != 0.0) {   // (a storm's: as heavy as the storm is now, its wind gusting)
				const double level = spec.storm.on ? spec.storm.levelAt(t) / spec.storm.strength : 1.0, gust = 0.7 + 0.6 * nature::noise2(t * 0.35, 0.5, 733);
				w.amount *= level, w.windX *= gust, w.windZ *= gust;
				if (!(w.amount > 0.01)) continue;
			}
			out += particles::weatherText(w, t, at, spec.shutter / std::max(1, spec.fps));
		}
		for (const particles::Emitter &e : spec.emitters) out += particles::emitterText(e, t);
	}
	for (const clouds::Clouds &c : spec.cloudLayers) out += clouds::text(spec.key.empty() ? std::string("film") : spec.key, c, nt);   // (film_clouds.h)
	if (!spec.fogBanks.empty()) {   // fog lying on the land (film_clouds.h): the land runs through it
		const std::shared_ptr<const terrain::Field> ground = spec.hasTerrain ? terrain::fieldOf(spec.terrain) : nullptr;
		const auto groundAt = [&](double x, double z) { return ground && ground->inside(x, z) ? ground->heightAt(x, z) : (ground ? spec.terrain.y : 0.0); };
		for (std::size_t i = 0; i < spec.fogBanks.size(); ++i)
			out += clouds::fogBankText(spec.key.empty() ? std::string("film") : spec.key, spec.fogBanks[i], static_cast<int>(i), t, nt, groundAt);
	}
	if (!spec.seas.empty() || !spec.rivers.empty() || !spec.falls.empty()) {   // water (film_water.h)
		Vec3 eye, target;
		double fov = 40.0;
		film_subjects::cameraOf(header, eye, target, fov);
		const std::shared_ptr<const terrain::Field> ground = spec.hasTerrain ? terrain::fieldOf(spec.terrain) : nullptr;
		const double aspect = 16.0 / 9.0, hfov = 2.0 * std::atan(std::tan(fov * kPi / 360.0) * aspect) * 180.0 / kPi;   // (a film's fov is its shorter side's)
		for (water::Sea sea : spec.seas) {
			sea.glow = sea.glow * (1.0 / std::max(1.0, exposureAt(t)));   // (living light as bright on the screen however far the night camera has opened up)
			if (spec.hasUnderwater && undersea::waterlineOf(sea, nt, {-eye.x, eye.y, eye.z}).where == undersea::Waterline::At) sea.holeX = -eye.x, sea.holeZ = eye.z, sea.holeR = 1.5;   // (clear of a lens at the waterline)
			out += water::seaText(sea, nt, eye, target, hfov, ground.get(), spec.swell.on ? &spec.swell : nullptr);
		}
		if (spec.swell.on && ground) out += surf::shallowsCausticText(spec.key.empty() ? std::string("film") : spec.key, nt, -eye.x, eye.z, *ground, spec.seas[0].level, spec.tracedCaustics);
		for (const water::River &r : spec.rivers) out += water::riverText(r, nt, ground.get());
		for (const water::Fall &f : spec.falls) out += f.frozen ? (ground ? std::string() : water::cliffText(f)) + ice::frozenFallText(f) : water::fallText(f, nt, ground.get());
	}
	out += spectacleText(spec, header, t, nt, exposureAt(t), [this](double ft) { return natureAt(ft); }, weatherAt(t));
	if (!spec.auroras.empty() || !spec.lightnings.empty() || !spec.meteors.empty()) {   // the sky's spectacles (film_skyfx.h)
		const std::shared_ptr<const terrain::Field> ground = spec.hasTerrain ? terrain::fieldOf(spec.terrain) : nullptr;
		const Vec3 centre = ground ? Vec3{spec.terrain.x, spec.terrain.y, spec.terrain.z} : Vec3{0.0, 0.0, 0.0};
		for (const skyfx::Aurora &a : spec.auroras) out += skyfx::auroraText(spec.key.empty() ? std::string("film") : spec.key, a, t, centre);
		const auto groundAt = [&](double x, double z) { return ground && ground->inside(x, z) ? ground->heightAt(x, z) : (ground ? spec.terrain.y : 0.0); };
		for (const skyfx::Lightning &l : spec.lightnings) out += skyfx::lightningText(l, t, groundAt);
		if (!spec.meteors.empty()) {
			Vec3 eye, target;
			double fov = 40.0;
			film_subjects::cameraOf(header, eye, target, fov);
			for (const skyfx::Meteors &m : spec.meteors) out += skyfx::meteorText(m, t, eye);
		}
	}
	if (!spec.flocks.empty() || !spec.herds.empty() || !spec.flights.empty()) {   // animals (film_wildlife.h): each kind's poses once
		const std::shared_ptr<const terrain::Field> ground = spec.hasTerrain ? terrain::fieldOf(spec.terrain) : nullptr;
		std::vector<std::string> written;
		const auto first = [&](const std::string &kind) {
			if (std::find(written.begin(), written.end(), kind) != written.end()) return false;
			written.push_back(kind);
			return true;
		};
		animals::Poses poses;   // (the rigged kinds' poses this frame uses: film_animals.h)
		poses.eyeshine = spec.night.eyeshine;
		for (const wildlife::Flock &f : spec.flocks)
			if (t >= f.t0 && t <= f.t1) out += wildlife::flockText(f, f.kind == "eagles" || f.kind == "geese" ? nt : t, ground.get(), poses, first(f.kind));
		for (const wildlife::Herd &h : spec.herds) out += wildlife::herdText(h, nt, ground.get(), poses, spec.herds, spec.hunts, spec.seas.empty() ? -1e30 : spec.seas[0].level);
		for (const wildlife::Flight &f : spec.flights) out += wildlife::flightText(f, nt, poses);
	}
	if (spec.sealife.any()) {   // life under the sea (film_sealife.h)
		const std::shared_ptr<const terrain::Field> ground = spec.hasTerrain ? terrain::fieldOf(spec.terrain) : nullptr;
		const auto groundAt = [&](double x, double z) { return ground && ground->inside(x, z) ? ground->heightAt(x, z) : (ground ? spec.terrain.y : 0.0); };
		out += sealife::text(sealife::lifted(spec.sealife, groundAt), nt, spec.current.velocity(nt), spec.current.drift(nt), spec.reefLife.night.on ? 0.15 : 1.0);   // (asleep on a night dive)
	}
	if (spec.night.flashNow() > 0.0) {   // a camera's flash (film_night.h)
		Vec3 eye, target;
		double fov = 40.0;
		film_subjects::cameraOf(header, eye, target, fov);
		out += night::flashText(spec.night, eye, target);
	}
	if (spec.macro.any()) {   // the small world close up (film_macro.h)
		const std::shared_ptr<const terrain::Field> ground = spec.hasTerrain ? terrain::fieldOf(spec.terrain) : nullptr;
		const auto groundAt = [&](double x, double z) { return ground && ground->inside(x, z) ? ground->heightAt(x, z) : (ground ? spec.terrain.y : 0.0); };
		out += macro::text(spec.macro, nt, groundAt);
	}
	if (!spec.geysers.empty() || !spec.rockfalls.empty() || !spec.avalanches.empty()) {   // the land in action (film_geology.h)
		const std::shared_ptr<const terrain::Field> ground = spec.hasTerrain ? terrain::fieldOf(spec.terrain) : nullptr;
		for (std::size_t i = 0; i < spec.geysers.size(); ++i) {
			const geology::Geyser &gy = spec.geysers[i];
			const double base = ground ? ground->heightAt(gy.x, gy.z) : 0.0;
			out += geology::geyserText(gy, t, base);
			double steam = 0.0;   // (the steam lingers after an eruption, and wisps from the pool between)
			for (double back = 0.0; back <= 6.0; back += 1.0) steam = std::max(steam, gy.strengthAt(t - back) * (1.0 - back / 7.0));
			steam = std::max(steam, 0.12);
			// (its sphere kept clear of the vent, the mound and the pool - a shape's medium with things inside it is drawn wrong on the GPU - so it starts a little up the
			// column: the sphere reaches 0.16 of the plume's height below its base)
			const double tall = gy.height * (0.4 + 1.4 * steam);
			out += fire::smokeText("steam_" + std::to_string(i), {gy.x, base + 2.5 + 0.17 * tall, gy.z}, 0.5 * gy.height * (0.5 + 0.5 * steam), tall, nt, 51 + static_cast<int>(i), 0.04,
								   {0.3 * gy.height, 0.0, 0.0});
		}
		for (const geology::Rockfall &rf : spec.rockfalls) out += geology::rockfallText(rf, t);
		for (const geology::Avalanche &av : spec.avalanches) out += geology::avalancheText(av, t);
	}
	if (!spec.meadows.empty()) {   // wild flowers (film_flowers.h): sunflowers turn to the sun
		const std::shared_ptr<const terrain::Field> ground = spec.hasTerrain ? terrain::fieldOf(spec.terrain) : nullptr;
		double el = 45.0, facing = 180.0;
		if (spec.hasSky) dressing::sunAt(spec.sky, dressing::hourAt(spec.sky, t, spec.storyEnd), el, facing);
		const double east = spec.hasSky ? spec.sky.south - 90.0 : 90.0;
		std::set<std::string> written;
		for (const flowers::Meadow &m : spec.meadows)
			out += flowers::text(m, t, nt, ground.get(), spec.seas.empty() ? -1e30 : spec.seas[0].level, facing, el > -1.0, east, written);
	}
	if (!spec.forests.empty()) {   // forests (film_forest.h): near the camera in full
		Vec3 eye, target;
		double fov = 40.0;
		film_subjects::cameraOf(header, eye, target, fov);
		for (std::size_t i = 0; i < spec.forests.size() && i < spec.planted.size(); ++i)
			out += forest::text(spec.key.empty() ? std::string("film") : spec.key, spec.planted[i], spec.forests[i], nt, t, eye, spec.gusts);
	}
	for (const Piece &piece : scene.world) {
		if (piece.block < 0) {
			out += piece.text;
			continue;
		}
		const Block &b = scene.blocks[static_cast<std::size_t>(piece.block)];
		if (!visible(b.name, t)) continue;
		out += b.head + transformsFor(b.name, t) + lightsScaled(b.name, b.body, t);
	}
	out += clothAt(t);   // cloth (film_cloth.h): simulated up to now
	for (std::size_t k = 0; k < spec.mattes.size(); ++k) out += matte::textOf(spec.mattes[k], static_cast<int>(k));   // matte paintings behind the set (film_matte.h)
	out += castAt(t);   // the characters, as their kinds draw them
	out = shot_lighting::withLights(out, shotLightsAt(header, t));
	out = marksAt(header, t) + out;   // the cartoon marks over the frame (film_marks.h): its `# @rt-mark` lines, before everything (the launcher draws them after the render)
	out = night::withLook(out, spec.night, t);   // (a trail camera's look, heat shimmer: film_night.h)
	if (spec.hasUnderwater) {   // the light under the sea once the sun is in (film_sealight.h), the waterline's look (film_undersea.h)
		Vec3 eye, target;
		double fov = 40.0;
		film_subjects::cameraOf(header, eye, target, fov);
		out = underTheSeaLight(spec, out, header, t, nt, seabedAt(-eye.x, eye.z));
	}
	return spec.space.on ? cosmos::withSpaceLook(space::recentred(out), spec.space.flare, spec.space.bloom) : out;   // (a floating origin for a film in space: film_space.h)
}

// The characters at a time, as their kinds draw them (each in its cast/NAME link group): their acting, voices, faces and looks, a smear's multiples.
std::string DataFilm::castAt(double t) const {
	std::string out;
	double dark = 0.3;   // (how dark it is round them, for real eyes' pupils: from the sky's sun - high 0, night 1 - else a lit set's 0.3, space 0.5)
	if (spec.hasSky) {
		double el = 0.0, facing = 0.0;
		dressing::sunAt(spec.sky, dressing::hourAt(spec.sky, t, spec.storyEnd), el, facing);
		dark = smoothstep(25.0, -8.0, el);
	} else if (spec.space.on) {
		dark = 0.5;
	}
	const auto groomFor = [&](const Character &c) -> const hair::Groom * {   // (`hair`: its own line, else every character's; `off` none)
		const hair::Groom *all = nullptr, *own = nullptr;
		for (const hair::Groom &g : spec.grooms) {
			if (g.who.empty()) all = &g;
			else if (g.who == c.name) own = &g;
		}
		const hair::Groom *g = own ? own : all;
		return g && g->style != "off" ? g : nullptr;
	};
	for (const Character &c : spec.characters) {   // the characters, as their kinds draw them
		const CharacterKind &kind = characterKinds().at(c.kind);
		if (!kind.text) continue;
		const hair::Groom *groom = kind.headFrame ? groomFor(c) : nullptr;
		const auto drawnAt = [&](double tc) {   // the character as its kind draws it at a time
			CharacterMoment m = standingMoment(c, tc);   // (its feet locked and, with `footing ground`, on the floor)
			if (kind.trails) m.trail = trailOf(c, tc, m);
			m.voice = music_cues::voiceAt(spec.key, c.name, tc, shotStarts(), shotNameList(), &m.mouth);   // (a recording's loudness and mouth shapes)
			speakingAt(c, tc, m);
			m.face = faces::faceAt(c.feelings, tc);
			for (const Spec::FaceAct &a : spec.faceActs)   // (a face's acting from a video: face_track.h)
				if (a.who == c.name && a.track && tc >= a.at && tc <= a.to) {
					const double fade = smoothstep(a.at, a.at + 0.2, tc) * (1.0 - smoothstep(a.to - 0.2, a.to, tc));
					m.faceShapes = face_track::at(*a.track, tc - a.at, a.amount * fade);
					m.face.add(face_track::faceOf(m.faceShapes), 1.0);
					const speech::Mouth acted = face_track::mouthOf(m.faceShapes);
					if (acted.open + acted.closed + acted.round > m.mouth.open + m.mouth.closed + m.mouth.round) m.mouth = acted;   // (a `say` line's mouth wins while it moves more)
				}
			m.skinLight = motion::amountFor(spec.skinLight, c.name), m.realEyes = motion::amountFor(spec.realEyes, c.name) > 0.5, m.dark = dark;   // (round 12)
			m.strandHair = groom != nullptr;
			std::string drawn = motionOf(c, kind, tc, m);
			// clean cel shadows on its face: its materials' shading normals from a sphere round its head (`face-normals`, proxy_normal.h; pbrt's x)
			const double faceBlend = motion::amountFor(spec.faceNormals, c.name);
			if (faceBlend > 0.0) {
				const Vec3 head = transformPoint(m.frame, kind.head * c.scale);
				char words[200];
				std::snprintf(words, sizeof words, " \"point3 normalcenter\" [ %.6g %.6g %.6g ] \"float normalradius\" [ %.6g ] \"float normalblend\" [ %.4g ]", -head.x, head.y, head.z,
							  std::max(0.05, (kind.height - kind.head.y) * c.scale * m.scale), faceBlend);
				drawn = withMaterialWords(drawn, words);
			}
			if (groom) {   // hair of strands (film_hair.h): on its head now, its tips trailing where the head was 0.06 s ago, bent by the gusts
				const CharacterMoment was = standingMoment(c, tc - 0.06);
				const hair::Scalp scalp{kind.headFrame(m), kind.scalpCentre, kind.scalpRadii};
				const Vec3 at = transformPoint(scalp.head, scalp.centre);
				const auto own = c.colours.find("hair");
				const Vec3 colour = groom->hasColour ? groom->colour : own != c.colours.end() ? own->second : Vec3{0.17, 0.09, 0.05};
				drawn += hair::text(*groom, scalp, kind.headFrame(was), dressing::gustAt(spec.gusts, at.x, at.z, tc), colour, c.scale, m.seed);
			}
			return drawn;
		};
		const double tc = motion::heldTime(spec.stepping, c.name, t, spec.fps);   // (on twos: the moment of the frame it holds; film_motion.h)
		std::string text = drawnAt(tc);
		// a smear's multiples: copies of it back along its way on a dash (film_motion.h)
		if (const motion::Smear *sm = motion::smearFor(spec.smears, c.name); sm && sm->multiples > 0)
			for (const double u : motion::smearTimes([&](double w) {
					 Vec3 at;
					 momentOf(c, w, &at);
					 return at;
				 }, tc, kind.height * c.scale, sm->amount, spec.fps, sm->multiples))
				text += drawnAt(u);
		out += "# " + c.name + " (" + c.kind + ")\n" + shot_lighting::inLinkGroup(text, "cast/" + c.name);   // (light linking: a shot's `lighting ... only cast` - "cast" names every cast/ group; the ID passes)
	}
	return out;
}

void DataFilm::soundscape(audio::Mix &m) const {
	const bool eagles = std::any_of(spec.flocks.begin(), spec.flocks.end(), [](const wildlife::Flock &f) { return f.kind == "eagles"; });
	if (spec.falls.empty() && spec.fires.empty() && spec.lavas.empty() && spec.volcanoes.empty() && spec.wildfires.empty() && !spec.swell.on && spec.herds.empty() && !eagles &&
		spec.forests.empty() && spec.geysers.empty() && spec.rockfalls.empty() && spec.avalanches.empty() && spec.quakes.empty() && spec.whales.empty() && spec.pods.empty() && spec.glaciers.empty() && spec.tornadoes.empty() &&
		!spec.space.on && spec.ships.empty() && spec.astronauts.empty() && spec.descents.empty() && spec.undersea.breaths.empty() && spec.undersea.streams.empty() && spec.divers.empty())
		return;
	namespace ss = soundscape;
	const ss::Ears ears = ss::earsOf(m.seconds(), [this](double t, Vec3 &eye, Vec3 &target) {
		double fov = 40.0;   // (over the credits the ears stay where the story's last camera was: the cards' own header is the base scene's)
		return film_subjects::cameraOf(headerAt(inCredits(t) ? std::max(0.0, spec.storyEnd - 1e-3) : t), eye, target, fov);
	});
	const std::shared_ptr<const terrain::Field> ground = spec.hasTerrain ? terrain::fieldOf(spec.terrain) : nullptr;
	const auto groundAt = [&](double x, double z) { return ground ? ground->heightAt(x, z) : 0.0; };
	const double T = m.seconds();
	const auto always = [](double) { return 1.0; };
	// sound in space, and craft anywhere (film_voyage.h): a descent onto a terrain under a sky, or a ship over it, has no `space` line (it cannot be with a sky) and was silent
	if (spec.space.on || !spec.ships.empty() || !spec.astronauts.empty() || !spec.descents.empty()) {
		cosmos::SpaceSounds s;
		s.hum = spec.space.on ? spec.space.hum : 0.0, s.ships = spec.ships, s.astronauts = spec.astronauts, s.stars = spec.stars, s.remnants = spec.remnants, s.blackHoles = spec.blackHoles;
		s.natureAt = [this](double t) { return natureAt(t); };
		for (std::size_t k = 0; k < spec.shots.size(); ++k)
			if (spec.shots[k].spaceCam.kind == cosmos::SpaceCam::Hyperspace) s.jumps.push_back({spec.shots[k].start, k + 1 < spec.shots.size() ? spec.shots[k + 1].start : spec.length});
		cosmos::spaceSound(m, ears, s);
		cosmos::descentSound(m, ears, spec.descents, s.natureAt);
		if (!spec.battle.empty()) battle::battleSound(m, ears, spec.battle, spec.ships, s.natureAt);   // (film_battle.h)
	}
	for (std::size_t i = 0; i < spec.tornadoes.size(); ++i) {   // a tornado's roar, from its foot, as strong as it is
		const wind::Tornado &k = spec.tornadoes[i];
		ss::lay(m, ears, k.t0, std::min(T, k.t1), 0.5, [&k, &groundAt](double t) { const Vec3 f = wind::footAt(k, t); return Vec3{f.x, groundAt(f.x, f.z) + 20.0, f.z}; },
				[&k](double t) { return 1.2 * wind::strength(k, t); }, ss::roar(40.0, 600.0, 1.0, 1801u + static_cast<unsigned>(i)), 300.0);
	}
	for (std::size_t i = 0; i < spec.glaciers.size(); ++i) {   // a calving: the crack, then the roar of the slab hitting the water
		const ice::Glacier &g = spec.glaciers[i];
		double cut = 0.0;
		for (std::size_t k = 0; k < g.calving.size(); ++k) {
			const unsigned s = 1701u + static_cast<unsigned>(i * 31 + k * 7);
			const Vec3 at = ice::calveSplash(g, cut, groundAt, spec.seas.empty() ? -1e30 : spec.seas[0].level);
			ss::place(m, ears, g.calving[k], at, audio::noiseBurst(0.6, 0.002, 0.5, 3000.0, 800.0, s), 0.9, 200.0);   // (the crack)
			ss::place(m, ears, g.calving[k] + ice::kCalveFall, at, ss::crash(s + 1u), 1.4, 200.0);
			ss::place(m, ears, g.calving[k] + ice::kCalveFall, at, audio::noiseBurst(3.0, 0.03, 2.6, 50.0, 700.0, s + 2u), 1.8, 200.0);   // (its boom)
			cut += ice::slabDepth(g);
		}
	}
	if ((!spec.whales.empty() || !spec.pods.empty()) && !spec.seas.empty())   // the sea's giants (film_giants.h); nature's times back to the film's
		giants::giantsSound(m, ears, spec.whales, spec.pods, spec.seas[0].level, spec.hasUnderwater, [this](double ft) { return natureAt(ft); }, [this, T](double ntv) {
			double lo = 0.0, hi = T;
			for (int k = 0; k < 40; ++k) (natureAt(0.5 * (lo + hi)) < ntv ? lo : hi) = 0.5 * (lo + hi);
			return hi;
		});
	// under the sea (film_undersea.h): a diver breathing - the regulator's hiss, then the burble of the breath going out - and the bubbles of a stream
	for (std::size_t i = 0; i < spec.undersea.breaths.size(); ++i) {
		const undersea::Breath &b = spec.undersea.breaths[i];
		const Vec3 at = b.at + Vec3{0.0, groundAt(b.at.x, b.at.z), 0.0};
		for (double t0 = 0.0; t0 < T; t0 += std::max(1.5, b.every)) {
			const unsigned seed = 2101u + static_cast<unsigned>(i * 977 + t0 * 10.0);
			ss::place(m, ears, t0 + 0.05 * b.every, at, audio::noiseBurst(0.35 * b.every, 0.1, 0.25 * b.every, 2200.0, 6500.0, seed), 0.35, 30.0);
			for (int k = 0; k < 9; ++k) ss::place(m, ears, t0 + 0.4 * b.every + 0.13 * k, at, ss::bubble(seed + 3u * static_cast<unsigned>(k)), 0.5, 30.0);
		}
	}
	for (std::size_t i = 0; i < spec.divers.size(); ++i) {   // a diver breathing as they swim (film_diver.h): the hiss in, the burble out, from where they are
		diver::Diver d = spec.divers[i];
		for (Vec3 &p : d.path) p.y += seabedAt(p.x, p.z);
		for (double t0 = 0.0; t0 < T; t0 += diver::kBreath) {
			const unsigned seed = 2601u + static_cast<unsigned>(i * 977 + t0 * 10.0);
			ss::place(m, ears, t0 + 0.05 * diver::kBreath, diver::mouthAt(d, natureAt(t0)), audio::noiseBurst(0.35 * diver::kBreath, 0.1, 0.25 * diver::kBreath, 2200.0, 6500.0, seed), 0.35, 30.0);
			for (int k = 0; k < 9; ++k) ss::place(m, ears, t0 + 0.55 * diver::kBreath + 0.12 * k, diver::mouthAt(d, natureAt(t0 + 0.55 * diver::kBreath)), ss::bubble(seed + 3u * static_cast<unsigned>(k)), 0.5, 30.0);
		}
	}
	for (std::size_t i = 0; i < spec.undersea.streams.size(); ++i) {
		const undersea::Stream &st = spec.undersea.streams[i];
		const Vec3 at = st.at + Vec3{0.0, groundAt(st.at.x, st.at.z), 0.0};
		for (double t0 = std::max(0.0, st.t0); t0 < std::min(T, st.t1); t0 += 0.15 + 0.6 * audio::unit(2301u + static_cast<unsigned>(t0 * 100.0) + static_cast<unsigned>(i)))
			ss::place(m, ears, t0, at, ss::bubble(2401u + static_cast<unsigned>(t0 * 100.0)), 0.25 * std::min(2.0, std::sqrt(st.rate / 6.0)), 20.0);
	}
	// waterfalls: a roar from the pool at the foot of each
	for (std::size_t i = 0; i < spec.falls.size(); ++i) {
		const water::Fall &f = spec.falls[i];
		if (f.frozen) continue;   // (hung in ice: silent)
		const Vec3 pool = f.lip + Vec3{0.0, -0.85 * f.drop, 0.0};
		const double g = 0.3 * std::sqrt(std::max(1.0, f.width * f.drop) / 120.0);
		ss::lay(m, ears, 0.0, T, 0.3, [pool](double) { return pool; }, [g](double) { return g; }, ss::roar(250.0, 1500.0, 0.8, 101u + static_cast<unsigned>(i)), 12.0);
	}
	// fires: a low roar and the crackle; a wildfire's roar over its land; lava's hiss and its bubbles; a volcano's rumble
	for (std::size_t i = 0; i < spec.fires.size(); ++i) {
		const fire::Fire &f = spec.fires[i];
		const double g = 0.12 * f.size;
		ss::lay(m, ears, f.t0, f.t1, 0.25, [&f](double) { return f.at + Vec3{0.0, 0.3 * f.size, 0.0}; }, [g](double) { return g; }, ss::roar(120.0, 700.0, 0.6, 201u + static_cast<unsigned>(i)), 4.0);
		double t = std::max(0.0, f.t0);
		for (unsigned k = 0; t < std::min(f.t1, T); ++k) {
			ss::place(m, ears, t, f.at + Vec3{0.0, 0.4 * f.size, 0.0}, ss::crackle(301u + k), 0.3 * f.size * (0.4 + audio::unit(311u + k)), 4.0);
			t += -std::log(std::max(1e-6, 1.0 - audio::unit(331u + k + 7919u * static_cast<unsigned>(i)))) / (8.0 * f.size);
		}
	}
	for (std::size_t i = 0; i < spec.wildfires.size(); ++i) {
		const fire::Wildfire &w = spec.wildfires[i];
		const Vec3 mid{0.5 * (w.x0 + w.x1), groundAt(0.5 * (w.x0 + w.x1), 0.5 * (w.z0 + w.z1)) + 3.0, 0.5 * (w.z0 + w.z1)};
		ss::lay(m, ears, w.t0, w.t1, 0.3, [mid](double) { return mid; }, [&w](double t) { return 0.9 * smoothstep(w.t0, w.t0 + 3.0, t) * (1.0 - smoothstep(w.t1 - 3.0, w.t1, t)); },
				ss::roar(90.0, 900.0, 1.0, 401u + static_cast<unsigned>(i)), 60.0);
		for (unsigned k = 0; k < static_cast<unsigned>(std::max(0.0, std::min(w.t1, T) - w.t0) * 30.0); ++k) {
			const double t = w.t0 + (std::min(w.t1, T) - w.t0) * audio::unit(411u + k), x = w.x0 + (w.x1 - w.x0) * audio::unit(421u + k), z = w.z0 + (w.z1 - w.z0) * audio::unit(431u + k);
			ss::place(m, ears, t, {x, groundAt(x, z) + 1.0, z}, ss::crackle(441u + k), 0.6, 30.0);
		}
	}
	for (std::size_t i = 0; i < spec.lavas.size(); ++i) {
		const fire::Lava &l = spec.lavas[i];
		if (l.path.size() < 2) continue;
		const auto front = [&l](double t) {   // (the flow's front: where it is newest and loudest)
			const double reach = std::max(4.0, (t - l.t0) * l.speed * 20.0);
			double s = 0.0;
			for (std::size_t k = 1; k < l.path.size(); ++k) {
				s += length(Vec3{l.path[k].x - l.path[k - 1].x, 0.0, l.path[k].z - l.path[k - 1].z});
				if (s > reach) return l.path[k];
			}
			return l.path.back();
		};
		ss::lay(m, ears, l.t0, T, 0.3, front, [](double) { return 0.07; }, ss::roar(1800.0, 6000.0, 0.6, 501u + static_cast<unsigned>(i)), 10.0);
		for (unsigned k = 0; k < static_cast<unsigned>(std::max(0.0, T - l.t0) * 2.5); ++k) {
			const double t = l.t0 + (T - l.t0) * audio::unit(511u + k);
			ss::place(m, ears, t, front(t), ss::bubble(521u + k), 0.3, 8.0);
		}
	}
	for (std::size_t i = 0; i < spec.volcanoes.size(); ++i) {
		const fire::Volcano &v = spec.volcanoes[i];
		const Vec3 top{v.x, groundAt(v.x, v.z), v.z};
		ss::lay(m, ears, v.t0, v.t1, 0.5, [top](double) { return top; }, [](double) { return 0.6; }, ss::roar(35.0, 180.0, 1.4, 601u + static_cast<unsigned>(i)), 400.0);   // (above 35 Hz: what speakers can play)
		for (std::size_t k = 0; k < v.flows.size(); ++k) {   // a pyroclastic flow's roar, from its front as it races down
			const fire::PyroclasticFlow &f = v.flows[k];
			const double radius = v.radius;
			ss::lay(m, ears, f.t, std::min(T, f.t + 60.0), 0.5, [f, radius](double t) { return fire::flowFront(f, t - f.t, radius); }, [f](double t) { return 0.8 * std::exp(-(t - f.t) / 25.0); },
					ss::roar(40.0, 900.0, 1.0, 611u + static_cast<unsigned>(i * 7 + k)), 250.0);
		}
	}
	// surf: each wave crashing where it breaks near the camera, its wash after
	if (spec.swell.on && ground && !spec.seas.empty()) {
		const std::shared_ptr<const surf::Grid> grid = surf::gridOf(*ground, spec.seas[0].level, spec.swell);
		std::vector<Vec3> points;   // (break points round where the camera goes: cells 0.8 to 2.2 m deep that the swell reaches)
		for (double t = 0.0; t <= T && points.size() < 16; t += 3.0) {
			const Vec3 eye = ears(t).eye;
			for (int k = 0; k < 400 && points.size() < 16; ++k) {
				const double a = 2.0 * kPi * audio::unit(701u + static_cast<unsigned>(k)), r = 150.0 * std::sqrt(audio::unit(709u + static_cast<unsigned>(k)));
				const double x = eye.x + r * std::cos(a), z = eye.z + r * std::sin(a);
				bool in = false;
				const double d = grid->sample(grid->depth, x, z, in), arrive = grid->sample(grid->arrive, x, z, in);
				if (!in || d < 0.8 || d > 2.2 || arrive < 0.0) continue;
				bool taken = false;
				for (const Vec3 &q : points) taken = taken || std::hypot(q.x - x, q.z - z) < 25.0;
				if (!taken) points.push_back({x, spec.seas[0].level, z});
			}
		}
		for (std::size_t p = 0; p < points.size(); ++p) {
			bool in = false;
			const double arrive = grid->sample(grid->arrive, points[p].x, points[p].z, in);
			for (double t = std::fmod(arrive, spec.swell.period); t < T; t += spec.swell.period) {
				const double set = 0.8 + 0.2 * std::sin(2.0 * kPi * (t - arrive) / (7.3 * spec.swell.period));
				ss::place(m, ears, t, points[p], ss::crash(801u + static_cast<unsigned>(p * 31 + static_cast<std::size_t>(t))), 0.14 * spec.swell.height * set, 15.0);
				ss::place(m, ears, t + 0.9, points[p] + Vec3{0.0, 0.0, 0.0}, ss::wash(901u + static_cast<unsigned>(p)), 0.05 * spec.swell.height, 15.0);
			}
		}
	}
	// animals: hoofbeats at each footfall of a herd's gait; a standing herd's calls (a lion's roar, a wolf's howl...: soundscape::callOf); eagles crying; geese honking
	for (std::size_t hi = 0; hi < spec.herds.size(); ++hi) {
		const wildlife::Herd &h = spec.herds[hi];
		const animals::Species *sp = animals::species(h.kind);
		if (!sp) continue;
		const bool wolves = h.kind == "wolves";
		const ss::Call call = ss::callOf(h.kind, static_cast<unsigned>(971 + hi * 37));
		const double gain = h.kind == "horses" ? 0.3 : wolves ? 0.05 : 0.15, f = h.kind == "horses" ? 85.0 : wolves ? 230.0 : 140.0;
		for (int i = 0; i < std::min(h.count, 40); ++i) {
			std::string lastClip;
			double lastCycles = 0.0;
			for (double t = 0.0; t < T; t += 0.02) {
				const wildlife::Animal a = wildlife::animalAt(h, *sp, i, natureAt(t), ground.get());
				if (!a.clip.empty() && a.clip == lastClip) {
					static const double walk[4] = {0.0, 0.25, 0.5, 0.75}, trot[4] = {0.0, 0.5, 0.5, 0.0}, run[4] = {0.0, 0.40, 0.08, 0.50};
					const double *starts = a.clip == "walk" ? walk : a.clip == "trot" ? trot : run;
					for (int foot = 0; foot < 4; ++foot)
						if (std::floor(a.cycles - starts[foot]) > std::floor(lastCycles - starts[foot])) {   // (each step its own: one sample over and over is a machine gun)
							const unsigned step = static_cast<unsigned>(i * 4 + foot) + 7919u * static_cast<unsigned>(std::max(0.0, std::floor(a.cycles - starts[foot])));
							ss::place(m, ears, t, a.at, ss::hoof(f * (0.9 + 0.2 * audio::unit(static_cast<unsigned>(i * 4 + foot))) * (0.95 + 0.1 * audio::unit(step + 3u)), step),
									  gain * (0.85 + 0.3 * audio::unit(step + 5u)), 5.0);
						}
				}
				if (call.every > 0.0 && a.going.paused && i % 3 == 0) {   // (one in three standing calls now and then)
					const double every = call.every * (0.85 + 0.35 * audio::unit(static_cast<unsigned>(951 + i))), when = std::fmod(t + every * audio::unit(static_cast<unsigned>(961 + i)), every);
					if (when < 0.02) ss::place(m, ears, t, a.at + Vec3{0.0, 0.7 * sp->length, 0.0}, wolves ? ss::howl(static_cast<unsigned>(971 + i)) : call.sound, call.gain, call.within);
				}
				lastClip = a.clip, lastCycles = a.cycles;
			}
		}
	}
	for (const wildlife::Flight &f : spec.flights) {   // a flight's birds calling as they go: geese honking, an eagle's cry
		if (spec.muteBirds) continue;
		const animals::Species *sp = animals::species(f.kind);
		const double v = sp && f.speed <= 0.0 ? sp->walkSpeed : f.speed, D = length(f.to - f.from), T1 = f.at + 1.5 + D / std::max(0.1, v);
		for (int i = 0; i < f.count; ++i)
			for (double t = f.at + 0.3 + 0.4 * i + 2.0 * audio::unit(static_cast<unsigned>(1101 + i)); t < std::min(T, T1); t += 2.5 + 3.0 * audio::unit(static_cast<unsigned>(1111 + i) + static_cast<unsigned>(t))) {
				const double u = std::clamp((t - f.at) / std::max(0.1, T1 - f.at), 0.0, 1.0);
				const Vec3 at = f.from + (f.to - f.from) * u + Vec3{0.0, 4.0 * std::min(30.0, 0.12 * D) * u * (1.0 - u), 0.0};
				ss::place(m, ears, t, at, f.kind == "geese" ? ss::honk(static_cast<unsigned>(1121 + i * 13) + static_cast<unsigned>(t)) : ss::eagleCry(), f.kind == "geese" ? 0.25 : 0.35, 60.0);
			}
	}
	for (const wildlife::Flock &f : spec.flocks) {
		if (f.kind != "eagles" || spec.muteBirds) continue;
		const std::vector<float> cry = ss::eagleCry();
		for (int i = 0; i < f.count; ++i) {
			const double every = 9.0 + 9.0 * audio::unit(static_cast<unsigned>(1001 + i));
			for (double t = std::max(0.0, f.t0) + every * audio::unit(static_cast<unsigned>(1011 + i)); t < std::min(T, f.t1); t += every)
				ss::place(m, ears, t, wildlife::eagleAt(f, i, natureAt(t)), cry, 0.35, 50.0);
		}
	}
	// the land in action: a geyser's roar while it erupts, each rock's knock as it lands, an avalanche's rumble from its front, a quake's rumble all round
	for (std::size_t i = 0; i < spec.geysers.size(); ++i) {
		const geology::Geyser &gy = spec.geysers[i];
		const Vec3 vent{gy.x, groundAt(gy.x, gy.z) + 3.0, gy.z};
		ss::lay(m, ears, 0.0, T, 0.25, [vent](double) { return vent; }, [&gy](double t) { return 0.4 * gy.strengthAt(t); }, ss::roar(250.0, 3500.0, 0.6, 1301u + static_cast<unsigned>(i)), 20.0);
	}
	for (const geology::Rockfall &rf : spec.rockfalls)
		for (const geology::Rock &r : rf.rocks)
			for (std::size_t k = 0; k < r.impacts.size(); ++k) {
				const std::size_t at = std::min(r.at.size() - 1, static_cast<std::size_t>((r.impacts[k] - r.t0) * 48.0));
				ss::place(m, ears, r.impacts[k], r.at[at], ss::hoof(55.0 + 40.0 * audio::unit(static_cast<unsigned>(r.seed * 7 + k)), static_cast<unsigned>(r.seed + k)), 0.25 * std::min(2.0, r.size), 12.0);
				ss::place(m, ears, r.impacts[k] + 0.01, r.at[at], ss::crackle(static_cast<unsigned>(r.seed * 13 + k)), 0.3 * std::min(2.0, r.size), 12.0);
			}
	for (std::size_t i = 0; i < spec.avalanches.size(); ++i) {
		const geology::Avalanche &av = spec.avalanches[i];
		if (av.path.size() < 2) continue;
		const auto front = [&av](double t) { return av.path[std::min(av.path.size() - 1, static_cast<std::size_t>(av.frontAt(t) / 2.0))]; };
		ss::lay(m, ears, av.t0, T, 0.4, front, [&av](double t) { return 0.9 * smoothstep(av.t0, av.t0 + 2.0, t) * (av.frontAt(t) < 2.0 * (av.path.size() - 1) - 1.0 ? 1.0 : 0.3); },
				ss::roar(25.0, 300.0, 1.2, 1401u + static_cast<unsigned>(i)), 150.0);
	}
	for (std::size_t i = 0; i < spec.quakes.size(); ++i) {
		const geology::Quake &q = spec.quakes[i];
		ss::lay(m, ears, q.t0, q.t1, 0.4, [&ears](double t) { return ears(t).eye + Vec3{0.0, -2.0, 0.0}; }, [&q](double t) { return 0.8 * q.levelAt(t); }, ss::roar(30.0, 150.0, 1.2, 1501u + static_cast<unsigned>(i)), 5.0);
	}
	// birds in the trees near the camera, by day: a phrase every few seconds (not a wall of song: the music is heard over them)
	if (!spec.planted.empty() && !spec.muteBirds) {
		unsigned k = 0;
		for (double t = 0.4; t < T; t += 1.8 + 2.4 * audio::unit(1301u + k), ++k) {
			if (spec.hasSky) {
				double el = 0.0, facing = 0.0;
				dressing::sunAt(spec.sky, dressing::hourAt(spec.sky, t, spec.storyEnd), el, facing);
				if (el < -4.0) continue;
			}
			const Vec3 eye = ears(t).eye;
			const forest::Tree *best = nullptr;
			double bestScore = 1e30;
			for (const std::vector<forest::Tree> &trees : spec.planted)
				for (const forest::Tree &tr : trees) {
					const double d = std::hypot(tr.at.x - eye.x, tr.at.z - eye.z);
					if (d > 80.0 || tr.species == "conifer" && audio::unit(k * 7u + 3u) < 0.3) continue;
					const double score = d * (0.3 + audio::unit(static_cast<unsigned>(std::lround(tr.at.x * 13.0 + tr.at.z * 7.0)) + k));
					if (score < bestScore) bestScore = score, best = &tr;
				}
			if (best) ss::place(m, ears, t + 0.5 * audio::unit(1101u + k), best->at + Vec3{0.0, 5.0 * best->scale, 0.0}, ss::birdPhrase(1201u + k), 0.09, 15.0);
		}
	}
}

void DataFilm::thunder(audio::Mix &m) const {
	for (const skyfx::Lightning &l : spec.lightnings)
		skyfx::thunder(m, l, [this](double t, double x, double z) {
			Vec3 eye, target;
			double fov = 40.0;
			if (!film_subjects::cameraOf(headerAt(t), eye, target, fov)) return std::hypot(x, z);
			return std::hypot(x + eye.x, z - eye.z);   // (eye is story space: pbrt's x is -eye.x)
		});
}

// ---- footprints and the dust of steps (round 11, `footprints`) ----
namespace {
struct Footfall {
	double t = 0.0;
	Vec3 at, forward;   // story space
	double size = 1.0;  // (the character's height)
};
}  // namespace
// Round 11's things in a frame at film time t (nature's `nt`): footprints, floating water and clutter (film_zerog.h), interiors (film_interior.h), holograms and
// screens (film_screens.h) and an explainer's drawings (film_explain.h).
std::string DataFilm::aboardAt(const std::string &header, double t, double nt) const {
	std::string out;
	out += footprintsText(t);   // (round 11: `footprints`)
	for (const zerog::Blob &b : spec.blobs) {   // water floating with no weight, and tears held on faces (film_zerog.h)
		if (b.on.empty()) {
			out += zerog::blobText(b, nt);
			continue;
		}
		for (const Character &c : spec.characters)
			if (c.name == b.on) {
				const CharacterKind &kind = characterKinds().at(c.kind);
				const CharacterMoment m = momentOf(c, t);
				const Vec3 eye = transformPoint(m.frame, (kind.head + Vec3{0.035, 0.0, 0.09}) * c.scale);   // (beside its eye, on the front of its face; story space)
				const Vec3 face{-eye.x, eye.y, eye.z};
				out += zerog::blobText(b, t, &face);
			}
	}
	for (const zerog::Drift &d : spec.drifts) out += zerog::driftText(d, nt);
	for (const interior::Interior &in : spec.interiors) out += interior::interiorText(in, nt);   // (film_interior.h)
	for (const screens::Hologram &h : spec.holograms) out += screens::hologramText(h, nt);   // (film_screens.h)
	for (const screens::Screen &sc : spec.screens) out += screens::screenText(sc, nt, spec.key.empty() ? std::string("film") : spec.key, spec.ships);
	if (spec.explain.any() || spec.night.trap) out += overlaysAt(header, t, nt);
	return out;
}

std::string DataFilm::overlaysAt(const std::string &header, double t, double nt) const {
	explain::View v;
	film_subjects::cameraOf(header, v.eye, v.target, v.fov);
	const std::string film = spec.key.empty() ? std::string("film") : spec.key;
	std::string out;
	for (const explain::Label &l : spec.explain.labels) {   // what each names: where its middle is now and how far its top is above that (pbrt's)
		Vec3 at = l.at;
		double radius = 0.0;
		bool found = l.on.empty();
		for (const cosmos::Ship &s : spec.ships)
			if (!found && s.name == l.on) {
				if (s.goneAt >= 0.0 && nt > s.goneAt) break;
				at = screens::shipWhere(s, nt), radius = 0.35 * s.size, found = true;
			}
		for (std::size_t i = 0; i < spec.planets.size() && !found; ++i)
			if (spec.planets[i].name == l.on) at = planets::centreAt(spec.planets, i, nt), radius = spec.planets[i].radius, found = true;
		for (const Character &c : spec.characters)
			if (!found && c.name == l.on) {
				const Vec3 head = transformPoint(momentOf(c, t).frame, characterKinds().at(c.kind).head * c.scale);
				at = {-head.x, head.y, head.z}, radius = 0.35 * c.scale, found = true;
			}
		if (found) out += explain::labelText(l, t, at, radius, v, film);
	}
	for (const explain::OrbitLine &o : spec.explain.orbits) {
		std::vector<Vec3> path;
		for (const cosmos::Ship &s : spec.ships)
			if (s.name == o.of && s.orbit.on) path = explain::orbitPath(s.orbit, nt);
		for (std::size_t i = 0; i < spec.planets.size(); ++i) {   // a moon: its loop round where what it goes round is now
			const planets::Planet &p = spec.planets[i];
			if (p.name != o.of || p.around.empty()) continue;
			std::size_t parent = i;
			for (std::size_t k = 0; k < spec.planets.size(); ++k)
				if (spec.planets[k].name == p.around) parent = k;
			const Vec3 now = planets::centreAt(spec.planets, parent, nt);
			for (int k = 0; k <= 160; ++k) {
				const double at = nt + p.period * k / 160.0;
				path.push_back(now + planets::centreAt(spec.planets, i, at) - planets::centreAt(spec.planets, parent, at));
			}
		}
		out += explain::orbitLineText(o, t, path, v);
	}
	for (const explain::ScaleBar &b : spec.explain.bars) out += explain::scaleBarText(b, t, v, film);
	out += night::stampText(spec.night, t, v, film);   // (a trail camera's strip: film_night.h)
	for (const explain::Compare &c : spec.explain.compares) {
		std::vector<explain::LinedUp> bodies;
		for (const std::string &name : c.bodies) {
			explain::LinedUp b;
			b.name = name;
			if (const explain::Body *k = explain::knownBody(name)) {
				b.name = k->name, b.radius = k->radiusKm, b.colour = k->colour, b.shines = b.name == "Sun", b.size = explain::detail::grouped(2.0 * k->radiusKm) + " km";
			} else {
				for (const planets::Planet &p : spec.planets)
					if (p.name == name) {
						static const std::map<std::string, Vec3> tint = {{"rocky", {0.55, 0.5, 0.45}}, {"desert", {0.8, 0.6, 0.35}}, {"ice", {0.8, 0.88, 0.95}},
																		{"ocean", {0.25, 0.45, 0.85}}, {"gas", {0.85, 0.72, 0.55}}, {"lava", {0.6, 0.25, 0.1}}};
						b.radius = p.radius, b.colour = tint.count(p.kind) ? tint.at(p.kind) : Vec3{0.6, 0.6, 0.6}, b.size = explain::detail::grouped(2.0 * p.radius);
					}
			}
			bodies.push_back(b);
		}
		out += explain::compareText(c, t, bodies, v, film);
	}
	return out;
}

std::string DataFilm::footprintsText(double t) const {
	if (spec.footprints.empty()) return std::string();
	// every footfall of the film, worked out once (a foot coming down: not down a moment before, down now), sampled 30 times a second
	static std::mutex lock;
	static std::map<std::string, std::shared_ptr<const std::vector<Footfall>>> cache;
	std::shared_ptr<const std::vector<Footfall>> falls;
	{
		const std::lock_guard<std::mutex> hold(lock);
		std::shared_ptr<const std::vector<Footfall>> &slot = cache[spec.key + "#" + spec.version];
		if (!slot) {
			auto all = std::make_shared<std::vector<Footfall>>();
			const double end = std::max(spec.length, spec.storyEnd);
			for (const Character &c : spec.characters) {
				if (std::find(spec.footprints.begin(), spec.footprints.end(), std::string("*")) == spec.footprints.end() &&
					std::find(spec.footprints.begin(), spec.footprints.end(), c.name) == spec.footprints.end())
					continue;
				const CharacterKind &kind = characterKinds().at(c.kind);
				if (!kind.feet) continue;
				std::vector<char> down;
				for (double u = 0.0; u <= end; u += 1.0 / 30.0) {
					CharacterMoment m = momentOf(c, u);
					lockFeet(c, u, m);
					const std::vector<Foot> feet = kind.feet(m);
					down.resize(feet.size(), 1);
					for (std::size_t k = 0; k < feet.size(); ++k) {
						if (feet[k].planted && !down[k] && m.moving) {
							Footfall f;
							f.t = u, f.at = feet[k].at, f.size = kind.height * c.scale;
							f.forward = normalize(transformPoint(m.frame, Vec3{0.0, 0.0, 1.0}) - transformPoint(m.frame, Vec3{0.0, 0.0, 0.0}));
							all->push_back(f);
						}
						down[k] = feet[k].planted;
					}
				}
			}
			slot = all;
		}
		falls = slot;
	}
	std::string out = "# footprints (film_world.cpp)\n";
	const double g = std::max(0.1, spec.gravity);
	for (std::size_t i = 0; i < falls->size(); ++i) {
		const Footfall &f = (*falls)[i];
		if (f.t > t) continue;
		// the print: a dark hollow the shape of a sole, along the way it walked
		const double len = 0.16 * f.size, wid = 0.065 * f.size, yaw = std::atan2(f.forward.x, f.forward.z) * 180.0 / kPi;
		out += attr(diffuseMaterial({0.07, 0.068, 0.065}), emitMesh(uvSphere(1.0, 16, 6), mul(translate(f.at.x, f.at.y + 0.001, f.at.z), rotate({0.0, 1.0, 0.0}, yaw), scale(wid, 0.004, len))));
		// the dust it kicked up: grains thrown out and up, flying on clean arcs with no air to hold them, back down where they fall (only under a low gravity: an airless world)
		const double since = t - f.t, flight = 2.0 * 0.9 / g;
		if (g >= 6.0 || since > flight) continue;
		for (int k = 0; k < 12; ++k) {
			const int h = static_cast<int>(i) * 31 + k;
			const double a = 2.0 * kPi * hash01(h, 1), up = 0.4 + 0.5 * hash01(h, 2), out_ = 0.2 + 0.5 * hash01(h, 3);
			const Vec3 v = (Vec3{std::cos(a), 0.0, std::sin(a)} * out_ + f.forward * 0.3 + Vec3{0.0, up, 0.0}) * std::sqrt(f.size / 1.8);
			const double s = std::min(since, 2.0 * v.y / g);
			const Vec3 p = f.at + v * s + Vec3{0.0, -0.5 * g * s * s, 0.0};
			out += attr(diffuseMaterial({0.5, 0.48, 0.45}), emitMesh(uvSphere(0.008 * f.size, 6, 3), translate(p.x, std::max(f.at.y, p.y), p.z)));
		}
	}
	return out;
}

std::string DataFilm::motionOf(const Character &c, const CharacterKind &kind, double t, CharacterMoment &m) const {
	// follow-through: what dangles from its head lags the head (and its walk's bob) on a loose spring
	const double jiggle = motion::amountFor(spec.jiggle, c.name);
	if (jiggle > 0.0)
		m.jiggle = motion::jiggleOf([&](double u) {
			const CharacterMoment at = momentOf(c, u);
			return transformPoint(at.frame, kind.head * c.scale) + Vec3{0.0, 0.03 * c.scale * std::cos(2.0 * kPi * at.steps), 0.0};
		}, t, jiggle);
	std::string text = kind.text(m);
	// squash and stretch: the whole of it deformed along its motion and by its bouncy spring
	const double squash = motion::amountFor(spec.squash, c.name);
	if (squash > 0.0)
		text = motion::wrapped(text, motion::squashStretch([&](double u) {
			Vec3 at;
			momentOf(c, u, &at);
			return at;
		}, t, kind.height * c.scale, squash));
	// its walk's personality: a sneak's lean, a strut's sway, a skip's hops (film_motion.h gaitPose)
	if (m.gait != motion::Gait::Plain) text = motion::wrapped(text, motion::gaitPose(m.gait, m.frame, m.steps, m.going, kind.height * c.scale));
	// low gravity: a walk becomes a lope, each step a bound that lifts it clear of the ground and floats it down (`gravity`)
	if (spec.gravity < 6.0 && m.going > 0.0 && !m.airborne) {
		const double bound = std::min(0.3, 0.05 * (9.81 / std::max(0.1, spec.gravity) - 1.0)) * kind.height * c.scale * m.going * std::abs(std::sin(kPi * m.steps));
		text = motion::wrapped(text, translate(0.0, bound, 0.0));
	}
	// anticipation and settle: wound up before a start or a turn, overshooting after a stop (film_motion.h)
	if (const double ant = motion::amountFor(spec.anticipate, c.name); ant > 0.0)
		text = motion::wrapped(text, motion::anticipation([&](double u) { return momentOf(c, u).frame; }, t, kind.height * c.scale, ant));
	// a smear frame on a dash: drawn out back along its way (film_motion.h)
	if (const motion::Smear *sm = motion::smearFor(spec.smears, c.name); sm && sm->amount > 0.0)
		text = motion::wrapped(text, motion::smearStretch([&](double u) {
			Vec3 at;
			momentOf(c, u, &at);
			return at;
		}, t, kind.height * c.scale, sm->amount, spec.fps));
	return text;
}

std::string DataFilm::clothAt(double t) const {
	std::string out;
	for (const cloth::Cloth &cl : spec.cloths) {   // (film_cloth.h)
		cloth::World w;
		// the film's wind (a flag with none flies in a breeze, a curtain stirs; a sheet and a cape feel only the air they move through)
		w.wind = spec.wind.on ? spec.wind.dir() * spec.wind.speed : cl.kind == "flag" ? Vec3{4.0, 0.0, 0.0} : cl.kind == "curtain" ? Vec3{0.0, 0.0, 0.6} : Vec3{};
		if (cloth::worn(cl.kind)) {
			const Character *who = nullptr;
			for (const Character &c : spec.characters)
				if (c.name == cl.who) who = &c;
			if (!who) continue;
			const CharacterKind &kind = characterKinds().at(who->kind);
			w.holdAt = [this, who, &kind, &cl](double when, int count, std::vector<Vec3> &held, std::vector<cloth::Sphere> &body) {
				const double heldAt = std::max(0.0, motion::heldTime(spec.stepping, who->name, when, spec.fps));   // (on twos: the moment its frame holds)
				const CharacterMoment m = standingMoment(*who, heldAt);   // (as it is drawn: its hips down with `footing ground`)
				const double s = m.scale, shoulders = kind.chest.y + 0.45 * (kind.head.y - kind.chest.y), back = -0.85 * kind.radius;
				const auto pbrt = [](Vec3 p) { return Vec3{-p.x, p.y, p.z}; };   // (story space to pbrt's)
				held.clear(), body.clear();
				const double waist = 0.70 * kind.chest.y, r = 0.5 * cl.width / std::max(1e-6, s);
				for (int i = 0; i < count; ++i) {
					const double u = static_cast<double>(i) / std::max(1, count - 1);
					if (cl.kind == "skirt" || cl.kind == "coat") {   // (round the waist: a skirt all the way, a coat from one side of the front round the back to the other)
						const double a = cl.kind == "skirt" ? 2.0 * kPi * i / count : kPi * (0.18 + 1.64 * u);
						held.push_back(pbrt(transformPoint(m.frame, Vec3{r * std::sin(a), waist, r * std::cos(a) * 0.8} * s)));
					} else if (cl.kind == "scarf") {   // (an end of a scarf, hanging from round the neck at the front)
						const Vec3 neck{0.06 + 0.10 * u, kind.chest.y + 0.62 * (kind.head.y - kind.chest.y), 0.55 * kind.radius - 0.03 * u};
						held.push_back(pbrt(transformPoint(m.frame, neck * s)));
					} else {
						const double across = 0.5 * cl.width * (1.0 - 2.0 * u) / std::max(1e-6, s);   // (its left (+x, body space) to its right)
						held.push_back(pbrt(transformPoint(m.frame, Vec3{across, shoulders, back} * s)));
					}
				}
				body.push_back({pbrt(transformPoint(m.frame, Vec3{0.0, kind.chest.y, 0.0} * s)), 0.95 * kind.radius * s});
				body.push_back({pbrt(transformPoint(m.frame, Vec3{0.0, 0.55 * kind.chest.y, 0.0} * s)), 0.9 * kind.radius * s});
				if (cl.kind != "cape" && kind.feet) {   // (the legs: balls from the hips to each foot, so a skirt swings round them as they stride)
					for (const Foot &f : kind.feet(m)) {
						const Vec3 hip = transformPoint(m.frame, Vec3{0.0, 0.52 * kind.chest.y, 0.0} * s);
						for (int k = 0; k < 5; ++k) {
							const double u = (k + 0.5) / 5.0;
							body.push_back({pbrt(lerp(hip, f.at + Vec3{0.0, 0.08 * s, 0.0}, u)), (0.075 - 0.025 * u) * kind.height / 1.8 * s});
						}
					}
				}
				return true;
			};
		}
		// (kept between frames under the film's own text and its wind: an edited walk, cape or wind starts the cloth again rather than going on from what it was)
		char wind[96];
		std::snprintf(wind, sizeof wind, "@%g,%g,%g", w.wind.x, w.wind.y, w.wind.z);
		out += cloth::text((spec.key.empty() ? std::string("film") : spec.key) + "@" + spec.version + wind, cl, w, t);
	}
	return out;
}

std::string DataFilm::marksAt(const std::string &header, double t) const {
	if (spec.marks.empty()) return std::string();
	film_marks::Camera cam;
	film_subjects::cameraOf(header, cam.eye, cam.target, cam.fov);
	const film_marks::PoseAt poseAt = [&](const std::string &who, double when, Vec3 &head, Vec3 &top, Vec3 &feet, Vec3 &chest) {
		for (const Character &c : spec.characters) {
			if (c.name != who) continue;
			const CharacterKind &kind = characterKinds().at(c.kind);
			const CharacterMoment m = standingMoment(c, motion::heldTime(spec.stepping, c.name, when, spec.fps));
			head = transformPoint(m.frame, kind.head * m.scale), top = transformPoint(m.frame, Vec3{0.0, kind.height, 0.0} * m.scale);
			feet = transformPoint(m.frame, {0.0, 0.0, 0.0}), chest = transformPoint(m.frame, kind.chest * m.scale);
			return true;
		}
		return false;
	};
	return film_marks::headerLines(spec.marks, t, cam, poseAt);
}

std::string DataFilm::airMedium() const {
	const atmosphere::Air a(spec.air.haze);
	const double A = spec.hasSky ? spec.sky.altitude : 0.0;
	const atmosphere::RGB R = a.rayleigh * a.rayleighDensity(A);
	const double Ms = a.mieScatter * a.mieDensity(A), Ma = a.mieAbsorb * a.mieDensity(A), lumR = R.luminance();
	const double H = a.mieHeight;
	const double k = spec.air.strength * (1.0 + std::exp(10.0)), g = a.mieG * Ms / std::max(1e-12, Ms + lumR);
	char buf[480];
	std::snprintf(buf, sizeof buf,
				  "MakeNamedMedium \"air\" \"string type\" [ \"heightfog\" ] \"rgb sigma_a\" [ %.6g %.6g %.6g ] \"rgb sigma_s\" [ %.6g %.6g %.6g ] \"float g\" [ %.4f ] "
				  "\"float top\" [ %.2f ] \"float thickness\" [ %.2f ] \"float radius\" [ %.2f ]\nMediumInterface \"\" \"air\"\n",
				  Ma * k, Ma * k, Ma * k, (R.r + Ms) * k, (R.g + Ms) * k, (R.b + Ms) * k, g, -10.0 * H, H, spec.air.radius);
	return buf;
}

}  // namespace film_file
}  // namespace cartoon
