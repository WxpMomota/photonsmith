// optix_intersection_sphere.h -- Sphere intersection + closest-hit programs
// Included by optix_programs.cu
// (the cloud / RGB-grid / box helpers that were here are in optix_device_media_helpers.h, included by optix_device_helpers.h)

extern "C" __global__ void __intersection__sphere() {
	// Get primitive index from OptiX
	const unsigned int primIdx = optixGetPrimitiveIndex();

	// A primitive index is local to its GAS, so an instanced definition's
	// sphere 0 and the scene's sphere 0 are different spheres. See
	// LaunchParams::instancePrimBase - one table serves triangles and spheres
	// alike because a GAS only ever holds one of the two.
	const int instBase = params.instancePrimBase
		? params.instancePrimBase[optixGetInstanceId()] : -1;
	const unsigned int sphBase = (instBase >= 0) ? (unsigned int)instBase : 0u;

	// Fetch sphere data from device array
	const SphereData& sphere = params.spheres[sphBase + primIdx];

	// Motion blur: interpolate to this ray's time. lerp(center, center1, 0)
	// == center exactly, so this is a provable no-op for every sphere in
	// every scene that doesn't set motionBlurEnabled (optix_raygen.h always
	// passes rayTime=0.0f in that case) - center1 can hold garbage there and
	// it's still safe. Matches src/TheRestOfYourLife/sphere.h's
	// `current_center = center.at(r.time())` linear interpolation exactly.
	const float ray_time = optixGetRayTime();
	const float3 center = make_float3(
		sphere.center.x + ray_time * (sphere.center1.x - sphere.center.x),
		sphere.center.y + ray_time * (sphere.center1.y - sphere.center.y),
		sphere.center.z + ray_time * (sphere.center1.z - sphere.center.z)
	);

	// OBJECT space, not world. The sphere's centre/radius are stored in
	// whatever space its GAS was built in, and for an instanced definition
	// that is the definition's own - so the test has to happen there too.
	//
	// This is not a special case for instanced geometry: the scene's own GAS
	// instances carry an identity transform, for which OptiX's object-space
	// ray IS the world-space ray, so the non-instanced path is unchanged by
	// construction rather than by a branch. Intersecting in object space is
	// also what makes a placement with a non-uniform scale come out as the
	// ellipsoid the scene asked for.
	//
	// t needs no rescaling between the two spaces: OptiX transforms the ray
	// direction WITHOUT renormalising it, so the same t parameter names the
	// same point in both, and reporting it against the world-space tmin/tmax
	// below is correct.
	const float3 ray_orig = optixGetObjectRayOrigin();
	const float3 ray_dir = optixGetObjectRayDirection();
	const float ray_tmin = optixGetRayTmin();
	const float ray_tmax = optixGetRayTmax();

	if (sphere.shapeKind == GpuMediumShapeKind::ClippedSphere) {
		// Partial sphere (z-slab + phi-wedge clip) - see GpuMediumShapeKind's
		// comment in optix_types.h. Unlike the Sphere/Box branches above,
		// this shape carries its OWN further object<->object affine
		// (sphere.o2w/w2o, in the SAME space as center/center1/radius above -
		// the instance definition's own for an instanced sphere, world space
		// for a top-level one, per pbrt_flatten.h's ShapeWork loop), rather
		// than baking a full world-space center/radius the way a full
		// (unclipped) sphere does. Composed through the already-computed
		// OBJECT-space ray_orig/ray_dir above (which already undo any real
		// OptiX instance/placement transform, identity for top-level - same
		// reasoning as the plain-Sphere/Box branches), THEN through
		// sphere.w2o via dc_apply_point/dc_apply_vector for this shape's own
		// further clip-local space - NOT the raw WORLD-space ray directly
		// (a real bug this replaced: reading the world ray and applying only
		// sphere.w2o silently skipped an INSTANCED clipped sphere's own
		// placement transform entirely, since sphere.w2o alone only ever
		// undoes the definition-local affine - the top-level case was
		// unaffected only because its own instance transform is always
		// identity). t is unaffected by this (dc_apply_vector is
		// deliberately not renormalised - see its own comment), so reporting
		// the resulting t against the world-space ray_tmin/ray_tmax below is
		// still correct, exactly like the plain-sphere branch's own
		// reasoning above.
		//
		// Quadric solve + pbrt-v4's own dual-root z/phi clip-rejection
		// algorithm, mirroring SphereShape<T>::intersect() (src/shared/
		// shapes.h) exactly - ported to plain float (this file's existing
		// sphere/box branches are float throughout; CPU's version uses
		// double internally for the quadratic solve, a precision headroom
		// this port doesn't carry, matching every other GPU intersection
		// program in this codebase).
		const float3 oro = dc_apply_point(sphere.w2o, ray_orig);
		const float3 ord = dc_apply_vector(sphere.w2o, ray_dir);
		const float r = sphere.radiusLocal;

		const float qa = dot(ord, ord);
		const float qb = 2.0f * dot(oro, ord);
		const float qc = dot(oro, oro) - r * r;
		const float disc = qb * qb - 4.0f * qa * qc;
		if (disc < 0.0f) return;  // No hit
		const float sqrt_disc = sqrtf(disc);
		const float q = (qb < 0.0f) ? -0.5f * (qb - sqrt_disc) : -0.5f * (qb + sqrt_disc);
		float t0 = q / qa;
		float t1 = qc / q;
		if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
		if (t0 > ray_tmax || t1 < ray_tmin) return;

		float t_hit = (t0 >= ray_tmin) ? t0 : t1;
		if (t_hit > ray_tmax) return;

		float hx = oro.x + t_hit * ord.x, hy = oro.y + t_hit * ord.y, hz = oro.z + t_hit * ord.z;
		float len = sqrtf(hx*hx + hy*hy + hz*hz);
		if (len > 0.0f) { hx *= r/len; hy *= r/len; hz *= r/len; }

		if (hz < sphere.zMin || hz > sphere.zMax) {
			// Try the other root.
			t_hit = (t_hit == t0) ? t1 : t0;
			if (t_hit < ray_tmin || t_hit > ray_tmax) return;
			hx = oro.x + t_hit * ord.x; hy = oro.y + t_hit * ord.y; hz = oro.z + t_hit * ord.z;
			len = sqrtf(hx*hx + hy*hy + hz*hz);
			if (len > 0.0f) { hx *= r/len; hy *= r/len; hz *= r/len; }
			if (hz < sphere.zMin || hz > sphere.zMax) return;
		}

		float phi = atan2f(hy, hx);
		if (phi < 0.0f) phi += 2.0f * 3.14159265358979323846f;
		if (phi > sphere.phiMax) {
			// Try the other root.
			float t_other = (t_hit == t0) ? t1 : t0;
			if (t_other < ray_tmin || t_other > ray_tmax) return;
			hx = oro.x + t_other * ord.x; hy = oro.y + t_other * ord.y; hz = oro.z + t_other * ord.z;
			len = sqrtf(hx*hx + hy*hy + hz*hz);
			if (len > 0.0f) { hx *= r/len; hy *= r/len; hz *= r/len; }
			phi = atan2f(hy, hx);
			if (phi < 0.0f) phi += 2.0f * 3.14159265358979323846f;
			if (hz < sphere.zMin || hz > sphere.zMax || phi > sphere.phiMax) return;
			t_hit = t_other;
		}

		// No attributes packed: __closesthit__sphere's own ClippedSphere
		// branch recomputes the object-space hit point from scratch via
		// sphere.w2o applied to the world hit point (t * ray direction) -
		// the same "cheaper to recompute than widen attributes" choice
		// __closesthit__disk makes (optix_intersection_disk_cylinder.h's own
		// comment) for the identical reason.
		optixReportIntersection(t_hit, 0, 0, 0, 0, 0);
		return;
	}

	if (sphere.shapeKind == GpuMediumShapeKind::Box) {
		// Box (AABB slab test) variant - see GpuMediumShapeKind's comment in
		// optix_types.h. No motion blur combination exists for this shape
		// (center/center1 above are unused), so this tests directly against
		// the ray as given, exactly mirroring the sphere branch's own
		// dual-root "nearest root in [ray_tmin,ray_tmax], else the far root"
		// selection below. Attributes are unused by the box path of
		// __closesthit__sphere (its box bounds are static, so it re-reads
		// sphere.boxMin/boxMax directly instead - see that function's own
		// comment), so 0 is reported for all four.
		float t_near, t_far;
		if (!box_slab_intersect(ray_orig, ray_dir, sphere.boxMin, sphere.boxMax, t_near, t_far))
			return;  // No hit
		float root = t_near;
		if (root < ray_tmin || root > ray_tmax) {
			root = t_far;
			if (root < ray_tmin || root > ray_tmax)
				return;  // No valid hit
		}
		optixReportIntersection(root, 0, 0, 0, 0, 0);
		return;
	}

	// Sphere intersection
	const float3 oc = ray_orig - center;
	const float a = dot(ray_dir, ray_dir);
	const float half_b = dot(oc, ray_dir);
	// pbrt-v4's improved-precision discriminant (Sphere::BasicIntersect): half_b^2 - a*c subtracts two float numbers near |oc|^2 (2.5e5 for a sphere 500 units
	// away) and so puts the roots ~0.005 units off - enough for a shadow ray to a light sphere to hit its surface before the point it was aimed at and be counted
	// as blocked. Taking the length of v = oc - (half_b / a) d, the closest approach of the ray's line to the centre, gives a * (r + |v|) * (r - |v|) exactly.
	const float3 closest = oc - (half_b / a) * ray_dir;
	const float closest_len = length(closest);
	const float discriminant = a * (sphere.radius + closest_len) * (sphere.radius - closest_len);

	if (discriminant < 0.0f) return;  // No hit

	const float sqrtd = sqrtf(discriminant);

	// Find nearest root in valid range
	float root = (-half_b - sqrtd) / a;
	if (root < ray_tmin || root > ray_tmax) {
		root = (-half_b + sqrtd) / a;
		if (root < ray_tmin || root > ray_tmax)
			return;  // No valid hit
	}

	// Report intersection
	optixReportIntersection(
		root,                      // t value
		0,                         // hit kind
		__float_as_int(center.x),  // attribute 0 (time-interpolated center, for normal calc)
		__float_as_int(center.y),  // attribute 1
		__float_as_int(center.z),  // attribute 2
		__float_as_int(sphere.radius)     // attribute 3
	);
}

