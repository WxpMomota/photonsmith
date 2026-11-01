// pbrt_gpu_strands.h -- fine strands as triangles, a part of pbrt_gpu_builder.h (included inside its namespace, after BuildCtx): the curves that ask for it are diced into triangles, which the
// card's ray tracing hardware tests, not bilinear patches, which a program of ours tests.

// A curve that asked to be triangles and can be: no fibre direction reaches a hair material through a triangle, and a strand that gives out light stays what the light sampling knows.
inline bool asTriangles(const pbrt_flatten::FlatScene &scene, const pbrt_flatten::Curve &cv) {
	if (!cv.diceTriangles || cv.areaLight >= 0) return false;
	return cv.material < 0 || static_cast<std::size_t>(cv.material) >= scene.materials.size() || scene.materials[static_cast<std::size_t>(cv.material)].kind != pbrt_flatten::MaterialKind::Hair;
}

// The strands that asked to be triangles: straight into the triangle list, after everything that list's indices are kept for (lights, the EndTime vertices). A coat is hundreds of thousands
// of strands, each eighteen triangles, so they go through neither a copy of the scene nor pbrt_flatten::Triangle.
inline void addStrandTriangles(BuildCtx &c) {
	using namespace detail;
	const auto &scene = c.scene;
	auto &out = c.out;
	std::size_t added = 0;
	for (const pbrt_flatten::Curve &cv : scene.curves)
		if (asTriangles(scene, cv)) added += static_cast<std::size_t>(cv.nSegments) * cv.diceLength * cv.diceRadial * 2;
	if (added == 0) return;
	out.triangles.reserve(out.triangles.size() + added);
	std::vector<curve_tessellate::Quad> quads;
	for (const pbrt_flatten::Curve &cv : scene.curves) {
		if (!asTriangles(scene, cv)) continue;
		const int matIdx = c.materialIndex(cv.material, cv.areaLight);
		for (int seg = 0; seg < cv.nSegments; ++seg) {
			float cp[4][3];
			for (int i = 0; i < 4; ++i)
				for (int a = 0; a < 3; ++a) cp[i][a] = static_cast<float>(cv.cp[(static_cast<std::size_t>(seg) * 4 + i) * 3 + a]);
			const double t0 = static_cast<double>(seg) / cv.nSegments, t1 = static_cast<double>(seg + 1) / cv.nSegments;
			quads.clear();
			curve_tessellate::tessellate(cp, 0.0f, 1.0f, static_cast<float>(cv.width0 + (cv.width1 - cv.width0) * t0), static_cast<float>(cv.width0 + (cv.width1 - cv.width0) * t1), cv.diceLength, cv.diceRadial, quads);
			for (const curve_tessellate::Quad &q : quads) {
				const float *corners[4] = {q.p00, q.p10, q.p01, q.p11};
				static const int kTris[2][3] = {{0, 1, 3}, {0, 3, 2}};
				for (const auto &t : kTris) {
					TriangleData td = {};
					td.p0 = make_float3(corners[t[0]][0], corners[t[0]][1], corners[t[0]][2]);
					td.p1 = make_float3(corners[t[1]][0], corners[t[1]][1], corners[t[1]][2]);
					td.p2 = make_float3(corners[t[2]][0], corners[t[2]][1], corners[t[2]][2]);
					td.uv0 = make_float2(0.0f, 0.0f), td.uv1 = make_float2(1.0f, 0.0f), td.uv2 = make_float2(1.0f, 1.0f);   // (as addTrianglesAndQuads gives a triangle with no uvs)
					td.hasUVs = true;
					td.materialIdx = matIdx;
					out.triangles.push_back(td);
					if (!out.triangleEndVertices.empty()) {   // (the moving triangles' EndTime list stays one to one with the triangles: strands stay still)
						out.triangleEndVertices.push_back(td.p0);
						out.triangleEndVertices.push_back(td.p1);
						out.triangleEndVertices.push_back(td.p2);
					}
				}
			}
		}
		c.stats.curveSegments += static_cast<std::size_t>(cv.nSegments);
	}
	c.stats.triangles = out.triangles.size();
}
