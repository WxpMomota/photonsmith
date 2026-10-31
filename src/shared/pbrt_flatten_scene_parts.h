#pragma once
// pbrt_flatten_scene_parts.h -- flatten()'s media, light and camera phases
//
// Part of pbrt_flatten.h: included by it at the point flatten() is defined, and not usable on
// its own (it needs FlatScene, the flatten_detail helpers and the pbrt_scene types declared
// above that point). Each function is one phase of flatten(), moved here verbatim so no
// single function is thousands of lines long.

namespace flatten_detail {

// A "cloud" medium: density, wispiness and frequency.
inline void readCloudMedium(FlatScene &out, const pbrt_scene::MediumDecl &md, Medium &medium) {
	const auto warn = [&out](const std::string &msg) {
		out.warnings.push_back({0, std::string(), msg});
	};
	// pbrt-v4's real per-param defaults (media.cpp's
	// CloudMedium::Create) - see Medium::density's own comment for
	// why these differ from this codebase's own E2 showcase scene.
	medium.density = md.params.getFloat("density", 1.0);
	medium.wispiness = md.params.getFloat("wispiness", 1.0);
	medium.frequency = md.params.getFloat("frequency", 5.0);
	// cloud_medium_hittable.h forces sigma_a to 0 (pure scattering) -
	// same convention constant_medium already uses, see that
	// header's own comment for why - so a scene that gave a real
	// absorption coefficient silently loses it. Worth a warning:
	// unlike most of this loader's approximations, this one can't
	// be inferred from the render (a too-bright cloud looks like a
	// lighting choice, not a dropped parameter).
	if (isNonzeroRGB(medium.sigma_a)) {
		warn("cloud medium '" + md.name + "' has a nonzero sigma_a; "
			 "cloud media only model scattering (sigma_a is forced to 0)");
	}
}

// An "rgbgrid" medium: the per-voxel sigma_a/sigma_s/Le arrays and the emission scale.
inline void readRgbGridMedium(FlatScene &out, const pbrt_scene::MediumDecl &md, Medium &medium, std::size_t voxels) {
	const auto warn = [&out](const std::string &msg) {
		out.warnings.push_back({0, std::string(), msg});
	};
	// "rgb sigma_a"/"rgb sigma_s": a flat array of one RGB triple
	// PER VOXEL (length 3*nx*ny*nz), NOT the single flat colour the
	// generic Vec3 read above assumed - de-interleave into
	// per-channel vectors here, matching RGBGridMediumData::build()'s
	// own (sa_r,sa_g,sa_b,...) parameter shape. Either array is
	// optional (empty vector = "channel group absent" - see
	// RGBGridMediumData::build()'s own comment); a present array
	// whose length doesn't match voxels*3 is dropped with a warning
	// rather than read out of bounds or silently truncated/padded.
	const auto deinterleave = [&](const char *name, std::vector<double> &r,
								   std::vector<double> &g, std::vector<double> &b) {
		const pbrt_scene::Param *p = md.params.find(name);
		if (!p || p->numbers.empty()) return;
		if (p->numbers.size() != voxels * 3) {
			warn("medium '" + md.name + "'s \"" + name + "\" array has "
				 + std::to_string(p->numbers.size()) + " numbers, expected "
				 + std::to_string(voxels * 3) + " (3 * nx*ny*nz); ignored");
			return;
		}
		r.resize(voxels); g.resize(voxels); b.resize(voxels);
		for (std::size_t i = 0; i < voxels; ++i) {
			r[i] = p->numbers[i*3+0]; g[i] = p->numbers[i*3+1]; b[i] = p->numbers[i*3+2];
		}
	};
	deinterleave("sigma_a", medium.sigma_a_r, medium.sigma_a_g, medium.sigma_a_b);
	deinterleave("sigma_s", medium.sigma_s_r, medium.sigma_s_g, medium.sigma_s_b);

	// "rgb Le"/"float Lescale" - real per-voxel emission, pbrt-v4's
	// own RGBGridMedium::LeGrid/LeScale - see Medium::Le_r's own
	// comment for why this is parsed separately from the
	// homogeneous-only flat "Le" above. Lescale is NOT baked into
	// the array here (unlike homogeneous's own medium.Le, which
	// bakes its scale in at parse time) - RGBGridMediumData<T>::
	// sample_point() applies Le_scale at sample time itself
	// (`Le_out[c] = Le_scale * le;`), matching how sigma_scale
	// (this same struct's "scale" param, applied inside
	// RGBGridMediumData::build() too) already works for sigma_a/
	// sigma_s. Defaults to 0.0 (matches RGBGridMediumData::
	// Le_scale's own default and pbrt-v4's real "Lescale" default
	// of 1 - but see is_emissive()'s own `Le_scale > 0` gate: a
	// scene that sets a real "Le" array but never touches
	// "Lescale" at all should still glow, so the DEFAULT here is
	// 1.0 when omitted, matching pbrt-v4's spec exactly; only an
	// explicit "Lescale" of 0 (or omitting "Le" entirely, leaving
	// Le_r/g/b empty) turns emission off).
	// "blackbody Le" (a single Kelvin temperature) is a
	// homogeneous-only convenience - rgbgrid's own real "Le" is
	// always a per-voxel RGB array (one triple per voxel), which a
	// single temperature can't express. Checked explicitly (rather
	// than falling through to deinterleave()'s own generic
	// wrong-length warning below) since a code-review pass found
	// that generic message actively misleading here - it reads
	// "array has 1 numbers, expected N" which implies "supply more
	// numbers", not "this syntax isn't supported for rgbgrid".
	const pbrt_scene::Param *leParam = md.params.find("Le");
	if (leParam && leParam->type == "blackbody") {
		warn("medium '" + md.name + "' (\"rgbgrid\") has a \"blackbody Le\", "
			 "but rgbgrid media only support a per-voxel \"rgb Le\" array, not "
			 "a single blackbody temperature; the emission is dropped");
	} else {
		deinterleave("Le", medium.Le_r, medium.Le_g, medium.Le_b);
	}
	// pbrt-v4's RGBGridMedium (unlike its homogeneous and grid media, and its
	// lights) does not divide the emission by SpectrumToPhotometric: the
	// per-voxel RGB is an RGBIlluminantSpectrum that keeps the full D65
	// illuminant, which the film reads as CIE_Y_integral (106.856) times the
	// radiance the same number means anywhere else. A real pbrt file's
	// "Lescale" is written for that, so it is read that way here: pbrt draws
	// "Lescale" 0.0187 where a plain 2 would be needed otherwise
	// (`pbrt_scenes/rgbgrid-emission.pbrt`, measured 106.9 on a dim grid).
	constexpr double kPbrtRgbGridLeNorm = 106.856495;
	medium.Le_scale = md.params.getFloat("Lescale", 1.0) * kPbrtRgbGridLeNorm;
	// "Lescale" set but no (valid) "Le" array ended up populated -
	// silently has no effect otherwise (RGBGridMediumData::
	// is_emissive() correctly stays false), which a code-review
	// pass found gives no hint to a scene author who forgot/
	// mistyped "Le" - warn, matching homogeneous Le's own "nonzero
	// param with no effect" diagnostic precedent above.
	if (md.params.find("Lescale") &&
		medium.Le_r.empty() && medium.Le_g.empty() && medium.Le_b.empty()) {
		warn("medium '" + md.name + "' (\"rgbgrid\") sets \"Lescale\" but has no "
			 "valid \"rgb Le\" array; \"Lescale\" has no effect without one");
	}
}

// A "uniformgrid" medium: the per-voxel density array.
inline void readUniformGridMedium(FlatScene &out, const pbrt_scene::MediumDecl &md, Medium &medium, std::size_t voxels) {
	const auto warn = [&out](const std::string &msg) {
		out.warnings.push_back({0, std::string(), msg});
	};
	// "float density": pbrt-v4's own REQUIRED flat scalar array,
	// exactly one value per voxel (length nx*ny*nz) - not a colour,
	// not the "cloud"-only scalar `density` field above. sigma_a/
	// sigma_s (the generic Vec3 read at the top of this loop) supply
	// the base coefficients each voxel's density multiplies (see
	// GridMediumData<T>::sample_sigma()). A missing or wrong-length
	// array is dropped with a warning (leaves gridDensity empty),
	// matching rgbgrid's own "wrong length -> warn and ignore rather
	// than read out of bounds" convention.
	const pbrt_scene::Param *p = md.params.find("density");
	if (!p || p->numbers.empty()) {
		warn("uniformgrid medium '" + md.name + "' has no \"float density\" "
			 "array (pbrt-v4 requires one); treated as empty (invisible)");
	} else if (p->numbers.size() != voxels) {
		warn("medium '" + md.name + "'s \"density\" array has "
			 + std::to_string(p->numbers.size()) + " numbers, expected "
			 + std::to_string(voxels) + " (nx*ny*nz); ignored");
	} else {
		medium.gridDensity = p->numbers;
	}
	// grid_medium_hittable.h forces sigma_a to 0 (pure scattering) -
	// same convention/reason as cloud above (see that block's own
	// comment) - so a scene that gave a real absorption coefficient
	// silently loses it.
	if (isNonzeroRGB(medium.sigma_a)) {
		warn("uniformgrid medium '" + md.name + "' has a nonzero sigma_a; "
			 "uniformgrid media only model scattering (sigma_a is forced to 0)");
	}
}

// A "nanovdb" medium: the file, the grids and the transform.
inline void readNanoVdbMedium(FlatScene &out, const pbrt_scene::MediumDecl &md, Medium &medium) {
	const auto warn = [&out](const std::string &msg) {
		out.warnings.push_back({0, std::string(), msg});
	};
	// "string filename" - REQUIRED (Medium::nanovdbFilename's own
	// comment); left as written here, resolved to a real filesystem
	// path by pbrt_load.h afterward. An empty/missing filename is
	// caught there (same "empty means unresolved, warn and skip"
	// convention as every other filename field), not here - this
	// loader has no filesystem access to confirm the file even
	// exists yet at flatten() time.
	medium.nanovdbFilename = md.params.getString("filename", "");
	if (medium.nanovdbFilename.empty()) {
		warn("nanovdb medium '" + md.name + "' has no \"string filename\" "
			 "(pbrt-v4 requires one); treated as empty (invisible)");
	}
	medium.nanovdbGridName = md.params.getString("gridname", "density");
	// See Medium::nanovdbTemperatureGridName's own comment.
	medium.nanovdbTemperatureGridName = md.params.getString("temperaturename", "");
	medium.nanovdbLeScale = md.params.getFloat("Lescale", 1.0);
	// See Medium::nanovdbXform's own comment for why this is passed
	// through unbaked instead of pre-composing world<->medium here
	// the way every other medium type's p0/p1 block above does.
	for (int i = 0; i < 16; ++i) medium.nanovdbXform[i] = md.xform.m[i];
	// pbrt_cpu_builder.h's nanovdb path forces sigma_a to 0 (pure
	// scattering) UNLESS a "temperaturename" grid is present (see
	// Medium::nanovdbTemperatureGridName's own comment - real
	// blackbody emission needs a real, nonzero sigma_a to be
	// anything other than a physical no-op) - same convention/
	// reason as uniformgrid just above for the plain-scattering
	// case.
	if (isNonzeroRGB(medium.sigma_a) && medium.nanovdbTemperatureGridName.empty()) {
		warn("nanovdb medium '" + md.name + "' has a nonzero sigma_a; "
			 "this loader's nanovdb media only model scattering (sigma_a is "
			 "forced to 0) unless a \"temperaturename\" grid is also given");
	}
}

// MakeNamedMedium -> out.media (1:1).
inline void flattenMedia(const pbrt_scene::Scene &scene, FlatScene &out) {
	const auto warn = [&out](const std::string &msg) {
		out.warnings.push_back({0, std::string(), msg});
	};

	// ---- media -------------------------------------------------------------
	// MakeNamedMedium, mirrored 1:1 into out.media (same convention as
	// materials above) - ShapeDecl::insideMedium already resolved names to
	// indices during parsing (see pbrt_scene.h's MediumInterface dispatch),
	// so this is a straight per-channel copy, not a second name lookup.
	for (const pbrt_scene::MediumDecl &md : scene.media) {
		Medium medium;
		const bool isCloud = (md.type == "cloud");
		// isRgbGrid is checked 3 separate times below - the flat-"Le"-field
		// skip (this loop, ~10 lines down), the cloud/uniformgrid-only
		// "Le is meaningless" warning condition, and that warning's own
		// hardcoded "homogeneous\"/\"rgbgrid\"" supported-types string -
		// each answers a genuinely different question (does this type use
		// the flat field at all / should THIS type's flat value warn if
		// nonzero / what to tell the user is actually supported), so a
		// code-review finding on this round's own rgbgrid-Le change flagged
		// the duplication risk but a single shared boolean would be a false
		// unification: forcing these into one flag would either be wrong
		// for one of the 3 questions or add an indirection with no real
		// payoff. Kept as 3 explicit sites instead, each now cross-
		// referencing the others by name so a future 4th emissive medium
		// type can't update one without being pointed at the rest.
		const bool isRgbGrid = (md.type == "rgbgrid");
		const bool isUniformGrid = (md.type == "uniformgrid");
		const bool isNanoVdb = (md.type == "nanovdb");
		const bool isHeightFog = (md.type == "heightfog");   // (this renderer's own: a fog in a layer, height_fog.h; the camera medium only)
		if (!isCloud && !isRgbGrid && !isUniformGrid && !isNanoVdb && !isHeightFog && md.type != "homogeneous") {
			warn("medium type '" + md.type + "' is not supported; "
				 "treated as homogeneous with its given sigma_a/sigma_s");
		}
		medium.type = (isCloud || isRgbGrid || isUniformGrid || isNanoVdb || isHeightFog) ? md.type : "homogeneous";
		if (isHeightFog) {
			medium.fogTop = md.params.getFloat("top", 0.0);
			medium.fogThickness = std::max(1e-3, md.params.getFloat("thickness", 1.0));
			medium.fogRadius = std::max(0.0, md.params.getFloat("radius", 0.0));
		}

		// sigma_a/sigma_s/scale/g: for homogeneous, these ARE the medium
		// (a flat RGB colour each). For rgbgrid, these same param NAMES
		// instead mean a flat array of one RGB triple per voxel (see
		// below) - the plain Vec3 read here still runs for that case too
		// (harmless; nothing reads medium.sigma_a/sigma_s for rgbgrid) so
		// this stays one shared block rather than three divergent copies.
		const pbrt_scene::Vec3 defA{medium.sigma_a[0], medium.sigma_a[1], medium.sigma_a[2]};
		const pbrt_scene::Vec3 defS{medium.sigma_s[0], medium.sigma_s[1], medium.sigma_s[2]};
		pbrt_scene::Vec3 sa = md.params.getVec3("sigma_a", defA);
		pbrt_scene::Vec3 ss = md.params.getVec3("sigma_s", defS);
		const double scale = md.params.getFloat("scale", 1.0);
		medium.sigma_a[0] = sa.x * scale; medium.sigma_a[1] = sa.y * scale; medium.sigma_a[2] = sa.z * scale;
		medium.sigma_s[0] = ss.x * scale; medium.sigma_s[1] = ss.y * scale; medium.sigma_s[2] = ss.z * scale;
		medium.g = md.params.getFloat("g", 0.0);

		// "rgb Le"/"float Lescale" - see Medium::Le's own comment on why
		// this round is homogeneous-only, and Medium::Le_r/g/b's own
		// comment for why "rgbgrid" is now ALSO supported, but through a
		// separate, array-shaped field, not this one. This is site 1 of 3
		// where isRgbGrid is checked for this reason (see isRgbGrid's own
		// declaration comment above for the other 2). Skipped entirely for
		// isRgbGrid (handled below instead) - reading it here too would
		// misinterpret that type's own per-voxel "Le" array as a flat
		// colour (see Medium::Le_r's own comment for why). Read
		// unconditionally for the remaining types (harmless for cloud/
		// uniformgrid, matching this loop's own "one shared block"
		// convention for sigma_a/sigma_s above) but only ever
		// nonzero-and-warned for those two, never silently dropped. Uses
		// resolveEmissionColor() (this same file, used by every light's
		// own "L"/"I"), not a plain getVec3 - pbrt-v4 also accepts
		// "blackbody Le" [<kelvin>] (a single number, real Kelvin-to-RGB
		// conversion) for physically-plausible fire/plasma color, which a
		// plain getVec3 (needs >=3 numbers) would silently read as
		// "absent" and default to zero with no diagnostic. Uses the
		// default sRGB working color space (RGBColorSpace::sRGB(),
		// resolveEmissionColor()'s own default) rather than a per-medium
		// ColorSpace capture the way LightDecl::colorSpaceName gives every
		// light - MediumDecl has no equivalent field, and no bundled scene
		// combines a non-default ColorSpace directive with an emissive
		// medium; a real scope cut, not an oversight.
		if (!isRgbGrid) {
			const pbrt_scene::Vec3 le = resolveEmissionColor(md.params, "Le", pbrt_scene::Vec3{0,0,0});
			const double leScale = md.params.getFloat("Lescale", 1.0);
			medium.Le[0] = le.x * leScale; medium.Le[1] = le.y * leScale; medium.Le[2] = le.z * leScale;
		}

		if (isCloud || isRgbGrid || isUniformGrid) {
			// Medium-space bounds (pbrt-v4's own "p0"/"p1", default unit
			// cube - matches CloudMedium::Create/RGBGridMedium::Create's
			// real defaults).
			const pbrt_scene::Vec3 p0 = md.params.getVec3("p0", pbrt_scene::Vec3{0,0,0});
			const pbrt_scene::Vec3 p1 = md.params.getVec3("p1", pbrt_scene::Vec3{1,1,1});
			medium.p0[0]=p0.x; medium.p0[1]=p0.y; medium.p0[2]=p0.z;
			medium.p1[0]=p1.x; medium.p1[1]=p1.y; medium.p1[2]=p1.z;

			// World -> medium-space transform: invert the CTM captured at
			// MakeNamedMedium declaration time (world -> medium is what
			// CloudMedium/rgb_grid_medium_hittable's own sample-time math
			// wants - see each one's own world_to_medium_pt/world_to_medium
			// comment).
			pbrt_scene::Matrix4 worldToMedium;
			if (md.xform.inverseAffine(worldToMedium)) {
				splitAffine(worldToMedium, medium.toMediumMat, medium.toMediumTranslate);
			} else {
				warn("medium '" + md.name + "' has a singular transform "
					 "(zero scale on some axis); it will not render correctly");
			}

			// World-space AABB: the medium-space box [p0,p1] under the
			// medium's own declared CTM (world_from_medium - the INVERSE of
			// the transform just computed above).
			const double p0arr[3] = {p0.x, p0.y, p0.z};
			const double p1arr[3] = {p1.x, p1.y, p1.z};
			aabbOfTransformedBox(md.xform, p0arr, p1arr, medium.worldMin, medium.worldMax);
		}

		// "Le"/"Lescale" (Medium::Le's own comment) - homogeneous only this
		// round; cloud/uniformgrid's real pbrt-v4 emission is a per-voxel
		// grid, not this flat colour, so it's silently meaningless for them
		// - warn rather than let a scene author believe it did something.
		// rgbgrid is deliberately EXCLUDED here now (Medium::Le_r's own
		// comment; this is site 2 of 3, see isRgbGrid's own declaration
		// comment above) - it gets its own real per-voxel "Le" support
		// below, so this check would otherwise fire a misleading "dropped"
		// warning for a scene whose emission this loader now actually
		// honors. The warning message just below is site 3 of 3.
		// Grouped with the sigma_a-dropped warnings just below (same
		// "parameter X is meaningless for type Y" shape), not folded into
		// the AABB-computation block above.
		if ((isCloud || isUniformGrid || isNanoVdb) && isNonzeroRGB(medium.Le)) {
			warn("medium '" + md.name + "' (\"" + md.type + "\") has a nonzero \"Le\", "
				 "but only \"homogeneous\"/\"rgbgrid\" media support emission in this loader; "
				 "the emission is dropped");
		}
		// "nanovdb"'s own real emission mechanism is a SEPARATE named grid
		// ("string temperaturename", real blackbody emission from that
		// grid's values) - not the flat "Le" the check just above already
		// covers. IMPLEMENTED (see Medium::nanovdbTemperatureGridName's own
		// comment) - parsed in the isNanoVdb block below, alongside
		// nanovdbFilename/nanovdbGridName.

		if (isCloud) readCloudMedium(out, md, medium);

		if (isRgbGrid || isUniformGrid) {
			medium.nx = md.params.getInt("nx", 1);
			medium.ny = md.params.getInt("ny", 1);
			medium.nz = md.params.getInt("nz", 1);
			// (a size the file gives is checked before anything is made of it: nx = ny = nz = 100000 asked for 10^15 values and crashed, and a negative one wrapped round)
			const long long cells = static_cast<long long>(medium.nx) * medium.ny * medium.nz;
			if (medium.nx < 1 || medium.ny < 1 || medium.nz < 1 || medium.nx > 4096 || medium.ny > 4096 || medium.nz > 4096 || cells > 512LL * 1024 * 1024) {
				out.warnings.push_back({0, std::string(), "grid medium '" + md.name + "' is " + std::to_string(medium.nx) + " x " + std::to_string(medium.ny) + " x " + std::to_string(medium.nz) +
															  " (each side 1 to 4096, at most 512 million cells); it is left empty"});
				medium.nx = medium.ny = medium.nz = 1;
			}
		}
		const std::size_t voxels = static_cast<std::size_t>(medium.nx)
			* static_cast<std::size_t>(medium.ny) * static_cast<std::size_t>(medium.nz);

		if (isRgbGrid) readRgbGridMedium(out, md, medium, voxels);

		if (isUniformGrid) readUniformGridMedium(out, md, medium, voxels);

		if (isNanoVdb) readNanoVdbMedium(out, md, medium);

		out.media.push_back(medium);
	}
}

// LightSource directives: the infinite light and the punctual lights.
// A light's `"string illuminates"` or `"string shadowsfrom"` (light_link.h) as a mask of link groups: bit g for group g ("rest": 0, the shapes in no group). All of them when
// the light does not say; a name no shape has is said, and counts for nothing.
inline std::uint64_t linkMask(const pbrt_scene::Scene &scene, FlatScene &out, const pbrt_scene::ParamList &params, const char *name) {
	const pbrt_scene::Param *p = params.find(name);
	if (!p) return ~0ull;
	std::uint64_t mask = 0;
	for (const std::string &group : p->strings) {
		if (group == "rest") {
			mask |= 1ull;
			continue;
		}
		// a name is a group, and also every group under it: "cast" is "cast/pip", "cast/ann" ... (a film's characters)
		bool any = false;
		for (std::size_t g = 0; g < scene.linkGroups.size(); ++g) {
			const std::string &have = scene.linkGroups[g];
			if (have == group || (have.size() > group.size() && have.compare(0, group.size(), group) == 0 && have[group.size()] == '/')) {
				mask |= 1ull << (g + 1);
				any = true;
			}
		}
		if (!any) out.warnings.push_back({0, std::string(), std::string("a light's \"") + name + "\" names \"" + group + "\", the linkgroup of no shape"});
	}
	return mask;
}

// A light's `"string lightgroup"` (light_groups.h): its group's index in out.lightGroups, -1 for none.
inline int lightGroupOf(FlatScene &out, const pbrt_scene::ParamList &params) {
	std::string warning;
	const int g = light_groups::indexOf(out.lightGroups, params.getString("lightgroup", ""), &warning);
	if (!warning.empty()) out.warnings.push_back({0, std::string(), warning});
	return g;
}

// Light linking and light groups for one light declaration: whatever punctual lights flattenLights makes of it get its masks and group when this goes out of scope
// (however that declaration's branch ends).
struct PunctualLightLinks {
	FlatScene &out;
	std::size_t from;
	std::uint64_t illuminates, shadowsFrom;
	int lightGroup;
	PunctualLightLinks(const pbrt_scene::Scene &scene, FlatScene &o, const pbrt_scene::ParamList &params)
		: out(o), from(o.punctualLights.size()), illuminates(linkMask(scene, o, params, "illuminates")), shadowsFrom(linkMask(scene, o, params, "shadowsfrom")),
		  lightGroup(lightGroupOf(o, params)) {}
	~PunctualLightLinks() {
		for (std::size_t i = from; i < out.punctualLights.size(); ++i)
			out.punctualLights[i].illuminates = illuminates, out.punctualLights[i].shadowsFrom = shadowsFrom, out.punctualLights[i].lightGroup = lightGroup;
	}
};

inline void flattenLights(const pbrt_scene::Scene &scene, FlatScene &out) {
	const auto warn = [&out](const std::string &msg) {
		out.warnings.push_back({0, std::string(), msg});
	};

	// ---- lights that are not area lights ---------------------------------
	// "infinite" (the environment/sky light) is carried through below - it is
	// usually a scene's main illumination, so dropping it silently produces a
	// render that looks exactly like a shading bug and sends you hunting
	// through the BSDF code instead. point/spot/distant/goniometric/
	// projection are carried through too, into FlatScene::punctualLights -
	// see PunctualLight's own comment for why this is a bridging job rather
	// than new rendering math (this codebase's own C2-C6 showcase scenes
	// already exercise every one of these five on both backends). Anything
	// else genuinely unknown still gets the warn-and-drop this whole block
	// used to give every non-infinite kind.
	//
	// A file can declare several "infinite" lights (the pbrt-v4-scenes Watercolor has two portal lights, one per window), and pbrt sums them. Every PORTAL light is kept: the one
	// with the largest window is `infiniteLight`, the rest are `extraPortalLights`, and the renderers sum them. Anything else beyond one (a second plain sky, or a plain sky
	// next to portals) has no home in the renderers' single environment slot: a plain sky beside portals is kept in `skyBesidePortals` (the last one), without portals the last plain
	// one is `infiniteLight`; the plain lights beyond that are dropped, and a warning says so.
	// Punctual lights have no such limit: every one is kept.
	std::vector<InfiniteLight> infiniteCandidates;
	for (const pbrt_scene::LightDecl &ld : scene.lights) {
		const PunctualLightLinks linkMasks(scene, out, ld.params);   // (light linking and light groups, for the branch below however it ends)
		if (ld.type == "infinite" && linkMasks.lightGroup >= 0) out.skyLightGroup = linkMasks.lightGroup;   // (the sky: one slot, one group)
		if (ld.type == "infinite") {
			InfiniteLight cand;
			cand.present = true;
			const pbrt_scene::Vec3 L = resolveEmissionColor(ld.params, "L", pbrt_scene::Vec3{1, 1, 1}, RGBColorSpaceFromName(ld.colorSpaceName));
			cand.L[0] = L.x; cand.L[1] = L.y; cand.L[2] = L.z;
			cand.scale = ld.params.getFloat("scale", 1.0);
			cand.imageFile = ld.params.getString("filename", "");
			cand.xform = ld.xform;
			// "illuminance" (lux, on an upward-facing patch): pbrt-v4 scales a uniform sky so that its emission, 1 nit before, gives that much - a uniform hemisphere
			// of 1 nit gives pi (lights.cpp, `k_e = Pi`). For a picture pbrt integrates the picture's upper hemisphere, which needs the picture: not done here.
			if (const double E_v = ld.params.getFloat("illuminance", -1.0); E_v > 0.0) {
				if (cand.imageFile.empty() && !ld.params.find("portal")) cand.scale *= E_v / 3.14159265358979323846;
				else warn("infinite light \"illuminance\" with a picture or a portal is not supported (pbrt-v4 integrates the picture's upper hemisphere); ignored");
			}
			// "point3 portal[4]" (pbrt-v4's windowed infinite light) - 4 corner points, 12 numbers total. They are WORLD-space points as written: pbrt maps them with the
			// camera transform alone (`cameraTransform.RenderFromWorld(p)`, lights.cpp) and never through the light's CTM, which only turns the environment picture
			// (pbrt_load.h resamples it). The pbrt-v4-scenes Kroken has `Rotate 340 0 1 0` / `Rotate -90 1 0 0` before its portal light, whose corners are the window's own
			// coordinates: carrying them through that rotation put the window somewhere else and the room got no sky at all. Reset unconditionally first, like every other field above.
			cand.hasPortal = false;
			if (const pbrt_scene::Param *portal = ld.params.find("portal")) {
				if (portal->numbers.size() >= 12) {
					cand.hasPortal = true;
					for (int i = 0; i < 12; ++i) cand.portal[i] = portal->numbers[static_cast<std::size_t>(i)];
				}
			}
			// A portal light with a colour and no picture: pbrt makes a 1 x 1 picture of the colour's XYZ -> sRGB (SpectrumToXYZ), NOT normalised to a luminance of 1 the way a
			// uniform infinite light's L is (it divides `scale` by the sRGB illuminant's photometric value alone). For an RGB L that is the RGB; for "blackbody L" it is the
			// peak-normalised blackbody seen through the sensor: a 6500 K body is (0.92, 0.87, 0.92), Y 0.89, where blackbodyKelvinToRGB() above gave Y 1.
			if (cand.hasPortal && cand.imageFile.empty()) {
				if (const pbrt_scene::Param *lp = ld.params.find("L"); lp && lp->type == "blackbody" && !lp->numbers.empty()) {
					const pbrt_scene::Vec3 c = blackbodyPeakNormalizedRGB(static_cast<float>(lp->numbers[0]), RGBColorSpace::sRGB());
					for (int k = 0; k < 3; ++k) cand.L[k] = (k == 0 ? c.x : k == 1 ? c.y : c.z) / kCIE_Y_integral;
				}
			}
			infiniteCandidates.push_back(cand);
			continue;
		}

		if (ld.type == "point") {
			PunctualLight pl;
			pl.kind = PunctualLightKind::Point;
			const pbrt_scene::Vec3 from = ld.params.getVec3("from", pbrt_scene::Vec3{0, 0, 0});
			transformPoint(ld.xform, from.x, from.y, from.z, pl.pos);
			const pbrt_scene::Vec3 I = resolveEmissionColor(ld.params, "I", pbrt_scene::Vec3{1, 1, 1}, RGBColorSpaceFromName(ld.colorSpaceName));
			pl.intensity[0] = I.x; pl.intensity[1] = I.y; pl.intensity[2] = I.z;
			pl.scale = ld.params.getFloat("scale", 1.0);
			// pbrt-v4's "power" (total emitted Phi, watts): an isotropic point
			// source radiates over the full 4*pi sphere, so I = Phi/(4*pi) -
			// matching PointLight::Create exactly. Multiplies into `scale`
			// rather than replacing it, so a scene giving both "power" and
			// "scale" gets both, the same composition pbrt-v4 itself performs.
			// pbrt does not divide by the colour's luminance either: its
			// SpectrumToPhotometric takes only the illuminant of an RGB light.
			// Guarded on phi > 0, matching pbrt-v4's own PointLight::Create
			// (`if (phi_v > 0)`) - a zero or negative "power" is treated as
			// "not given" rather than literally zeroing or negating the
			// light, which a bare multiply would otherwise do.
			if (const pbrt_scene::Param *p = ld.params.find("power")) {
				if (!p->numbers.empty() && p->numbers[0] > 0.0) {
					const double phi = p->numbers[0];
					pl.scale *= phi / (4.0 * 3.14159265358979323846);   // no division by the colour's luminance: pbrt's SpectrumToPhotometric takes only the illuminant of an RGB spectrum
				}
			}
			out.punctualLights.push_back(pl);
			continue;
		}

		if (ld.type == "spot") {
			PunctualLight pl;
			pl.kind = PunctualLightKind::Spot;
			const pbrt_scene::Vec3 from = ld.params.getVec3("from", pbrt_scene::Vec3{0, 0, 0});
			const pbrt_scene::Vec3 to = ld.params.getVec3("to", pbrt_scene::Vec3{0, 0, 1});
			double worldFrom[3], worldTo[3];
			transformPoint(ld.xform, from.x, from.y, from.z, worldFrom);
			transformPoint(ld.xform, to.x, to.y, to.z, worldTo);
			for (int c = 0; c < 3; ++c) pl.pos[c] = worldFrom[c];
			// The cone axis - the direction the spot is AIMED, from `from`
			// toward `to` - both already CTM-transformed, so subtracting
			// cancels the translation and leaves exactly the CTM's rotation
			// applied to pbrt's own local (to - from) (matches pbrt-v4
			// SpotLight::Create's w = Normalize(to - from) exactly, just
			// composed with the CTM by transforming the endpoints instead of
			// building pbrt's own dirToZ rotation matrix - unnecessary here
			// since SpotLightData only ever needs the resulting axis, not a
			// full light-space frame).
			for (int c = 0; c < 3; ++c) pl.dir[c] = worldTo[c] - worldFrom[c];
			normalizeOrDefault(pl.dir, 0, -1, 0);
			const pbrt_scene::Vec3 I = resolveEmissionColor(ld.params, "I", pbrt_scene::Vec3{1, 1, 1}, RGBColorSpaceFromName(ld.colorSpaceName));
			pl.intensity[0] = I.x; pl.intensity[1] = I.y; pl.intensity[2] = I.z;
			pl.scale = ld.params.getFloat("scale", 1.0);
			pl.coneAngleDeg = ld.params.getFloat("coneangle", 30.0);
			// pbrt-v4's own SpotLight::Create passes coneangle - conedeltaangle
			// through UNCLAMPED (see SpotLight's cosFalloffStart/cosFalloffEnd
			// construction) - a conedeltaangle larger than coneangle yields a
			// negative falloffStartAngleDeg, which is not malformed: cos() is
			// even, so cos(-10deg) == cos(10deg), meaning a negative start angle
			// still produces a real, meaningful full-intensity core (just one
			// that extends slightly past the geometric cone axis) rather than
			// collapsing to cosFalloffStart=1.0 (no core at all), which is what
			// clamping to 0 here used to produce.
			const double delta = ld.params.getFloat("conedeltaangle", 5.0);
			pl.falloffStartAngleDeg = pl.coneAngleDeg - delta;
			// pbrt-v4's "power" (total emitted Phi, watts): SpotLight::Create's
			// own closed-form integral of the cone's falloff region, treating
			// it as contributing half its solid angle on average -
			// Phi = 2*pi*I*((1-cosFalloffStart) + (cosFalloffStart-cosFalloffEnd)/2)
			// - inverted for I here exactly like the point-light case just
			// above (same "multiplies into scale" composition with an explicit
			// "scale" param, same phi > 0 guard matching pbrt-v4's own SpotLight::Create).
			if (const pbrt_scene::Param *p = ld.params.find("power")) {
				if (!p->numbers.empty() && p->numbers[0] > 0.0) {
					const double phi = p->numbers[0];
					const double cosFalloffStart = std::cos(pl.falloffStartAngleDeg * 3.14159265358979323846 / 180.0);
					const double cosFalloffEnd = std::cos(pl.coneAngleDeg * 3.14159265358979323846 / 180.0);
					const double kE = 2.0 * 3.14159265358979323846 *
						((1.0 - cosFalloffStart) + (cosFalloffStart - cosFalloffEnd) * 0.5);
					if (std::fabs(kE) > 1e-12) {
						pl.scale *= phi / kE;   // likewise no luminance division
					} else {
						// A degenerate cone (e.g. "coneangle" 0) has no solid
						// angle to spread Phi over - same class of problem as
						// the area-light zero-area case below, so it gets the
						// same treatment: warn and leave "power" unconverted
						// rather than silently dropping it with no diagnostic.
						warn("spot light \"power\" given but the cone is degenerate "
							 "(coneangle/conedeltaangle leave no solid angle to spread "
							 "it over); \"scale\"/I used as given instead");
					}
				}
			}
			out.punctualLights.push_back(pl);
			continue;
		}

		if (ld.type == "distant") {
			PunctualLight pl;
			pl.kind = PunctualLightKind::Distant;
			const pbrt_scene::Vec3 from = ld.params.getVec3("from", pbrt_scene::Vec3{0, 0, 0});
			const pbrt_scene::Vec3 to = ld.params.getVec3("to", pbrt_scene::Vec3{0, 0, 1});
			double worldFrom[3], worldTo[3];
			transformPoint(ld.xform, from.x, from.y, from.z, worldFrom);
			transformPoint(ld.xform, to.x, to.y, to.z, worldTo);
			// dir must be `wi` - the direction FROM ANY SHADING POINT TOWARD
			// THE LIGHT (see PunctualLight::dir's own comment) - i.e. the
			// OPPOSITE of the direction sunlight travels, which runs from
			// `from` toward `to`. pbrt-v4 DistantLight::Create builds this
			// same vector (w = Normalize(from - to)) for exactly this reason
			// - its own SampleLi later returns renderFromLight(local +Z),
			// and its local +Z axis is constructed to already equal that
			// (from - to) direction, so this is the same result reached
			// without needing pbrt's intermediate rotation-to-Z matrix.
			for (int c = 0; c < 3; ++c) pl.dir[c] = worldFrom[c] - worldTo[c];
			normalizeOrDefault(pl.dir, 0, 1, 0);
			const pbrt_scene::Vec3 L = resolveEmissionColor(ld.params, "L", pbrt_scene::Vec3{1, 1, 1}, RGBColorSpaceFromName(ld.colorSpaceName));
			pl.intensity[0] = L.x; pl.intensity[1] = L.y; pl.intensity[2] = L.z;
			pl.scale = ld.params.getFloat("scale", 1.0);
			// "illuminance" (lux): pbrt-v4's DistantLight::Create multiplies its scale by it, the emission having been made 1 nit first (it was ignored here, so a
			// scene that set the sun's brightness this way was lit by a 1-lux sun).
			if (const double E_v = ld.params.getFloat("illuminance", -1.0); E_v > 0.0) pl.scale *= E_v;
			// pbrt-v4 itself has no "power" for a distant light - unlike every
			// other punctual kind, its irradiance doesn't fall off with
			// distance, so there is no finite total Phi to solve for without
			// an arbitrary reference area (pbrt-v4 reads "illuminance" instead,
			// above). Warn rather than silently ignoring a "power" a scene
			// author expected to matter.
			if (ld.params.find("power")) {
				warn("distant light \"power\" is not supported (pbrt-v4 has no "
					 "finite total power for a directional light); ignored");
			}
			out.punctualLights.push_back(pl);
			continue;
		}

		if (ld.type == "goniometric") {
			PunctualLight pl;
			pl.kind = PunctualLightKind::Goniometric;
			transformPoint(ld.xform, 0.0, 0.0, 0.0, pl.pos);
			worldToLightRotation(ld.xform, pl.worldToLight);
			// pbrt-v4 (GoniometricLight::Create) puts a swap of y and z between the light's frame and the profile's: the profile's own pole, the z the equal-area
			// mapping is built around, is the light's +y, "up" for a light that has not been turned. The direction handed to the profile is the swap of the light-space one.
			for (int c = 0; c < 3; ++c) std::swap(pl.worldToLight[3 + c], pl.worldToLight[6 + c]);
			const pbrt_scene::Vec3 I = resolveEmissionColor(ld.params, "I", pbrt_scene::Vec3{1, 1, 1}, RGBColorSpaceFromName(ld.colorSpaceName));
			pl.intensity[0] = I.x; pl.intensity[1] = I.y; pl.intensity[2] = I.z;
			pl.scale = ld.params.getFloat("scale", 1.0);
			const std::string file = ld.params.getString("filename", "");
			if (!file.empty()) {
				pl.hadImageFilename = true;
				pl.filename = file;
			}
			// pbrt-v4's real "power" for a goniometric light weighs the
			// profile image's own luminance distribution into the conversion
			// (GoniometricLight::Create) - out of scope here (see this
			// loader's matching scope decision for ReverseOrientation and
			// other narrowly-closed gaps this session); warn rather than
			// silently ignore it.
			if (ld.params.find("power")) {
				warn("goniometric light \"power\" is not supported (its real "
					 "pbrt-v4 conversion depends on the profile image); ignored");
			}
			out.punctualLights.push_back(pl);
			continue;
		}

		if (ld.type == "projection") {
			PunctualLight pl;
			pl.kind = PunctualLightKind::Projection;
			transformPoint(ld.xform, 0.0, 0.0, 0.0, pl.pos);
			worldToLightRotation(ld.xform, pl.worldToLight);
			pl.scale = ld.params.getFloat("scale", 1.0);
			pl.fovDeg = ld.params.getFloat("fov", 90.0);
			const std::string file = ld.params.getString("filename", "");
			// pbrt-v4 requires "filename" (a projection light has no other
			// way to describe what it projects) - a scene that omits it is
			// itself malformed, worth a warning even though the light still
			// renders (as a uniform white beam) rather than being dropped.
			if (!file.empty()) {
				pl.hadImageFilename = true;
				pl.filename = file;
			} else {
				warn("light 'projection' has no \"filename\" (pbrt-v4 requires "
					 "one); emitted as a uniform white beam of the requested "
					 "shape/scale/aim instead of failing outright");
			}
			// Same scope decision as goniometric just above: real pbrt-v4
			// weighs the projected slide image's own luminance into the
			// conversion (ProjectionLight::Create) - warn instead of ignoring.
			if (ld.params.find("power")) {
				warn("projection light \"power\" is not supported (its real "
					 "pbrt-v4 conversion depends on the projected image); ignored");
			}
			out.punctualLights.push_back(pl);
			continue;
		}

		warn("light source '" + ld.type + "' is not supported and was dropped; "
			 "the scene will be darker than intended");
	}
	if (!infiniteCandidates.empty()) {
		const auto windowArea = [](const InfiniteLight &l) {   // the portal's area: |p01 x p03|
			if (!l.hasPortal) return 0.0;
			const double ax = l.portal[3] - l.portal[0], ay = l.portal[4] - l.portal[1], az = l.portal[5] - l.portal[2];
			const double bx = l.portal[9] - l.portal[0], by = l.portal[10] - l.portal[1], bz = l.portal[11] - l.portal[2];
			const double cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
			return std::sqrt(cx * cx + cy * cy + cz * cz);
		};
		std::vector<InfiniteLight> portals;
		for (const InfiniteLight &c : infiniteCandidates)
			if (c.hasPortal) portals.push_back(c);
		int dropped = 0;
		if (!portals.empty()) {
			std::stable_sort(portals.begin(), portals.end(), [&](const InfiniteLight &x, const InfiniteLight &y) { return windowArea(x) > windowArea(y); });
			out.infiniteLight = portals.front();
			out.extraPortalLights.assign(portals.begin() + 1, portals.end());
			const int plain = static_cast<int>(infiniteCandidates.size() - portals.size());
			for (auto it = infiniteCandidates.rbegin(); it != infiniteCandidates.rend(); ++it)
				if (!it->hasPortal) { out.skyBesidePortals = *it; break; }
			dropped = plain > 1 ? plain - 1 : 0;
		} else {
			out.infiniteLight = infiniteCandidates.back();
			dropped = static_cast<int>(infiniteCandidates.size()) - 1;
		}
		if (dropped > 0) {
			warn("the scene has " + std::to_string(infiniteCandidates.size()) + " \"infinite\" lights, which pbrt sums; this renderer has one environment slot, so " + std::to_string(dropped) +
				 " plain (non-portal) one" + (dropped > 1 ? "s were" : " was") + " dropped (portal lights are all kept: " + std::to_string(portals.size()) + ", and one plain light beside them)");
		}
	}
}

// AreaLightSource directives -> out.areaLights.
inline void flattenAreaLights(const pbrt_scene::Scene &scene, FlatScene &out) {
	// ---- area lights -----------------------------------------------------
	for (const pbrt_scene::LightDecl &ld : scene.areaLights) {
		Emission e;
		// "blackbody L" declares a colour TEMPERATURE under the parameter
		// name L (resolveEmissionColor's own comment) - barcelona-pavilion's/
		// contemporary-bathroom's real night-lighting area lights use this.
		const pbrt_scene::Vec3 L = resolveEmissionColor(ld.params, "L", pbrt_scene::Vec3{1, 1, 1}, RGBColorSpaceFromName(ld.colorSpaceName));
		e.L[0] = L.x; e.L[1] = L.y; e.L[2] = L.z;
		e.scale = ld.params.getFloat("scale", 1.0);
		// Round 6 Phase 4: spatially-varying emission (an image mapped onto
		// the shape) and real two-sided emission - see Emission's own
		// comment on each field.
		e.filename = ld.params.getString("filename", "");
		e.twoSided = ld.params.getBool("twosided", false);
		e.sampled = ld.params.getBool("sampled", true);   // (a glow that is no light to aim at: Emission::sampled)
		if (const pbrt_scene::Param *p = ld.params.find("power")) {
			if (!p->numbers.empty()) {
				e.hasPower = true;
				e.power = p->numbers[0];
			}
		}
		e.illuminates = linkMask(scene, out, ld.params, "illuminates");
		e.lightGroup = lightGroupOf(out, ld.params);
		out.areaLights.push_back(e);
	}
}

// Camera, film and screen window.
inline void flattenCamera(const pbrt_scene::Scene &scene, FlatScene &out) {
	const auto warn = [&out](const std::string &msg) {
		out.warnings.push_back({0, std::string(), msg});
	};

	// ---- camera ----------------------------------------------------------
	out.camera = cameraFromWorldToCamera(scene.worldToCamera, scene.cameraLookAtDistance);
	{
		const double fov = scene.cameraFov();
		// pbrt's fov applies to the NARROWER image axis. Ours is always
		// vertical, so on a landscape frame they agree and on a portrait one
		// they do not - taking pbrt's number as vertical unconditionally
		// silently mis-frames every portrait scene.
		if (scene.xResolution >= scene.yResolution) {
			out.camera.vfov = fov;
		} else {
			const double aspect = (scene.yResolution > 0)
								  ? static_cast<double>(scene.xResolution) / scene.yResolution
								  : 1.0;
			const double halfRad = fov * 0.5 * 3.14159265358979323846 / 180.0;
			const double tanV = (aspect > 0.0) ? std::tan(halfRad) / aspect : std::tan(halfRad);
			out.camera.vfov = 2.0 * std::atan(tanV) * 180.0 / 3.14159265358979323846;
		}
		out.camera.aperture = scene.cameraParams.getFloat("lensradius", 0.0) * 2.0;
		// the aperture's shape (aperture_shape.h: Cycles' blades, rotation and ratio; our extension)
		out.camera.apertureBlades = std::max(0, std::min(16, scene.cameraParams.getInt("blades", 0)));
		out.camera.apertureRotation = scene.cameraParams.getFloat("bladesrotation", 0.0);
		out.camera.apertureRatio = std::clamp(scene.cameraParams.getFloat("apertureratio", 1.0), 0.1, 10.0);
		// pbrt-v4 spells this "focaldistance" for perspective/orthographic but
		// "focusdistance" for realistic - reading both means a scene does not
		// silently keep the 1e6 sentinel just because it used the other
		// camera type's spelling.
		out.camera.focusDistance = scene.cameraParams.getFloat("focaldistance",
									   scene.cameraParams.getFloat("focusdistance",
										   out.camera.focusDistance));

		// Non-perspective cameras. lookfrom/lookat/up above already came from
		// the world-to-camera matrix, which every pbrt camera type shares -
		// only the type-specific parameters need reading here.
		out.camera.type = scene.cameraType;
		if (const pbrt_scene::Param *sw = scene.cameraParams.find("screenwindow")) {
			if (sw->numbers.size() >= 4) {
				out.camera.hasScreenWindow = true;
				out.camera.screenWindow[0] = sw->numbers[0];
				out.camera.screenWindow[1] = sw->numbers[1];
				out.camera.screenWindow[2] = sw->numbers[2];
				out.camera.screenWindow[3] = sw->numbers[3];
			} else {
				warn("a camera's \"screenwindow\" needs 4 numbers (xmin xmax ymin "
					 "ymax); ignored");
			}
		}
		if (out.camera.type == "spherical" || out.camera.type == "environment") {
			out.camera.sphericalMapping =
				scene.cameraParams.getString("mapping", out.camera.sphericalMapping);
		} else if (out.camera.type == "realistic") {
			out.camera.lensFile = scene.cameraParams.getString("lensfile", "");
			out.camera.apertureDiameterMM =
				scene.cameraParams.getFloat("aperturediameter", out.camera.apertureDiameterMM);
			out.camera.filmDiagonalMM =
				scene.cameraParams.getFloat("filmdiag", out.camera.filmDiagonalMM);
			if (out.camera.lensFile.empty()) {
				warn("a realistic camera has no \"lensfile\"; rendering with a "
					 "perspective camera instead");
				out.camera.type = "perspective";
			}
		}

		// Camera motion blur (pbrt-v4's real ActiveTransform "StartTime"/
		// "EndTime" idiom - see Camera::isAnimated's own comment). shutteropen/
		// shutterclose are read unconditionally (harmless when the camera
		// turns out not to be animated - CPU's own camera.h never consults
		// them unless camera_is_animated is set).
		out.camera.shutterOpen = scene.cameraParams.getFloat("shutteropen", out.camera.shutterOpen);
		out.camera.shutterClose = scene.cameraParams.getFloat("shutterclose", out.camera.shutterClose);
		out.camera.isAnimated = scene.cameraIsAnimated();
		if (out.camera.isAnimated) {
			const Camera endCam = cameraFromWorldToCamera(scene.worldToCameraEnd, scene.cameraLookAtDistanceEnd);
			for (int i = 0; i < 3; ++i) {
				out.camera.lookfrom1[i] = endCam.lookfrom[i];
				out.camera.lookat1[i] = endCam.lookat[i];
			}
			// TransformTimes's own distinct value has no effect (see
			// Camera::shutterOpen's own comment on why) - loud, not silent,
			// when a scene actually asked for one, so a scene author relying
			// on TransformTimes differing from shutteropen/shutterclose (an
			// unusual, but real pbrt-v4 combination) finds out rather than
			// silently getting shutteropen/shutterclose's own timing instead.
			if (scene.transformTimeStart != out.camera.shutterOpen ||
				scene.transformTimeEnd != out.camera.shutterClose) {
				warn("scene declares TransformTimes [" + std::to_string(scene.transformTimeStart) +
					 ", " + std::to_string(scene.transformTimeEnd) + "] distinct from the camera's own "
					 "shutteropen/shutterclose [" + std::to_string(out.camera.shutterOpen) + ", " +
					 std::to_string(out.camera.shutterClose) + "] - this renderer's camera motion blur "
					 "uses shutteropen/shutterclose for both the keyframe times and the shutter "
					 "sampling window, so TransformTimes's own distinct value has no effect");
			}
		}
	}
}

} // namespace flatten_detail