//==============================================================================
// Sphere Closest Hit Program
//==============================================================================

// Where the world ray (unit_dir) enters and leaves a Medium or DielectricMedium sphere or box, entry clamped to 0. center, radius and the box's corners are in the
// shape's OWN space, which for an instanced shape is not the world's, so the ray is brought into it first; t is the same in both spaces because the transform is linear.
// (A ray used as-is placed an instanced fog ball's chord in the wrong place, and the ball rendered black: consistency sweep `shapeform`.) Not instanced: the world ray as it was.
__device__ __forceinline__ void interface_shape_chord(
	const SphereData& sphere, bool is_box, int instBase, float3 ray_orig, float3 unit_dir, float3 center, float radius, float& t_near, float& t_far
) {
	float3 o = ray_orig, d = unit_dir;
	if (instBase >= 0) {
		o = optixTransformPointFromWorldToObjectSpace(ray_orig);
		d = optixTransformVectorFromWorldToObjectSpace(unit_dir);
	}
	if (is_box) {
		float bn, bf;
		box_slab_intersect(o, d, sphere.boxMin, sphere.boxMax, bn, bf);
		t_near = fmaxf(0.0f, bn);
		t_far  = bf;
		return;
	}
	const float3 oc = o - center;
	const float a = dot(d, d);
	const float half_b = dot(oc, d);
	const float disc = fmaxf(0.0f, half_b * half_b - a * (dot(oc, oc) - radius * radius));
	const float sq = sqrtf(disc);
	t_near = fmaxf(0.0f, (-half_b - sq) / a);
	t_far  = (-half_b + sq) / a;
}

extern "C" __global__ void __closesthit__sphere() {
	// Get primitive index
	const unsigned int primIdx = optixGetPrimitiveIndex();

	// Same base lookup the intersection program above does - see its comment.
	// Read again here (rather than passed through an attribute) because it
	// also gates the object-to-world normal transform below.
	const int instBase = params.instancePrimBase
		? params.instancePrimBase[optixGetInstanceId()] : -1;
	const unsigned int sphBase = (instBase >= 0) ? (unsigned int)instBase : 0u;

	// Fetch sphere data from device array
	const SphereData& sphere = params.spheres[sphBase + primIdx];

	// Reconstruct sphere data from attributes. OBJECT space, matching the
	// intersection program that reported them; identical to world space for
	// every non-instanced sphere, whose instance transform is the identity.
	const float3 sphere_center = make_float3(
		__int_as_float(optixGetAttribute_0()),
		__int_as_float(optixGetAttribute_1()),
		__int_as_float(optixGetAttribute_2())
	);
	const float sphere_radius = __int_as_float(optixGetAttribute_3());

	// Get hit point. The shading position and view direction stay in WORLD
	// space - they feed lighting, which is world-space throughout - while the
	// surface normal is derived in object space just below and brought back.
	const float t = optixGetRayTmax();
	const float3 ray_orig = optixGetWorldRayOrigin();
	const float3 ray_dir = optixGetWorldRayDirection();
	const float3 hit_point = ray_orig + t * ray_dir;

	// Resolved to a real, non-Mix material before any mat.type branch below -
	// see MaterialType::Mix's own comment (optix_types.h). Sphere geometry
	// handles every MaterialType (it's the trap lists' own "everything else
	// falls through here" baseline), so unlike triangle/quad/bilinear-patch/
	// disk/cylinder this resolution never needs a sphere-only-handling
	// check of its own - whatever the sub-material resolves to is already
	// exactly as supported here as if it had been assigned directly.
	int matIdx = sphere.materialIdx;
	const MaterialData mat = resolve_mix_material(params.materials[matIdx], matIdx, hit_point, matIdx);
	// Light linking (light_link.h): whether this hit's light reaches the surface the ray left, read before this hit reports its own group (payload p28),
	// which the light sampling below reads as the receiver's.
	const bool linkLit = link_lit_from_previous(mat);
	link_report_group(mat);

	// Compute the normal in the sphere's own space, where it is exactly radial
	// and unit length however the placement scales it, then carry it to world
	// space through the inverse-transpose. Doing it the other way round - a
	// world-space (hit - centre) - is what would turn a scaled instance's
	// ellipsoid back into a sphere for shading purposes.
	//
	// The hit point is brought the other way, by transform rather than by
	// rebuilding the ray: optixGetObjectRay*() is legal in an intersection
	// program but NOT in a closest-hit one (OptiX rejects the module outright:
	// "Illegal call to optixGetObjectRayOrigin ... with semantic type
	// CLOSESTHIT"), whereas the transform helpers are available here.
	//
	// Both transforms are skipped entirely, not merely applied as no-ops, for
	// non-instanced geometry (instBase < 0) - so that path costs nothing and
	// stays bit-for-bit what it computed before instancing existed.
	// See GpuMediumShapeKind's comment in optix_types.h. Box bounds are
	// static (no motion-blur combination exists for this shape), so they're
	// re-read directly from `sphere` rather than via attributes the way
	// sphere_center above has to be (attributes carry the time-interpolated
	// value the raw struct doesn't have precomputed).
	const bool is_box = (sphere.shapeKind == GpuMediumShapeKind::Box);
	// See __intersection__sphere's own ClippedSphere comment: this shape
	// carries its own further object<->object affine (sphere.o2w/w2o) on top
	// of whatever OptiX instance transform placed it (identity for a
	// top-level sphere) - so its hit point/normal/dpdu below are derived by
	// composing BOTH: optixTransformPointFromWorldToObjectSpace() first
	// (undoing the instance placement, gated on instBase >= 0 exactly like
	// the plain-Sphere/Box branch below, since optixGetObjectRay*() is
	// illegal in a closest-hit program), THEN sphere.w2o/o2w for this
	// shape's own further clip-local space. Applying sphere.w2o to the raw
	// WORLD hit point directly (skipping the instance-transform undo
	// entirely) was a real bug for an INSTANCED clipped sphere - harmless
	// for the top-level case only because its own instance transform is
	// always identity.
	const bool is_clipped = (sphere.shapeKind == GpuMediumShapeKind::ClippedSphere);

	float3 obj_hit = hit_point;
	float3 obj_normal;
	if (is_clipped) {
		const float3 hit_in_instance_space = (instBase >= 0)
			? optixTransformPointFromWorldToObjectSpace(hit_point) : hit_point;
		obj_hit = dc_apply_point(sphere.w2o, hit_in_instance_space);
		const float rl = sphere.radiusLocal;
		obj_normal = (rl > 0.0f) ? (obj_hit / rl) : make_float3(0.0f, 0.0f, 1.0f);
	} else {
		if (instBase >= 0) obj_hit = optixTransformPointFromWorldToObjectSpace(hit_point);
		obj_normal = is_box
			? box_face_normal(obj_hit, sphere.boxMin, sphere.boxMax)
			: (obj_hit - sphere_center) / sphere_radius;
	}
	float3 outward_normal = obj_normal;
	if (is_clipped) {
		const float3 normal_in_instance_space = dc_apply_normal_from_w2o(sphere.w2o, obj_normal);
		outward_normal = (instBase >= 0)
			? normalize(optixTransformNormalFromObjectToWorldSpace(normal_in_instance_space))
			: normalize(normal_in_instance_space);
	} else if (instBase >= 0) {
		outward_normal = normalize(optixTransformNormalFromObjectToWorldSpace(obj_normal));
	}
	const bool front_face = dot(ray_dir, outward_normal) < 0.0f;
	const float3 normal = front_face ? outward_normal : -outward_normal;

	// Sphere UV (only meaningful for a Lambertian material with
	// textureIdx >= 0 - see shade_material()'s uv_u/uv_v params). Matches
	// CPU's get_sphere_uv() (sphere.h:131-144) exactly, using the raw
	// outward_normal (not the front-face-corrected one) since UV mapping
	// is a property of the surface point, independent of which side the
	// ray hit from. Uses the OBJECT-space normal, so a texture stays pinned
	// to the geometry as a placement rotates it - which is also what the CPU
	// does, since transform_instance hands the child an object-space ray.
	//
	// ClippedSphere uses a DIFFERENT (theta,phi) convention from the plain
	// sphere below: pbrt-v4's own Z-pole parametrization (theta from the
	// object-space Z coordinate, phi=atan2(y,x)), matching
	// SphereShape<T>::intersect() exactly (src/shared/shapes.h) - not this
	// codebase's RTIOW-style Y-pole convention the plain (unclipped)
	// GPU sphere below uses. This has to match because zMin/zMax/phiMax are
	// themselves defined in the pbrt Z-pole convention; using the Y-pole one
	// here would map the clip bounds to the wrong texture region.
	float sphere_theta, sphere_phi, sphere_uv_u, sphere_uv_v;
	if (is_clipped) {
		const float pi = 3.14159265358979323846f;
		const float rl = sphere.radiusLocal;
		const float cosTheta = fminf(1.0f, fmaxf(-1.0f, (rl > 0.0f) ? (obj_hit.z / rl) : 0.0f));
		sphere_theta = acosf(cosTheta);
		sphere_phi = atan2f(obj_hit.y, obj_hit.x);
		if (sphere_phi < 0.0f) sphere_phi += 2.0f * pi;
		// thetaZMin/thetaZMax are host-precomputed (SphereData's own comment)
		// from zMin/zMax/radiusLocal, which never change for this primitive -
		// reading them here avoids 2 redundant acosf() calls on every hit.
		sphere_uv_u = (sphere.phiMax > 1e-8f) ? (sphere_phi / sphere.phiMax) : 0.0f;
		sphere_uv_v = (sphere.thetaZMax > sphere.thetaZMin)
			? (sphere_theta - sphere.thetaZMin) / (sphere.thetaZMax - sphere.thetaZMin) : 0.0f;
	} else {
		// (a sphere loaded from a pbrt file has pbrt's poles, on the object's z axis: its direction is carried into this frame first)
		const float3 uv_n = sphere.hasUvRot ? sphere_to_uv_frame(sphere, obj_normal) : obj_normal;
		sphere_theta = acosf(-uv_n.y);
		sphere_phi = atan2f(-uv_n.z, uv_n.x) + 3.14159265358979323846f;
		sphere_uv_u = sphere_phi / (2.0f * 3.14159265358979323846f);
		sphere_uv_v = sphere_theta / 3.14159265358979323846f;
	}

	// Real analytic dpdu (tangent), matching CPU's sphere.h exactly
	// (theta=pi*v, phi=2*pi*u; differentiate the sphere's own p(theta,phi)
	// parametrization w.r.t. phi/theta, chain-rule through u/v) - computed
	// in OBJECT space from obj_normal/sphere_theta/sphere_phi (same
	// convention the UV computation above already uses), then carried to
	// world space as a genuine tangent VECTOR transform (NOT the inverse-
	// transpose normal transform used for outward_normal above - see
	// optix_intersection_triangle.h's own dpdu for the same distinction).
	// Degenerate at the poles (sin_theta -> 0 - a real parametric
	// singularity, not a bug, same as lines of longitude converging at
	// Earth's poles): falls back to cross(world_up, obj_normal) rescaled to
	// the same vanishing magnitude, exactly as CPU does, rather than
	// substituting an unrelated magnitude that would size-jump discontinuously
	// right at the fallback threshold.
	// Gated on material_needs_dpdu() - only NormalMappedLambertian and the 4
	// anisotropic material kinds ever read this; every other kind (the
	// overwhelming common case - Lambertian, Metal, Dielectric, DiffuseLight,
	// etc.) skips the trig/pole-fallback/transform work entirely rather than
	// computing a value it never uses.
	float3 sphere_dpdu = make_float3(0.0f, 0.0f, 0.0f);
	if (material_needs_dpdu(mat.type)) {
		if (is_clipped) {
			// pbrt-v4 Sphere dpdu = phiMax * (-pHit.y, pHit.x, 0) (Z-pole
			// convention - see the UV comment above): differentiating
			// p(theta,phi) w.r.t. phi and chaining through u=phi/phiMax
			// reduces to just the object-space hit point's own x/y, no
			// separate trig needed.
			sphere_dpdu = make_float3(-obj_hit.y, obj_hit.x, 0.0f) * sphere.phiMax;
			if (dot(sphere_dpdu, sphere_dpdu) < 1e-14f) {
				// Pole fallback (obj_hit.x==obj_hit.y==0, i.e. hz==+-radiusLocal) -
				// same "vanishing-magnitude tangent, not a distinct direction"
				// reasoning as the plain-sphere branch below, adapted to this
				// shape's Z-axis pole instead of Y.
				const float3 zAxis = make_float3(0.0f, 0.0f, 1.0f);
				const float3 tt = cross(zAxis, obj_normal);
				const float tlen = length(tt);
				const float3 dir = (tlen > 1e-6f) ? (tt / tlen) : make_float3(1.0f, 0.0f, 0.0f);
				sphere_dpdu = dir * (sphere.phiMax * sphere.radiusLocal * sinf(sphere_theta));
			}
			sphere_dpdu = dc_apply_vector(sphere.o2w, sphere_dpdu);
			if (instBase >= 0) sphere_dpdu = optixTransformVectorFromObjectToWorldSpace(sphere_dpdu);
			sphere_dpdu = normalize(sphere_dpdu);
		} else {
			const float sin_theta = sinf(sphere_theta);
			const float sin_phi = sinf(sphere_phi), cos_phi = cosf(sphere_phi);
			sphere_dpdu = make_float3(sin_theta * sin_phi, 0.0f, sin_theta * cos_phi)
				* (sphere_radius * 2.0f * 3.14159265358979323846f);
			if (dot(sphere_dpdu, sphere_dpdu) < 1e-14f) {
				const float3 world_up = make_float3(0.0f, 1.0f, 0.0f);
				const float3 t = cross(world_up, sphere.hasUvRot ? sphere_to_uv_frame(sphere, obj_normal) : obj_normal);
				const float tlen = length(t);
				const float3 dir = (tlen > 1e-6f) ? (t / tlen) : make_float3(1.0f, 0.0f, 0.0f);
				sphere_dpdu = dir * (sphere_radius * 2.0f * 3.14159265358979323846f * sin_theta);
			}
			if (sphere.hasUvRot) sphere_dpdu = sphere_from_uv_frame(sphere, sphere_dpdu);   // (computed in the texture frame above)
			if (instBase >= 0) sphere_dpdu = normalize(optixTransformVectorFromObjectToWorldSpace(sphere_dpdu));
		}
	}

	// Unpack payload from registers
	float3 attenuation_in = make_float3(
		__uint_as_float(optixGetPayload_0()),
		__uint_as_float(optixGetPayload_1()),
		__uint_as_float(optixGetPayload_2())
	);
	unsigned int seed = optixGetPayload_9();

	// Get emission from material - see material_emission()'s own comment for
	// why this goes through an accessor rather than reading mat.emission raw
	// (that field is a reused union slot for several material types). Real
	// UV passed through so a pbrt AreaLightSource "filename" sphere light
	// samples its image correctly instead of always reading texel (0,0).
	float3 emission = material_emission(mat, front_face, sphere_uv_u, sphere_uv_v, hit_point);

	// Material scattering
	float3 attenuation;
	float3 scattered_dir;
	bool scattered = false;
	bool is_specular = false;  // pbrt-v4 specularBounce: MIS is skipped for specular events
	// True only for MaterialType::Interface - see its own comment
	// (optix_types.h) and shade_material()'s out_is_medium_boundary.
	bool is_medium_boundary = false;
	float brdf_pdf_override = -1.0f;  // if >= 0, overrides cosine_pdf in payload packing
	bool is_medium = false;    // MaterialType::Medium: t_hit below must use medium_t_hit,
	float medium_t_hit = 0.0f; // not optixGetRayTmax() (which is just the sphere's entry surface)
	// MaterialType::Subsurface never applies to spheres in this loader (see
	// shade_material()'s own comment) - always false/unused here, but every
	// call site must still supply these two out-params (shade_material()'s
	// signature is shared across all geometry types).
	bool bssrdf_exit = false;
	float3 bssrdf_exit_pos = make_float3(0.0f, 0.0f, 0.0f);
	// pbrt-v4 etaScale term for this event - see PathTracingPayload::eta's
	// own comment. 1.0f (a no-op) by default; the shade_material() call
	// sites below fill in their own out_eta, and DielectricMedium's two
	// dielectric_scatter() calls (entry/exit, not routed through
	// shade_material()) set this directly.
	float out_eta = 1.0f;
	// See PathTracingPayload's own p24/rgbChannel comment (optix_renderer_init.cpp)
	// and shade_material()'s inout_rgb_channel parameter comment - read
	// unconditionally so a channel already chosen by an earlier bounce
	// still survives unchanged through this one. Computed once here and
	// reused by both shade_material() call sites below, matching out_eta's
	// own "read/declared once" convention just above.
	unsigned int rgbChannel = optixGetPayload_24();
	// Integrator "bool regularize" - see current_do_regularize()'s own
	// comment (optix_device_helpers.h). Computed once here and reused by
	// both shade_material() call sites below, rather than re-reading the
	// same launch-constant/payload pair twice in one closest-hit program.
	const bool do_regularize = current_do_regularize();

	if (mat.type == MaterialType::Medium) {
			// Homogeneous participating medium - see MaterialType::Medium's
			// comment in optix_types.h. Recompute both sphere-intersection
			// roots (t here is only the near/entry root optixGetRayTmax()
			// already reported) to find the exit distance, then sample a
			// free-path distance via Beer-Lambert inversion; if it lands
			// before the exit, scatter via the HG phase function, else pass
			// straight through and continue from the exit point.
			//
			// The chord is taken in the sphere's own space (interface_shape_chord), so an instanced fog ball works.
			float3 unit_dir = normalize(ray_dir);
			float t_near, t_far;
			interface_shape_chord(sphere, is_box, instBase, ray_orig, unit_dir, sphere_center, sphere_radius, t_near, t_far);
			float dist_inside = fmaxf(0.0f, t_far - t_near);

			float sigma_t = mat.ior;
			float free_path = (sigma_t > 1e-8f) ? (-logf(fmaxf(1e-8f, 1.0f - random_float(seed))) / sigma_t) : 1e30f;

			// Per-channel extinction (see MaterialData::chromaSigmaA): one event sampled with the channel-MIS estimator replaces the scalar free
			// path above; chroma_w is the collision's path weight, or the pass-through weight when the ray gets through.
			const bool chroma = medium_is_chromatic(mat);
			bool chroma_collided = false;
			float3 chroma_w = make_float3(1.0f, 1.0f, 1.0f);
			float3 chroma_le = make_float3(0.0f, 0.0f, 0.0f);
			if (chroma) {
				const float cu_channel = random_float(seed);
				const float cu_dist = random_float(seed);
				const HomogeneousEvent<float> cev = chroma_medium_event(mat, dist_inside, cu_channel, cu_dist);
				chroma_collided = cev.collided;
				free_path = cev.collided ? cev.t : 1e30f;
				chroma_w = make_float3(cev.w[0], cev.w[1], cev.w[2]);
				chroma_le = mat.chromaLe * make_float3(cev.e[0], cev.e[1], cev.e[2]);
			}

			if (chroma ? chroma_collided : (free_path < dist_inside)) {
				medium_t_hit = t_near + free_path;
				// sample_henyey_greenstein's `wo` is the outgoing direction
				// (toward where the ray came from), matching CPU's
				// hg_phase_material::scatter() `wo = -r_in.direction()`
				// convention - passing the un-negated forward travel
				// direction here inverted the g>0/g<0 forward/back-scatter
				// bias for any anisotropic medium.
				float3 wo = -unit_dir;
				scattered_dir = sample_henyey_greenstein(wo, mat.fuzz, seed);
				attenuation = chroma ? chroma_w : mat.albedo;
				// Real NEE+MIS at the phase-function scatter event, plus
				// MakeNamedMedium's own "rgb Le" self-emission (folded into
				// medium_phase_nee_mis()'s own return value now - see that
				// function's own comment, optix_device_helpers.h, and
				// MaterialData::medium_emission's own comment, optix_types.h,
				// for the sigma_a/sigma_t weighting already baked in at build
				// time).
				float3 medium_point = ray_orig + medium_t_hit * unit_dir;
				emission = emission + medium_phase_nee_mis(
					medium_point, wo, mat.fuzz, attenuation, scattered_dir, seed, brdf_pdf_override, chroma ? chroma_le : mat.medium_emission, optixGetRayTime());
				is_specular = false;
			} else {
				medium_t_hit = t_far;
				scattered_dir = unit_dir;  // straight through, no interaction
				attenuation = chroma ? chroma_w : make_float3(1.0f, 1.0f, 1.0f);
				is_specular = true;  // no interaction - a free/non-scattering pass-through
				// A free crossing, like MaterialType::Interface: flag it so the last real vertex's MIS state (prev BSDF pdf,
				// specular flag) survives it instead of being reset as if a specular bounce had happened.
				is_medium_boundary = true;
			}
			scattered    = true;
			is_medium    = true;
	} else if (mat.type == MaterialType::CloudMedium) {
			// Heterogeneous, procedural Perlin-noise cloud - see
			// MaterialType::CloudMedium's comment in optix_types.h. Delta
			// tracking (null-collision free-path sampling, pbrt-v4
			// SampleT_maj) through the medium's own world-space AABB - NOT
			// this trigger sphere's bounds, which only exists to get the ray
			// into this branch at all - using CloudMedium<float>'s own
			// sample_ray()/world_to_medium_pt()/compute_density() directly
			// device-side (CPU_GPU-tagged, compiles as-is - see
			// optix_types.h's cloud_medium.h include comment). Matches
			// src/TheRestOfYourLife/cloud_medium_hittable.h's CPU
			// delta-tracking loop exactly. Real NEE+MIS at a genuine scatter
			// event, like Medium above - see medium_phase_nee_mis()'s own
			// comment (optix_device_helpers.h).
			const CloudMedium<float>& cloud = params.cloudMediums[(int)mat.cloud_medium_extra.cloudMediumIdx];
			float3 unit_dir3 = normalize(ray_dir);
			float ray_o3[3] = { ray_orig.x, ray_orig.y, ray_orig.z };
			float ray_d3[3] = { unit_dir3.x, unit_dir3.y, unit_dir3.z };

			auto maj_it = cloud.sample_ray(ray_o3, ray_d3, 1e30f);
			float segMin, segMax, sigma_maj;
			bool has_seg = maj_it.next(segMin, segMax, sigma_maj);

			bool did_scatter = false;
			float3 medium_point = make_float3(0.0f, 0.0f, 0.0f);
			const float3 wo = -unit_dir3;
			if (has_seg && sigma_maj > 0.0f) {
				if (segMin < 0.0f) segMin = 0.0f;
				float tt = segMin;
				// Bounded iteration count: device code must not risk an
				// unbounded loop from a pathological (e.g. near-zero
				// majorant) configuration.
				for (int iter = 0; iter < 128 && !did_scatter; ++iter) {
					float dt = -logf(fmaxf(1e-8f, 1.0f - random_float(seed))) / sigma_maj;
					tt += dt;
					if (tt >= segMax) break;
					float3 p = ray_orig + tt * unit_dir3;
					float mx, my, mz;
					cloud.world_to_medium_pt(p.x, p.y, p.z, mx, my, mz);
					float d = gpu_cloud_density(cloud, mx, my, mz);
					float sigma_s_local = d * cloud.sigma_s;
					if (random_float(seed) < sigma_s_local / sigma_maj) {
						did_scatter = true;
						medium_t_hit  = tt;
						medium_point  = p;  // capture the real scatter point directly,
						                    // not a recomputation from medium_t_hit after
						                    // the loop - see `wo`'s own comment above.
						// `wo` must be the negated (outgoing) direction to match
						// CPU's hg_phase_material convention.
						scattered_dir = sample_henyey_greenstein(wo, mat.fuzz, seed);
						attenuation   = mat.albedo;
					}
				}
				if (!did_scatter) medium_t_hit = segMax;
			} else {
				// Missed the medium's own (tighter) AABB entirely - the
				// trigger sphere is a loose bound, so this is a real,
				// expected case, not an error. Pass straight through.
				medium_t_hit = t;
			}
			if (did_scatter) {
				// Real NEE+MIS at the phase-function scatter event - see
				// medium_phase_nee_mis()'s own comment (optix_device_helpers.h).
				// mat.medium_emission is always zero here (CloudMedium never
				// sets it - "rgb Le" support is homogeneous-Medium only), so
				// this is a pure no-op self-emission term, not a new feature.
				emission = emission + medium_phase_nee_mis(
					medium_point, wo, mat.fuzz, attenuation, scattered_dir, seed, brdf_pdf_override, mat.medium_emission, optixGetRayTime());
				is_specular = false;
			} else {
				scattered_dir = unit_dir3;  // straight through, no interaction
				attenuation   = make_float3(1.0f, 1.0f, 1.0f);
				is_specular   = true;  // no interaction - a free/non-scattering pass-through
				// A free crossing, like MaterialType::Interface: flag it so the last real vertex's MIS state (prev BSDF pdf,
				// specular flag) survives it instead of being reset as if a specular bounce had happened.
				is_medium_boundary = true;
			}
			scattered    = true;
			is_medium    = true;
	} else if (mat.type == MaterialType::RgbGridMedium) {
			// Heterogeneous per-voxel R/G/B scattering grid - see
			// MaterialType::RgbGridMedium's comment in optix_types.h. Uses a
			// single GLOBAL majorant (grid.sigma_maj) rather than CPU's real
			// per-voxel DDA majorant grid (src/TheRestOfYourLife/
			// rgb_grid_medium_hittable.h) - the same simplification
			// CloudMedium's GPU delta tracking already makes over its own
			// CPU counterpart, for the same reason (keep GPU delta tracking
			// algorithmically simple).
			const GpuRgbGridMedium& grid = params.rgbGridMediums[(int)mat.rgb_grid_medium_extra.rgbGridMediumIdx];
			float3 unit_dir3 = normalize(ray_dir);

			// Transform ray into medium space (grid.mat/translate) - same
			// affine-transform-preserves-t reasoning as CloudMedium's own
			// branch above (grid.mat applied to both origin and direction).
			float mox = grid.mat[0]*ray_orig.x + grid.mat[1]*ray_orig.y + grid.mat[2]*ray_orig.z + grid.translate[0];
			float moy = grid.mat[3]*ray_orig.x + grid.mat[4]*ray_orig.y + grid.mat[5]*ray_orig.z + grid.translate[1];
			float moz = grid.mat[6]*ray_orig.x + grid.mat[7]*ray_orig.y + grid.mat[8]*ray_orig.z + grid.translate[2];
			float mdx = grid.mat[0]*unit_dir3.x + grid.mat[1]*unit_dir3.y + grid.mat[2]*unit_dir3.z;
			float mdy = grid.mat[3]*unit_dir3.x + grid.mat[4]*unit_dir3.y + grid.mat[5]*unit_dir3.z;
			float mdz = grid.mat[6]*unit_dir3.x + grid.mat[7]*unit_dir3.y + grid.mat[8]*unit_dir3.z;

			// Ray/AABB slab test in medium space (unit cube [0,1]^3).
			float segMin = 0.0f, segMax = 1e30f;
			bool has_seg = true;
			{
				float invd, s0, s1;
				invd = (mdx != 0.0f) ? 1.0f/mdx : 1e30f;
				s0 = (0.0f - mox)*invd; s1 = (1.0f - mox)*invd;
				if (s0 > s1) { float tmp = s0; s0 = s1; s1 = tmp; }
				segMin = fmaxf(segMin, s0); segMax = fminf(segMax, s1);
				if (segMin > segMax) has_seg = false;

				invd = (mdy != 0.0f) ? 1.0f/mdy : 1e30f;
				s0 = (0.0f - moy)*invd; s1 = (1.0f - moy)*invd;
				if (s0 > s1) { float tmp = s0; s0 = s1; s1 = tmp; }
				segMin = fmaxf(segMin, s0); segMax = fminf(segMax, s1);
				if (segMin > segMax) has_seg = false;

				invd = (mdz != 0.0f) ? 1.0f/mdz : 1e30f;
				s0 = (0.0f - moz)*invd; s1 = (1.0f - moz)*invd;
				if (s0 > s1) { float tmp = s0; s0 = s1; s1 = tmp; }
				segMin = fmaxf(segMin, s0); segMax = fminf(segMax, s1);
				if (segMin > segMax) has_seg = false;
			}

			bool did_scatter = false;
			float3 selfEmission = make_float3(0.0f, 0.0f, 0.0f);
			// Running product of the null-collision weights of this flight: the path weight of a ray that gets through (no real collision).
			float3 grid_w = make_float3(1.0f, 1.0f, 1.0f);
			if (has_seg && grid.sigma_maj > 0.0f) {
				if (segMin < 0.0f) segMin = 0.0f;
				float tt = segMin;
				const int voxelCount = grid.nx * grid.ny * grid.nz;
				const float* sRData = params.rgbGridData + grid.dataOffset;
				const float* sGData = sRData + voxelCount;
				const float* sBData = sGData + voxelCount;
				const bool hasSa = grid.saDataOffset >= 0;
				const float* aRData = hasSa ? params.rgbGridData + grid.saDataOffset : nullptr;
				const float* aGData = hasSa ? aRData + voxelCount : nullptr;
				const float* aBData = hasSa ? aGData + voxelCount : nullptr;
				for (int iter = 0; iter < 128 && !did_scatter; ++iter) {
					float dt = -logf(fmaxf(1e-8f, 1.0f - random_float(seed))) / grid.sigma_maj;
					tt += dt;
					if (tt >= segMax) break;
					float px = mox + tt*mdx, py = moy + tt*mdy, pz = moz + tt*mdz;
					// Per-channel sigma_s / sigma_a at the tentative point; an absent sigma_a grid is pbrt's default of 1.
					const float ss[3] = { gpu_rgb_grid_trilinear(sRData, grid.nx, grid.ny, grid.nz, px, py, pz) * grid.sigma_scale,
					                      gpu_rgb_grid_trilinear(sGData, grid.nx, grid.ny, grid.nz, px, py, pz) * grid.sigma_scale,
					                      gpu_rgb_grid_trilinear(sBData, grid.nx, grid.ny, grid.nz, px, py, pz) * grid.sigma_scale };
					const float sa[3] = { (hasSa ? gpu_rgb_grid_trilinear(aRData, grid.nx, grid.ny, grid.nz, px, py, pz) : 1.0f) * grid.sigma_scale,
					                      (hasSa ? gpu_rgb_grid_trilinear(aGData, grid.nx, grid.ny, grid.nz, px, py, pz) : 1.0f) * grid.sigma_scale,
					                      (hasSa ? gpu_rgb_grid_trilinear(aBData, grid.nx, grid.ny, grid.nz, px, py, pz) : 1.0f) * grid.sigma_scale };
					float w3[3] = { grid_w.x, grid_w.y, grid_w.z }, cw[3], ce[3];
					const float u_real = random_float(seed);
					if (heterogeneous_tracking_step<3, float>(sa, ss, grid.sigma_maj, u_real, w3, cw, ce)) {
						did_scatter = true;
						medium_t_hit  = tt;
						// See the Medium branch's comment above: `wo` must be the negated (outgoing) direction to match CPU's
						// hg_phase_material convention.
						scattered_dir = sample_henyey_greenstein(-unit_dir3, grid.phase_g, seed);
						attenuation = make_float3(cw[0], cw[1], cw[2]);
						// Real per-voxel "rgb Le" at this accepted collision, weighted by this event's absorption share exactly as the CPU
						// does (rgb_grid_medium_hittable::sample_event): Le_c * w_c * sigma_a_c / mean(sigma_t). (It used to be the full Le at
						// weight 1 because this grid had no sigma_a at all.)
						if (grid.leDataOffset >= 0) {
							const float* leRData = params.rgbGridData + grid.leDataOffset;
							const float* leGData = leRData + voxelCount;
							const float* leBData = leGData + voxelCount;
							float ler = gpu_rgb_grid_trilinear(leRData, grid.nx, grid.ny, grid.nz, px, py, pz);
							float leg = gpu_rgb_grid_trilinear(leGData, grid.nx, grid.ny, grid.nz, px, py, pz);
							float leb = gpu_rgb_grid_trilinear(leBData, grid.nx, grid.ny, grid.nz, px, py, pz);
							selfEmission = make_float3(ler * ce[0], leg * ce[1], leb * ce[2]) * grid.Le_scale;
						}
					} else {
						grid_w = make_float3(w3[0], w3[1], w3[2]);
					}
				}
				if (!did_scatter) medium_t_hit = segMax;
			} else {
				// Missed the medium's own (tighter) AABB entirely - the
				// trigger sphere is a loose bound, same as CloudMedium above.
				medium_t_hit = t;
			}
			if (did_scatter) {
				// Real NEE+MIS at the phase-function scatter event - see
				// medium_phase_nee_mis()'s own comment (optix_device_helpers.h).
				// selfEmission (computed above, real per-voxel "rgb Le") is
				// folded in exactly like mat.medium_emission is for every
				// other medium type's own build-time-baked constant.
				float3 wo = -unit_dir3;
				float3 medium_point = ray_orig + medium_t_hit * unit_dir3;
				emission = emission + medium_phase_nee_mis(
					medium_point, wo, grid.phase_g, attenuation, scattered_dir, seed, brdf_pdf_override, selfEmission, optixGetRayTime());
				is_specular = false;
			} else {
				scattered_dir = unit_dir3;
				attenuation   = grid_w;   // pass weight of the null collisions (1 for a grey grid with no null events)
				is_specular   = true;  // no interaction - a free/non-scattering pass-through
				// A free crossing, like MaterialType::Interface: flag it so the last real vertex's MIS state (prev BSDF pdf,
				// specular flag) survives it instead of being reset as if a specular bounce had happened.
				is_medium_boundary = true;
			}
			scattered    = true;
			is_medium    = true;
	} else if (mat.type == MaterialType::GridMedium) {
			// Heterogeneous single-channel scalar density grid - single-
			// channel twin of the RgbGridMedium branch just above (see that
			// one's own comment; same single-GLOBAL-majorant simplification).
			const GpuGridMedium& grid = params.gridMediums[(int)mat.grid_medium_extra.gridMediumIdx];
			float3 unit_dir3 = normalize(ray_dir);

			float mox = grid.mat[0]*ray_orig.x + grid.mat[1]*ray_orig.y + grid.mat[2]*ray_orig.z + grid.translate[0];
			float moy = grid.mat[3]*ray_orig.x + grid.mat[4]*ray_orig.y + grid.mat[5]*ray_orig.z + grid.translate[1];
			float moz = grid.mat[6]*ray_orig.x + grid.mat[7]*ray_orig.y + grid.mat[8]*ray_orig.z + grid.translate[2];
			float mdx = grid.mat[0]*unit_dir3.x + grid.mat[1]*unit_dir3.y + grid.mat[2]*unit_dir3.z;
			float mdy = grid.mat[3]*unit_dir3.x + grid.mat[4]*unit_dir3.y + grid.mat[5]*unit_dir3.z;
			float mdz = grid.mat[6]*unit_dir3.x + grid.mat[7]*unit_dir3.y + grid.mat[8]*unit_dir3.z;

			float segMin = 0.0f, segMax = 1e30f;
			bool has_seg = true;
			{
				float invd, s0, s1;
				invd = (mdx != 0.0f) ? 1.0f/mdx : 1e30f;
				s0 = (0.0f - mox)*invd; s1 = (1.0f - mox)*invd;
				if (s0 > s1) { float tmp = s0; s0 = s1; s1 = tmp; }
				segMin = fmaxf(segMin, s0); segMax = fminf(segMax, s1);
				if (segMin > segMax) has_seg = false;

				invd = (mdy != 0.0f) ? 1.0f/mdy : 1e30f;
				s0 = (0.0f - moy)*invd; s1 = (1.0f - moy)*invd;
				if (s0 > s1) { float tmp = s0; s0 = s1; s1 = tmp; }
				segMin = fmaxf(segMin, s0); segMax = fminf(segMax, s1);
				if (segMin > segMax) has_seg = false;

				invd = (mdz != 0.0f) ? 1.0f/mdz : 1e30f;
				s0 = (0.0f - moz)*invd; s1 = (1.0f - moz)*invd;
				if (s0 > s1) { float tmp = s0; s0 = s1; s1 = tmp; }
				segMin = fmaxf(segMin, s0); segMax = fminf(segMax, s1);
				if (segMin > segMax) has_seg = false;
			}

			bool did_scatter = false;
			if (has_seg && grid.sigma_maj > 0.0f) {
				if (segMin < 0.0f) segMin = 0.0f;
				float tt = segMin;
				const float* dData = params.gridData + grid.dataOffset;
				for (int iter = 0; iter < 128 && !did_scatter; ++iter) {
					float dt = -logf(fmaxf(1e-8f, 1.0f - random_float(seed))) / grid.sigma_maj;
					tt += dt;
					if (tt >= segMax) break;
					float px = mox + tt*mdx, py = moy + tt*mdy, pz = moz + tt*mdz;
					float d = gpu_rgb_grid_trilinear(dData, grid.nx, grid.ny, grid.nz, px, py, pz);
					float sigma_t_local = d * grid.sigma_scale;
					if (random_float(seed) < sigma_t_local / grid.sigma_maj) {
						did_scatter = true;
						medium_t_hit  = tt;
						scattered_dir = sample_henyey_greenstein(-unit_dir3, grid.phase_g, seed);
						attenuation   = mat.albedo;  // no per-voxel colour (single-channel density) - see grid_medium_hittable.h's own comment
					}
				}
				if (!did_scatter) medium_t_hit = segMax;
			} else {
				medium_t_hit = t;
			}
			if (did_scatter) {
				// Real NEE+MIS at the phase-function scatter event - see
				// medium_phase_nee_mis()'s own comment (optix_device_helpers.h).
				// mat.medium_emission is always zero here (GridMedium never
				// sets it - "rgb Le" support is homogeneous-Medium only), so
				// this is a pure no-op self-emission term, not a new feature.
				float3 wo = -unit_dir3;
				float3 medium_point = ray_orig + medium_t_hit * unit_dir3;
				emission = emission + medium_phase_nee_mis(
					medium_point, wo, grid.phase_g, attenuation, scattered_dir, seed, brdf_pdf_override, mat.medium_emission, optixGetRayTime());
				is_specular = false;
			} else {
				scattered_dir = unit_dir3;
				attenuation   = make_float3(1.0f, 1.0f, 1.0f);
				is_specular   = true;  // no interaction - a free/non-scattering pass-through
				// A free crossing, like MaterialType::Interface: flag it so the last real vertex's MIS state (prev BSDF pdf,
				// specular flag) survives it instead of being reset as if a specular bounce had happened.
				is_medium_boundary = true;
			}
			scattered    = true;
			is_medium    = true;
	} else if (mat.type == MaterialType::Hair) {
			// Marschner/Chiang fiber scattering - see sample_hair_material's
			// comment in optix_device_helpers.h. Matches hair_material.h's
			// skip_pdf=true: no NEE/MIS, res.r/g/b already divides by the
			// sample pdf (same convention as this codebase's other
			// is_specular=true cases).
			scattered   = sample_hair_material(ray_dir, normal, mat, seed, scattered_dir, attenuation);
			is_specular = true;
	} else if (mat.type == MaterialType::DielectricMedium) {
			// Combined dielectric surface + internal medium - see this
			// type's comment in optix_types.h. On entry (front_face) the
			// direct dielectric surface always wins the bounce (matches the
			// CPU's two-hittable trick: the medium's sampled hit distance
			// can never be closer than the entry surface), so just refract/
			// reflect normally (or, when is_thin, reflect-or-straight-
			// through - see thin_dielectric_scatter()'s own comment). On the
			// following bounce, now travelling inside toward this sphere's
			// exit surface (front_face false), recompute both roots exactly
			// like the Medium branch above to get the remaining distance to
			// the exit, sample a free path, and either scatter via the HG
			// phase function or fall through to a normal exit refraction/
			// reflection (or thin coin-flip) at the far surface.
			//
			// surfaceKind: which BSDF model this fused material's surface
			// uses instead of always smooth Dielectric refraction - see
			// pbrt_gpu_builder.h's mediumMaterialIndex() for how a shape's
			// own Material "dielectric" (rough) / "thindielectric" (thin)
			// sets dielectric_medium_extra.surfaceKind (0=smooth/default,
			// 1=thin, 2=rough). Thin needs no etaScale adjustment (no actual
			// refraction/depth change); rough's real glossy NEE is handled
			// entirely inside rough_dielectric_scatter_and_nee() (optix_
			// device_helpers.h), including its own etaScale/is_specular/
			// emission updates - unlike thin/smooth, it isn't a simple
			// "just compute scattered_dir" call.
			const float rdm_kind = mat.dielectric_medium_extra.surfaceKind;
			const bool is_rough = rdm_kind >= 1.5f;
			const bool is_thin  = !is_rough && rdm_kind >= 0.5f;
			// Set false only by a rough entry/exit surface call below that
			// hits the rare grazing-angle degenerate case (rough_dielectric_
			// scatter_and_nee() returns false) - mirrors shade_material()'s
			// own `scattered=false` handling for a plain RoughDielectric in
			// the identical situation. Stays true for every other path
			// (smooth/thin always "scatter", the interior phase-scatter
			// sub-case below always "scatters" too), matching the
			// unconditional `scattered = true;` this replaced.
			bool rdm_scatter_ok = true;
			if (front_face) {
				if (is_rough) {
					rdm_scatter_ok = rough_dielectric_scatter_and_nee(
						mat, mat.dielectric_medium_extra.roughness, /*allowDispersion=*/false,
						normal, ray_dir, hit_point, front_face, sphere_uv_u, sphere_uv_v, sphere_dpdu,
						do_regularize, seed, rgbChannel,
						attenuation, scattered_dir, is_specular, brdf_pdf_override, emission, out_eta);
				} else {
					attenuation = make_float3(1.0f, 1.0f, 1.0f);
					if (is_thin) {
						scattered_dir = thin_dielectric_scatter(ray_dir, normal, mat.ior, seed);
					} else {
						scattered_dir = dielectric_scatter(ray_dir, normal, front_face, mat.ior, seed);
						// pbrt-v4 etaScale (entry surface) - see MaterialType::
						// Dielectric's identical eta computation above.
						if (dot(scattered_dir, normal) < 0.0f) out_eta = front_face ? (1.0f / mat.ior) : mat.ior;
					}
					is_specular = true;
				}
			} else {
				float3 unit_dir = normalize(ray_dir);
				float t_near, t_far;
				interface_shape_chord(sphere, is_box, instBase, ray_orig, unit_dir, sphere_center, sphere_radius, t_near, t_far);
				float dist_inside = fmaxf(0.0f, t_far - t_near);

				float sigma_t = mat.eta_c.x;
				float free_path = (sigma_t > 1e-8f) ? (-logf(fmaxf(1e-8f, 1.0f - random_float(seed))) / sigma_t) : 1e30f;

				// Per-channel extinction (see MaterialData::chromaSigmaA): one event sampled with the channel-MIS estimator replaces the scalar free
				// path above; chroma_w is the collision's path weight, or the pass-through weight when the ray gets through.
				const bool chroma = medium_is_chromatic(mat);
				bool chroma_collided = false;
				float3 chroma_w = make_float3(1.0f, 1.0f, 1.0f);
				float3 chroma_le = make_float3(0.0f, 0.0f, 0.0f);
				if (chroma) {
					const float cu_channel = random_float(seed);
					const float cu_dist = random_float(seed);
					const HomogeneousEvent<float> cev = chroma_medium_event(mat, dist_inside, cu_channel, cu_dist);
					chroma_collided = cev.collided;
					free_path = cev.collided ? cev.t : 1e30f;
					chroma_w = make_float3(cev.w[0], cev.w[1], cev.w[2]);
					chroma_le = mat.chromaLe * make_float3(cev.e[0], cev.e[1], cev.e[2]);
				}

				if (chroma ? chroma_collided : (free_path < dist_inside)) {
					medium_t_hit  = t_near + free_path;
					float3 medium_point = ray_orig + medium_t_hit * unit_dir;
					float g = mat.fuzz;  // Medium/DielectricMedium: HG asymmetry
					// wo = -unit_dir: direction back toward where this ray came
					// from, matching CPU's `-r_in.direction()` convention
					// (hg_phase_pdf's own `wo`) - also makes the sampled
					// scattered_dir self-consistent with the `wo`
					// medium_phase_nee_mis() uses below for this same event's
					// NEE/MIS.
					float3 wo = -unit_dir;
					scattered_dir = sample_henyey_greenstein(wo, g, seed);
					attenuation   = chroma ? chroma_w : mat.albedo;
					is_medium     = true;

					// Real NEE+MIS at the phase-function scatter event - see
					// medium_phase_nee_mis()'s own comment (optix_device_helpers.h)
					// for the full root-cause derivation of why this matters (this
					// is the branch it was originally written for: B13/
					// SubsurfaceSlab's ~32-38% CPU-brighter gap without it). The
					// two OTHER DielectricMedium sub-cases (the entry/exit
					// dielectric-surface refractions just above/below) are
					// genuinely specular (a Dirac-delta BSDF) and correctly stay
					// is_specular=true - only this interior phase-function event
					// is smooth/continuous like a diffuse BRDF and benefits from
					// NEE the same way. mat.medium_emission carries
					// MakeNamedMedium's own "rgb Le" when the pbrt loader's
					// mediumMaterialIndex() populated it (pbrt_gpu_builder.h) -
					// zero for the native scene_builder.cpp::add_dielectric_
					// medium() path, which never had a reason to set it.
					emission = emission + medium_phase_nee_mis(
						medium_point, wo, g, attenuation, scattered_dir, seed, brdf_pdf_override, chroma ? chroma_le : mat.medium_emission, optixGetRayTime());
					is_specular = false;
				} else if (is_rough) {
					rdm_scatter_ok = rough_dielectric_scatter_and_nee(
						mat, mat.dielectric_medium_extra.roughness, /*allowDispersion=*/false,
						normal, ray_dir, hit_point, front_face, sphere_uv_u, sphere_uv_v, sphere_dpdu,
						do_regularize, seed, rgbChannel,
						attenuation, scattered_dir, is_specular, brdf_pdf_override, emission, out_eta);
				} else {
					attenuation = make_float3(1.0f, 1.0f, 1.0f);
					if (is_thin) {
						scattered_dir = thin_dielectric_scatter(ray_dir, normal, mat.ior, seed);
					} else {
						scattered_dir = dielectric_scatter(ray_dir, normal, front_face, mat.ior, seed);
						// pbrt-v4 etaScale (exit surface, front_face is false
						// here) - see MaterialType::Dielectric's identical eta
						// computation above.
						if (dot(scattered_dir, normal) < 0.0f) out_eta = front_face ? (1.0f / mat.ior) : mat.ior;
					}
					is_specular = true;
				}
							if (chroma && !chroma_collided) {
					// The ray crossed the interior without a collision: its path weight, and the direct light the exit surface just gathered
					// (which that attenuated path carries), take the per-channel pass-through weight.
					attenuation = attenuation * chroma_w;
					emission = emission * chroma_w;
				}
			}
			scattered = rdm_scatter_ok;
	} else if (mat.type == MaterialType::NormalMappedLambertian) {
			// Real analytic dpdu, computed once above (sphere_dpdu) and
			// shared with the anisotropic-material path below - previously
			// this branch had its own separate, approximate cross(world_up,
			// normal) tangent; now uses the same real one CPU's sphere.h
			// computes, which also fixes this branch's normal-map basis to
			// match CPU exactly rather than an approximation.
			const float3& dpdu = sphere_dpdu;

			// Decode the tangent-space normal from the map texture exactly
			// like CPU's normal_map_material::apply(): 2*RGB-1, normalize
			// (fallback (0,0,1) i.e. "no perturbation" if degenerate).
			const float3 packed = sample_texture(mat.textureIdx, sphere_uv_u, sphere_uv_v, hit_point);
			float ns_x = 2.0f * packed.x - 1.0f;
			float ns_y = 2.0f * packed.y - 1.0f;
			float ns_z = 2.0f * packed.z - 1.0f;
			const float ns_len = sqrtf(ns_x * ns_x + ns_y * ns_y + ns_z * ns_z);
			if (ns_len > 1e-8f) { ns_x /= ns_len; ns_y /= ns_len; ns_z /= ns_len; }
			else                { ns_x = 0.0f; ns_y = 0.0f; ns_z = 1.0f; }

			float out_nx, out_ny, out_nz;
			apply_normal_map(ns_x, ns_y, ns_z, normal.x, normal.y, normal.z,
				dpdu.x, dpdu.y, dpdu.z, out_nx, out_ny, out_nz);
			float3 perturbed_normal = make_float3(out_nx, out_ny, out_nz);
			if (dot(perturbed_normal, ray_dir) > 0.0f) perturbed_normal = -perturbed_normal;   // (two-sided about the shading normal, as pbrt's BSDF and the CPU's normal_map_material)

			// Shade as plain Lambertian (mat.albedo) using the perturbed
			// normal - reuses shade_material()'s existing Lambertian NEE/
			// MIS logic verbatim rather than duplicating it. textureIdx is
			// cleared since it means "normal map" for this material type,
			// not "albedo texture" the way Lambertian itself reads it.
			MaterialData effective = mat;
			effective.type = MaterialType::Lambertian;
			effective.textureIdx = -1;
			// Integrator "bool regularize" - see shade_material()'s own
			// do_regularize parameter comment. Inert here (Lambertian has
			// no alpha to widen) but every call site must still supply it -
			// reuses the same do_regularize hoisted above rather than
			// re-reading it, even though this branch never consumes it.
			shade_material(effective, matIdx, perturbed_normal, ray_dir, hit_point, front_face, sphere_uv_u, sphere_uv_v, dpdu, do_regularize, seed,
				attenuation, scattered_dir, scattered, is_specular, is_medium_boundary, brdf_pdf_override, emission,
				bssrdf_exit, bssrdf_exit_pos, out_eta, rgbChannel);
	} else if (mat.type == MaterialType::Principled) {
			// Disney/pbrt-v4 multi-lobe BSDF - see sample_principled_material's
			// comment in optix_device_helpers.h. Matches principled_material.h's
			// skip_pdf=true: no NEE/MIS, res.r/g/b already divides by the
			// sample pdf (same convention as this codebase's other
			// is_specular=true cases, e.g. MaterialType::Hair above).
			scattered   = sample_principled_material(ray_dir, normal, mat, seed, scattered_dir, attenuation);
			is_specular = true;
	} else {
		// Integrator "bool regularize" - see shade_material()'s own
		// do_regularize parameter comment (reuses the value hoisted above).
		shade_material(mat, matIdx, normal, ray_dir, hit_point, front_face, sphere_uv_u, sphere_uv_v, sphere_dpdu, do_regularize, seed,
			attenuation, scattered_dir, scattered, is_specular, is_medium_boundary, brdf_pdf_override, emission,
			bssrdf_exit, bssrdf_exit_pos, out_eta, rgbChannel);
	}

	// Pack updated payload back into registers

	// p3-p5: emission from this surface hit
	// p6-p8: scatter direction (if scattered)
	// p9: updated seed
	// p10: scattered flag (0=absorbed, 1=scattered, 2=hit_light)
	// p11: hit distance 't'
	// p12: brdf_pdf of scattered dir (flag==1) OR light NEE pdf of incoming dir (flag==2) for MIS

	// Always set emission (for all material types - non-lights have emission=0)
	if (!linkLit) emission = make_float3(0.0f, 0.0f, 0.0f);   // light linking (light_link.h)
	optixSetPayload_3(__float_as_uint(emission.x));
	optixSetPayload_4(__float_as_uint(emission.y));
	optixSetPayload_5(__float_as_uint(emission.z));
	optixSetPayload_9(seed);

	// p16-p21: denoiser guide-layer AOVs (recursive backend only) - see
	// pack_aov_payload()'s own comment (optix_device_helpers.h) for why
	// this is unconditional (every branch below, not just `scattered`).
	// `attenuation` is only guaranteed written when `scattered` is true
	// (it's an uninitialized local otherwise, per its declaration above) -
	// mat.albedo is the safe always-valid fallback for the DiffuseLight/
	// absorbed branches. `normal` itself is always valid here regardless
	// of branch.
	{
		float3 albedoAov = scattered ? attenuation : mat.albedo;
		pack_aov_payload(albedoAov, normal);
	}
	optixSetPayload_22(__float_as_uint(out_eta));  // pbrt-v4 etaScale - see PathTracingPayload::eta
	optixSetPayload_24(rgbChannel);  // dispersion channel - see shade_material()'s inout_rgb_channel comment

	// The light pdf of this primitive for the incoming direction: a light hit's MIS weight, and a reflecting lamp's own emission in the scattered branch.
	const auto sphere_light_pdf = [&]() -> float {
		float light_pdf_for_incoming = 0.0f;
		const float3 mis_o = mis_origin_from_payload();  // last real vertex, not the (possibly moved) ray origin
		if (params.aliasTable && params.numLights > 0) {
			// Find selection PMF for this sphere in the alias table. The
			// GLOBAL index, since lightIndices holds indices into the one flat
			// sphere array. An instanced sphere is never emissive (flatten()
			// bakes emitters per placement instead), so its index simply never
			// matches - which is the correct answer, not a missed case.
			// (the light BVH's pmf at mis_o, the shading point the BSDF sample that reached this light was taken from, so the MIS weight matches whatever NEE
			// would have used from there: gpu_light_selection_pmf)
			const float sel_pdf = gpu_light_selection_pmf(GpuLightKind::Sphere, (int)(sphBase + primIdx), mis_o);
			// Solid-angle PDF: 1 / (2*pi*(1 - cos_theta_max)). ClippedSphere
			// uses sphere.center/sphere.radius (the real struct fields,
			// populated in pbrt_gpu_builder.h purely for this full-sphere-
			// cone NEE approximation - see SphereData's own comment) rather
			// than sphere_center/sphere_radius (this function's attribute-
			// derived locals, which the ClippedSphere intersection program
			// never packs - see __intersection__sphere's own comment) - and
			// MUST match whatever sample_sphere_light() used for the reverse
			// (NEE-sampling) direction for MIS weights to be consistent.
			const float3 lightCenter = is_clipped ? sphere.center : sphere_center;
			const float lightRadius = is_clipped ? sphere.radius : sphere_radius;
			float3 to_center = lightCenter - mis_o;
			float dist_sq = dot(to_center, to_center);
			if (dist_sq > lightRadius * lightRadius) {
				// Same stable form as sample_sphere_light() (the NEE density this must equal for MIS).
				const float x_sq = lightRadius * lightRadius / dist_sq;
				const float cos_theta_max = sqrtf(1.0f - x_sq);
				float solid_angle = 2.0f * 3.14159265f * (x_sq / (1.0f + cos_theta_max));
				light_pdf_for_incoming = sel_pdf / solid_angle;
			}
		}
		return light_pdf_for_incoming;
	};

	if (scattered) {
		// A lamp that also reflects: its own light, weighted against the light sampling (p13-p15, which only a subsurface exit otherwise uses; raygen
		// adds it for flag 1). The input p12 (the arriving BSDF pdf) is read here, before this program overwrites it.
		float3 lamp_le = make_float3(0.0f, 0.0f, 0.0f);
		if (lamp_scatters(mat) && linkLit)
			lamp_le = lamp_own_emission(material_emission(mat, front_face, sphere_uv_u, sphere_uv_v, hit_point), __uint_as_float(optixGetPayload_12()), sphere_light_pdf());
		// Return surface attenuation ONLY (raygen will multiply with throughput)
		// Medium: the scatter/exit point is not the sphere's entry surface
		// (optixGetRayTmax()) - use the distance computed in the Medium
		// case above instead.
		float t_hit = is_medium ? medium_t_hit : optixGetRayTmax();  // Hit distance
		// p12: BRDF PDF for MIS on the next bounce.
		// Specular (Metal/Dielectric): 0 = no MIS (pbrt-v4 specularBounce pattern).
		// Lambertian: cosine_pdf of the sampled direction.
		// NormalizedFresnel: (1-Fr)*cos/(c*pi) stored in brdf_pdf_override.
		float brdf_pdf_out = is_specular ? 0.0f
						  : (brdf_pdf_override >= 0.0f ? brdf_pdf_override : cosine_pdf(scattered_dir, normal));

		optixSetPayload_0(__float_as_uint(attenuation.x));
		optixSetPayload_1(__float_as_uint(attenuation.y));
		optixSetPayload_2(__float_as_uint(attenuation.z));
		optixSetPayload_6(__float_as_uint(scattered_dir.x));  // Scatter direction
		optixSetPayload_7(__float_as_uint(scattered_dir.y));
		optixSetPayload_8(__float_as_uint(scattered_dir.z));
		// MaterialType::Subsurface (and therefore bssrdf_exit) never applies
		// to spheres in this loader, but flag 3 / p13-15 are packed the same
		// way triangle/quad/bilinear-patch do for consistency - see
		// optix_intersection_quad.h's p0-p15 layout comment. flag==4:
		// MaterialType::Interface - see optix_raygen.h's own flag==4 branch.
		optixSetPayload_10(pack_scatter_flag(bssrdf_exit, is_medium_boundary, is_specular));  // scattered (see pack_scatter_flag's own comment)
		optixSetPayload_11(__float_as_uint(t_hit));
		optixSetPayload_12(__float_as_uint(brdf_pdf_out));
		if (lamp_le.x > 0.0f || lamp_le.y > 0.0f || lamp_le.z > 0.0f) {
			optixSetPayload_13(__float_as_uint(lamp_le.x));
			optixSetPayload_14(__float_as_uint(lamp_le.y));
			optixSetPayload_15(__float_as_uint(lamp_le.z));
		}
		if (bssrdf_exit) {
			optixSetPayload_13(__float_as_uint(bssrdf_exit_pos.x));
			optixSetPayload_14(__float_as_uint(bssrdf_exit_pos.y));
			optixSetPayload_15(__float_as_uint(bssrdf_exit_pos.z));
		}
	} else if (mat.type == MaterialType::DiffuseLight) {
		// p12: NEE PDF for the incoming ray direction reaching this sphere light.
		// This is the solid-angle PDF used by the NEE sampler, enabling MIS in raygen.
		const float light_pdf_for_incoming = sphere_light_pdf();
		optixSetPayload_10(2);  // hit_light
		optixSetPayload_11(__float_as_uint(optixGetRayTmax()));  // camera-medium clip distance (see optix_raygen.h's own call site)
		optixSetPayload_12(__float_as_uint(light_pdf_for_incoming));
	} else {
		optixSetPayload_10(0);  // absorbed
		optixSetPayload_11(__float_as_uint(optixGetRayTmax()));  // camera-medium clip distance (see optix_raygen.h's own call site)
		optixSetPayload_12(0);
	}
}

//==============================================================================
// Quad Intersection Program
//==============================================================================

