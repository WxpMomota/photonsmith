/// @file optix_renderer_init.cpp
/// @brief OptiX Renderer Implementation -- context/pipeline setup.
/// @details Split out of optix_renderer.cpp (see optix_renderer_scene.cpp
///          and optix_renderer_render.cpp for the other two thirds, and
///          optix_renderer.h for the shared class declaration). This part
///          owns device availability probing, OptiX context/module/pipeline
///          creation, and PTX loading -- everything a scene build needs
///          already in place. Also the sole owner of
///          <optix_function_table_definition.h>, which must be included in
///          exactly one of these 3 translation units (it defines the global
///          OptiX function table that <optix_stubs.h>'s inline wrappers call
///          through) -- do not add it to the other two files.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "optix_renderer.h"
// wavefront_path_tracer.h/sppm_path_tracer.h: not otherwise needed here, but
// ~OptiXRenderer() (defined in this file) implicitly destroys the
// unique_ptr<WavefrontPathTracer>/unique_ptr<SPPMPathTracer> members, which
// requires both types complete at the point of definition.
#include "wavefront_path_tracer.h"
#include "sppm_path_tracer.h"
#include <optix_function_table_definition.h>
#include <optix_stack_size.h>
#include "optix_module_parallel.h"
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <mutex>
#include <thread>
#include "optix_program_group.h"
#include <cuda.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <array>
#include <iterator>   // std::size
#include <vector>
#include <algorithm>     // std::min, for getDiagnostics()'s bounded string copy
#include <cstdlib>       // getenv, for RAY_TRACER_OPTIX_VALIDATION

namespace {
	/// Constants for OptiX configuration
	constexpr unsigned int kDefaultLogLevel = 3;  ///< OptiX log level (0=off, 4=verbose)
	constexpr int kDefaultCudaDevice = 0;         ///< The CUDA device used when nothing chooses another (see selectedCudaDevice)
	constexpr size_t kMaxDeviceNameLength = 256;  ///< Max length for device name buffer

	/// The CUDA device the renderer uses: RAY_TRACER_GPU_DEVICE (`--gpu-device N` sets it), else the first. A number that is not a device of this machine is said once and the first is used.
	int selectedCudaDevice() {
#pragma warning(suppress: 4996)
		const char *env = std::getenv("RAY_TRACER_GPU_DEVICE");
		if (!env || !*env) return kDefaultCudaDevice;
		const int wanted = std::atoi(env);
		int count = 0;
		if (wanted < 0 || cudaGetDeviceCount(&count) != cudaSuccess || wanted >= count) {
			static bool told = false;
			if (!told) {
				told = true;
				std::cerr << "[OptiX] GPU device " << env << " (RAY_TRACER_GPU_DEVICE / --gpu-device) is not a device of this computer (" << count << " found); using device " << kDefaultCudaDevice << "\n";
			}
			return kDefaultCudaDevice;
		}
		return wanted;
	}
}

// ============================================================================
// SBT Record Structures (file scope for use across multiple functions)
// ============================================================================

// SBT record types are defined in optix_types.h

// ============================================================================
// Logging and Initialization
// ============================================================================

/// @brief Logging callback for OptiX messages
/// @param level Message severity level (1=fatal, 2=error, 3=warning, 4=print)
/// @param tag Message category tag
/// @param message The log message
/// @param cbdata The OptiXRenderer instance passed via
///               OptixDeviceContextOptions::logCallbackData at context
///               creation - null for isAvailable()'s throwaway probe
///               context, which doesn't track issues.
void contextLogCallback(
	unsigned int level,
	const char* tag,
	const char* message,
	void* cbdata
) {
	fprintf(stderr, "[OptiX][%u][%s]: %s\n", level, tag, message);
	if (level <= 3) {
		if (auto* self = static_cast<OptiXRenderer*>(cbdata)) {
			self->loggedIssues_.push_back(
				std::string("[") + tag + "] " + message);
		}
	}
}

OptiXRenderer::OptiXRenderer() {
}

OptiXRenderer::~OptiXRenderer() {
	cleanup();
}

// The test runner sets RT_FORBID_GPU in the shards of its Fast tier, which runs many tests at once on the assumption that none of them uses the GPU (scripts/run_tests_parallel.ps1
// picks the GPU tests out by name). A GPU test named without "GPU" used to run there unnoticed, and the check meant to catch it could not fail: now the shard stops here, and
// its log ends with the test that asked.
static void refuseIfGpuForbidden() {
	char *forbid = nullptr;
	size_t len = 0;
	const bool set = _dupenv_s(&forbid, &len, "RT_FORBID_GPU") == 0 && forbid && forbid[0] && forbid[0] != '0';
	std::free(forbid);
	if (!set) return;
	std::cerr << "RT_FORBID_GPU: a test of the Fast tier asked for the GPU. Put GPU (or Gpu) in its name, or add it to the GPU tests' filter in scripts/run_tests_parallel.ps1."
			  << std::endl;
	std::abort();
}

bool OptiXRenderer::isAvailable() noexcept {
	refuseIfGpuForbidden();
	// Step 1: Check CUDA device availability
	int deviceCount = 0;
	const cudaError_t cudaErr = cudaGetDeviceCount(&deviceCount);
	if (cudaErr != cudaSuccess || deviceCount == 0) {
		std::cerr << "[OptiX] No CUDA devices found. Error: "
				  << cudaGetErrorString(cudaErr) << "\n";
		return false;
	}

	// Step 2: Set CUDA device
	if (const cudaError_t setErr = cudaSetDevice(selectedCudaDevice()); setErr != cudaSuccess) {
		std::cerr << "[OptiX] cudaSetDevice(" << selectedCudaDevice() << ") failed: "
				  << cudaGetErrorString(setErr) << "\n";
		return false;
	}

	// Step 3: Initialize OptiX library (loads OptiX DLL)
	if (const OptixResult initRes = optixInit(); initRes != OPTIX_SUCCESS) {
		std::cerr << "[OptiX] optixInit failed: "
				  << optixGetErrorString(initRes) << "\n";
		return false;
	}

	// Step 4: Test OptiX context creation
	if (const CUresult cuInitErr = cuInit(0); cuInitErr != CUDA_SUCCESS) {
		std::cerr << "[OptiX] cuInit failed\n";
		return false;
	}

	CUdevice cuDevice;
	if (const CUresult devErr = cuDeviceGet(&cuDevice, selectedCudaDevice()); devErr != CUDA_SUCCESS) {
		std::cerr << "[OptiX] cuDeviceGet failed\n";
		return false;
	}

	CUcontext cuCtx = nullptr;
	if (const CUresult ctxErr = cuDevicePrimaryCtxRetain(&cuCtx, cuDevice); ctxErr != CUDA_SUCCESS) {
		std::cerr << "[OptiX] cuDevicePrimaryCtxRetain failed\n";
		return false;
	}

	// Attempt to create OptiX device context
	OptixDeviceContext context = nullptr;
	OptixDeviceContextOptions options{};
	options.logCallbackFunction = &contextLogCallback;
	options.logCallbackLevel = kDefaultLogLevel;

	const OptixResult ctxCreateRes = optixDeviceContextCreate(cuCtx, &options, &context);

	// Cleanup temporary resources
	if (context) {
		optixDeviceContextDestroy(context);
	}
	if (cuCtx) {
		cuDevicePrimaryCtxRelease(cuDevice);
	}

	if (ctxCreateRes != OPTIX_SUCCESS) {
		std::cerr << "[OptiX] optixDeviceContextCreate failed: "
				  << optixGetErrorString(ctxCreateRes) << "\n";
		return false;
	}

	std::cout << "[OptiX] OptiX is available and functional!\n";
	return true;
}

bool OptiXRenderer::getDiagnostics(OptixDiagnostics& out) noexcept {
	out = OptixDiagnostics{};
	out.available = false;

	auto fail = [&](const char* reason) {
		// std::string::copy instead of strncpy - avoids MSVC's C4996
		// "unsafe function" flag on strncpy without needing
		// _CRT_SECURE_NO_WARNINGS or a Windows-only strncpy_s branch.
		std::string s(reason);
		size_t n = (std::min)(s.size(), sizeof(out.failure_reason) - 1);
		s.copy(out.failure_reason, n);
		out.failure_reason[n] = '\0';
		return false;
	};

	// Step 1: Check CUDA device availability (mirrors isAvailable() exactly -
	// see that function's own comments for why each step is checked in this
	// order).
	int deviceCount = 0;
	const cudaError_t cudaErr = cudaGetDeviceCount(&deviceCount);
	if (cudaErr != cudaSuccess || deviceCount == 0) {
		return fail(cudaErr != cudaSuccess ? cudaGetErrorString(cudaErr)
											: "No CUDA devices found");
	}

	if (const cudaError_t setErr = cudaSetDevice(selectedCudaDevice()); setErr != cudaSuccess) {
		return fail(cudaGetErrorString(setErr));
	}

	// Driver/runtime version and VRAM are available as soon as a device is
	// selected - captured here even though the OptiX-specific steps below
	// haven't run yet, so a driver/runtime mismatch is visible in the report
	// even when OptiX itself later fails to initialize.
	int driverVer = 0, runtimeVer = 0;
	cudaDriverGetVersion(&driverVer);
	cudaRuntimeGetVersion(&runtimeVer);
	out.cuda_driver_version  = driverVer;
	out.cuda_runtime_version = runtimeVer;

	size_t freeBytes = 0, totalBytes = 0;
	if (cudaMemGetInfo(&freeBytes, &totalBytes) == cudaSuccess) {
		out.vram_free_bytes  = static_cast<unsigned long long>(freeBytes);
		out.vram_total_bytes = static_cast<unsigned long long>(totalBytes);
	}

	if (const OptixResult initRes = optixInit(); initRes != OPTIX_SUCCESS) {
		return fail(optixGetErrorString(initRes));
	}
	out.optix_abi_version = OPTIX_VERSION;

	if (const CUresult cuInitErr = cuInit(0); cuInitErr != CUDA_SUCCESS) {
		return fail("cuInit failed");
	}

	CUdevice cuDevice;
	if (const CUresult devErr = cuDeviceGet(&cuDevice, selectedCudaDevice()); devErr != CUDA_SUCCESS) {
		return fail("cuDeviceGet failed");
	}

	cuDeviceGetName(out.device_name, static_cast<int>(sizeof(out.device_name)), cuDevice);

	CUcontext cuCtx = nullptr;
	if (const CUresult ctxErr = cuDevicePrimaryCtxRetain(&cuCtx, cuDevice); ctxErr != CUDA_SUCCESS) {
		return fail("cuDevicePrimaryCtxRetain failed");
	}

	OptixDeviceContext context = nullptr;
	OptixDeviceContextOptions options{};
	options.logCallbackFunction = &contextLogCallback;
	options.logCallbackLevel   = kDefaultLogLevel;

	const OptixResult ctxCreateRes = optixDeviceContextCreate(cuCtx, &options, &context);

	if (context) {
		optixDeviceContextDestroy(context);
	}
	if (cuCtx) {
		cuDevicePrimaryCtxRelease(cuDevice);
	}

	if (ctxCreateRes != OPTIX_SUCCESS) {
		return fail(optixGetErrorString(ctxCreateRes));
	}

	out.available = true;
	return true;
}

bool OptiXRenderer::initialize() {
	refuseIfGpuForbidden();
	if (!createContext()) {
		std::cerr << "Failed to create OptiX context\n";
		return false;
	}

	// The default renderer's modules are made later, by ensureDefaultRenderer(): all this checks now is that its program files are there.
	for (const char *file : {"optix_programs.ptx", "optix_programs_sphere.ptx", "optix_programs_quad.ptx", "optix_programs_disk.ptx", "optix_programs_cylinder.ptx", "optix_programs_triangle.ptx"}) {
		if (!ptxFileExists(file)) {
			std::cerr << "[OptiX] Could not find PTX file: " << file << "\n";
			return false;
		}
	}

	// Allocate launch params buffer
	CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_launchParams_), sizeof(LaunchParams)));

	std::cout << "[OptiX] Renderer initialized successfully\n";
	return true;
}

bool OptiXRenderer::ptxFileExists(const char* filename) const {
	for (const std::string &path : {std::string("gpu/optix/") + filename, std::string("optix_output/") + filename, std::string("./") + filename}) {
		std::ifstream file(path.c_str(), std::ios::binary);
		if (file.good()) return true;
	}
	return false;
}

void OptiXRenderer::destroyDefaultRendererObjects() {
	if (pipeline_) { optixPipelineDestroy(pipeline_); pipeline_ = nullptr; }
	for (OptixProgramGroup *pg : {&raygenPG_, &missPG_, &shadowMissPG_,
	                              &hitgroupSpherePG_, &hitgroupQuadPG_, &hitgroupBilinearPatchPG_, &hitgroupDiskPG_, &hitgroupCylinderPG_, &hitgroupTrianglePG_,
	                              &shadowHitgroupSpherePG_, &shadowHitgroupQuadPG_, &shadowHitgroupBilinearPatchPG_, &shadowHitgroupDiskPG_, &shadowHitgroupCylinderPG_, &shadowHitgroupTrianglePG_,
	                              &probeMissPG_, &probeHitgroupSpherePG_, &probeHitgroupQuadPG_, &probeHitgroupBilinearPatchPG_, &probeHitgroupDiskPG_, &probeHitgroupCylinderPG_,
	                              &probeHitgroupTrianglePG_}) {
		if (*pg) { optixProgramGroupDestroy(*pg); *pg = nullptr; }
	}
	for (OptixModule *m : {&module_, &moduleSphere_, &moduleQuad_, &moduleDisk_, &moduleCylinder_, &moduleTriangle_}) {
		if (*m) { optixModuleDestroy(*m); *m = nullptr; }
	}
	defaultRendererReady_ = false;
}

bool OptiXRenderer::ensureDefaultRenderer() {
	if (defaultRendererReady_) return true;
	// A step that fails leaves nothing behind, so that the next attempt starts clean instead of making the modules and program groups again over live ones.
	if (!createModule()) {
		std::cerr << "Failed to create OptiX module\n";
		destroyDefaultRendererObjects();
		return false;
	}
	if (!createProgramGroups()) {
		std::cerr << "Failed to create program groups\n";
		destroyDefaultRendererObjects();
		return false;
	}
	if (!linkPipeline()) {
		std::cerr << "Failed to link pipeline\n";
		destroyDefaultRendererObjects();
		return false;
	}
	defaultRendererReady_ = true;
	return true;
}

/// @brief Create OptiX context and initialize CUDA resources
/// @return true if context created successfully, false otherwise
bool OptiXRenderer::createContext() {
	// Initialize CUDA Driver API
	CU_CHECK(cuInit(0));

	// Get CUDA device (stored in cuDevice_ -- cleanup() needs it to release
	// the primary context correctly, see that member's own comment).
	CU_CHECK(cuDeviceGet(&cuDevice_, selectedCudaDevice()));

	// Query and display device name
	std::array<char, kMaxDeviceNameLength> deviceName{};
	cuDeviceGetName(deviceName.data(), static_cast<int>(deviceName.size()), cuDevice_);
	std::cout << "[OptiX] Using GPU: " << deviceName.data() << "\n";

	// Retain primary CUDA context (modern approach for CUDA 13.2+)
	CU_CHECK(cuDevicePrimaryCtxRetain(&cudaContext_, cuDevice_));

	// Create CUDA stream for asynchronous operations
	CUDA_CHECK(cudaStreamCreate(&stream_));

	// Initialize OptiX function table
	OPTIX_CHECK(optixInit());

	// Create OptiX device context with logging
	OptixDeviceContextOptions options{};
	options.logCallbackFunction = &contextLogCallback;
	options.logCallbackData = this;
	options.logCallbackLevel = kDefaultLogLevel;

	// Opt-in only: validation mode enables extra device-side checks (e.g.
	// SBT-index bounds) that OptiX otherwise skips for performance, at a
	// real per-launch cost. It caught a real, previously-undetected bug
	// this way - a shadow-ray miss-SBT index that was silently reading
	// adjacent heap memory - deterministically, on every scene, the moment
	// it was tried. See tests/integration/optix_validation_sweep_test.cpp
	// for the opt-in test that exercises this. Fixed for the context's
	// whole lifetime (matches OptiX's own API - there's no way to change
	// an existing context's validation mode after creation).
#pragma warning(suppress: 4996)
	const char* validationEnv = std::getenv("RAY_TRACER_OPTIX_VALIDATION");
	validationEnabled_ = validationEnv && std::string(validationEnv) == "1";
	options.validationMode = validationEnabled_
		? OPTIX_DEVICE_CONTEXT_VALIDATION_MODE_ALL
		: OPTIX_DEVICE_CONTEXT_VALIDATION_MODE_OFF;
	if (validationEnabled_) {
		std::cout << "[OptiX] Validation mode enabled (RAY_TRACER_OPTIX_VALIDATION=1)\n";
	}

	OPTIX_CHECK(optixDeviceContextCreate(cudaContext_, &options, &context_));

	// The OptiX cache keeps the compiled shaders between runs (compiling them cold takes 20 minutes). Its default size is too small for more than one build of them at a time, and
	// an entry that is evicted is a 20-minute recompile at the next start; this leaves room for several builds. OPTIX_CACHE_MAXSIZE, when set, is the user's or a script's own choice.
	// Only ever raised (never below what OptiX or the user already allows), the result is checked, and the log says when it changed anything. RAY_TRACER_OPTIX_CACHE_GB=n sets the limit
	// in gigabytes (the low water mark is three quarters of it), 0 leaves OptiX's own.
	{
		size_t wantGb = 4;
#pragma warning(suppress: 4996)
		if (const char *gb = std::getenv("RAY_TRACER_OPTIX_CACHE_GB")) wantGb = static_cast<size_t>((std::max)(0, std::atoi(gb)));
#pragma warning(suppress: 4996)
		const bool userChoseSize = std::getenv("OPTIX_CACHE_MAXSIZE") != nullptr;
		const size_t megabyte = size_t(1) << 20;
		size_t low = 0, high = 0;
		if (wantGb > 0 && !userChoseSize && optixDeviceContextGetCacheDatabaseSizes(context_, &low, &high) == OPTIX_SUCCESS && high < wantGb * 1024 * megabyte) {
			const size_t newHigh = wantGb * 1024 * megabyte, newLow = newHigh / 4 * 3;
			if (optixDeviceContextSetCacheDatabaseSizes(context_, newLow, newHigh) == OPTIX_SUCCESS) {
				// (said on every start, so only when asked for: RAY_TRACER_VERBOSE; a failure is always said)
#pragma warning(suppress: 4996)
				if (std::getenv("RAY_TRACER_VERBOSE"))
					std::cout << "[OptiX] The shader cache may now grow to " << newHigh / megabyte << " MB (it was " << high / megabyte << " MB); RAY_TRACER_OPTIX_CACHE_GB=0 leaves OptiX's own limit" << std::endl;
			} else {
				std::cerr << "[OptiX] Could not raise the shader cache limit; a shader build that does not fit may be recompiled (about 20 minutes) at a later start" << std::endl;
			}
		}
	}

	return true;
}

/// @brief Load PTX shader and create OptiX module
/// @return true if module created successfully, false otherwise
bool OptiXRenderer::createModule() {
	// Module compile options
	OptixModuleCompileOptions moduleCompileOptions = {};
	moduleCompileOptions.maxRegisterCount = OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT;
	moduleCompileOptions.optLevel = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
	moduleCompileOptions.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_MINIMAL;

	// Pipeline compile options (store as member for use in linkPipeline).
	// usesMotionBlur is a pipeline-wide enable (needed for
	// optixGetRayTime()/optixTrace()'s time argument to do anything at all)
	// but is safe for every scene, motion or not: scenes without motion keep
	// their GAS single-key and their rays always carry time=0.0f (see
	// optix_raygen.h), so optixGetRayTime() simply always returns 0 for them
	// - a provable no-op, not just an assumption.
	pipelineCompileOptions_.usesMotionBlur = true;
	// Single-level instancing (one IAS wrapping exactly the 2 child GASes
	// built in buildAccelerationStructure - triangles can't share a GAS
	// with the custom AABB primitives, see that function's comment), not
	// ALLOW_SINGLE_GAS anymore.
	pipelineCompileOptions_.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING;
	// Pipeline-wide UPPER BOUND on payload registers (not a per-call exact
	// count - trace_shadow_ray() passes just 1 word and the new probe ray
	// (optix_probe_hit.h) passes 7, both well under this). The radiance ray
	// is the largest user: attenuation(3) + emission(3) + dir(3) + seed(1) +
	// flag(1) + t(1) + brdf_or_light_pdf(1) + explicit_origin(3, flag==3
	// only - MaterialType::Subsurface's probe-walk exit point, see
	// optix_raygen.h) = 16, plus denoiser guide-layer AOVs: albedo(3) +
	// normal(3) = 6 more (p16-p21, see PathTracingPayload::albedo/normal's
	// comment in optix_types.h) = 22, plus eta(1) for pbrt-v4's Russian
	// Roulette etaScale correction (p22, see PathTracingPayload::eta's
	// comment) = 23, plus anyNonSpecularBounces(1) for Integrator "bool
	// regularize" (p23, an INPUT-only register - see optix_raygen.h's own
	// comment on p23, same convention as p12's prev_brdf_pdf for
	// __miss__ms) = 24, plus rgbChannel(1) for dispersive-dielectric
	// wavelength tracking (p24, a genuinely PERSISTENT in/out register -
	// unlike p22/p23, closest-hit programs both READ it via
	// optixGetPayload_24() and WRITE it back via optixSetPayload_24() so it
	// survives across bounces - see optix_raygen.h's own rgbChannel comment
	// and shade_material()'s inout_rgb_channel parameter comment,
	// optix_device_helpers.h) = 25 total. Comfortably under OptiX's
	// 32-register hard limit.
	pipelineCompileOptions_.numPayloadValues = 28;  // p25-p27: mis_origin (optix_raygen.h)
	pipelineCompileOptions_.numAttributeValues = 4;  // Sphere: center.xyz + radius (4 attrs)
	pipelineCompileOptions_.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
	pipelineCompileOptions_.pipelineLaunchParamsVariableName = "params";
	// Triangles use OptiX's built-in hardware-accelerated triangle geometry
	// (OPTIX_BUILD_INPUT_TYPE_TRIANGLES, see build_triangle_mesh_gpu below);
	// spheres/quads/bilinear-patches stay custom AABB primitives. Both flags
	// must be declared together since the GAS mixes both kinds of build
	// input - matches pbrt-v4's own GPU renderer, which also uses built-in
	// triangles alongside custom quadric/bilinear-patch primitives.
	pipelineCompileOptions_.usesPrimitiveTypeFlags =
		OPTIX_PRIMITIVE_TYPE_FLAGS_CUSTOM | OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE;

	// The recursive programs are one core module and one module per geometry type (see optix_programs.cu); they all share the pipeline options above.
	struct ModuleFile { const char *file; OptixModule *module; };
	const ModuleFile modules[] = {
		{"optix_programs.ptx", &module_},
		{"optix_programs_sphere.ptx", &moduleSphere_},
		{"optix_programs_quad.ptx", &moduleQuad_},
		{"optix_programs_disk.ptx", &moduleDisk_},
		{"optix_programs_cylinder.ptx", &moduleCylinder_},
		{"optix_programs_triangle.ptx", &moduleTriangle_},
	};
	// All six are created in one call so OptiX's compile tasks of every module share the cores (see optixModulesCreateParallel).
	std::string ptxFiles[std::size(modules)];
	char logs[std::size(modules)][2048];
	size_t logSizes[std::size(modules)];
	std::vector<OptixModuleRequest> requests;
	for (size_t i = 0; i < std::size(modules); ++i) {
		ptxFiles[i] = loadPTX(modules[i].file);
		if (ptxFiles[i].empty()) {
			std::cerr << "Failed to load PTX\n";
			return false;
		}
		logSizes[i] = sizeof(logs[i]);
		requests.push_back({ptxFiles[i].c_str(), ptxFiles[i].size(), logs[i], &logSizes[i], modules[i].module});
	}
	// (timed: with a warm OptiX cache this takes a second or two, with a cold one many minutes, and a user who has just installed the program should be told which it is)
	const auto compileStart = std::chrono::steady_clock::now();
	{
		// With a cold OptiX cache (the first run on this computer, or after an update of the shaders) this takes about 20 minutes; a message after five seconds says so, so that nobody
		// takes it for a hang. (Joined when this block ends, however it ends.)
		struct SlowCompileNotice {
			std::mutex mutex;
			std::condition_variable wake;
			bool done = false;
			std::thread thread;
			SlowCompileNotice() : thread([this] {
				std::unique_lock<std::mutex> lock(mutex);
				if (!wake.wait_for(lock, std::chrono::seconds(5), [this] { return done; }))
					std::cout << "[OptiX] Compiling the default renderer's shaders. This happens once on this computer and again after an update, and takes up to 25 minutes (the wavefront renderer does not need it)." << std::endl;
			}) {}
			~SlowCompileNotice() {
				{ std::lock_guard<std::mutex> lock(mutex); done = true; }
				wake.notify_all();
				thread.join();
			}
		} notice;
		OPTIX_CHECK(optixModulesCreateParallel(context_, &moduleCompileOptions, &pipelineCompileOptions_, requests));
	}
	{
		// (formatted here, not with std::fixed on std::cout: that would change how every later message prints its numbers)
		char took[32];
		std::snprintf(took, sizeof(took), "%.1f", std::chrono::duration<double>(std::chrono::steady_clock::now() - compileStart).count());
		std::cout << "[OptiX] The default renderer's " << std::size(modules) << " modules were ready in " << took << " s" << std::endl;
	}
	for (size_t i = 0; i < std::size(modules); ++i) {
		if (logSizes[i] > 1) {
			std::cout << "[OptiX] Module creation log (" << modules[i].file << "):\n" << logs[i] << "\n";
		}
	}

	return true;
}

std::string OptiXRenderer::loadPTX(const char* filename) const {
	// Try loading from build output directory
	std::string paths[] = {
		std::string("gpu/optix/") + filename,
		std::string("optix_output/") + filename,
		std::string("./") + filename
	};

	for (const auto& path : paths) {
		std::ifstream file(path.c_str(), std::ios::binary);
		if (file.good()) {
			std::stringstream buffer;
			buffer << file.rdbuf();
			std::cout << "[OptiX] Loaded PTX from: " << path << "\n";
			return buffer.str();
		}
	}

	std::cerr << "[OptiX] Could not find PTX file: " << filename << "\n";
	return "";
}

bool OptiXRenderer::createProgramGroups() {
	using namespace optix_pg;
	const auto make = [this](const OptixProgramGroupDesc &desc, OptixProgramGroup *out) { create(context_, desc, out); };

	// Where each program lives (see optix_programs.cu): the core module holds the raygen, miss, shadow any-hit and probe programs; each geometry module holds that shape's
	// intersection and closest-hit programs (the triangle's any-hit too). A hit group takes each of its programs from its own module.
	const OptixModule core = module_;
	const auto sphereIs = Prog{moduleSphere_, "__intersection__sphere"};
	const auto quadIs = Prog{moduleQuad_, "__intersection__quad"};
	const auto bilinearIs = Prog{moduleQuad_, "__intersection__bilinear_patch"};
	const auto diskIs = Prog{moduleDisk_, "__intersection__disk"};
	const auto cylinderIs = Prog{moduleCylinder_, "__intersection__cylinder"};
	const Prog none{nullptr, nullptr};

	// Raygen and miss programs (radiance, shadow, and the BSSRDF probe's no-op miss)
	make(raygen(core, "__raygen__rg"), &raygenPG_);
	make(miss(core, "__miss__ms"), &missPG_);
	make(miss(core, "__miss__shadow"), &shadowMissPG_);

	// Radiance hit groups: intersection + closest-hit per custom shape. A triangle uses OptiX's built-in hardware triangle test (no intersection program bound), and
	// its any-hit is a no-op for the overwhelming majority of triangles (MaterialData::alphaMaskTexIdx < 0), only rejecting a candidate hit via
	// optixIgnoreIntersection() for OBJ/.mtl alpha-cutout materials (map_d) - see __anyhit__triangle's own comment (optix_intersection_triangle.h).
	make(hit(sphereIs, Prog{moduleSphere_, "__closesthit__sphere"}, none), &hitgroupSpherePG_);
	make(hit(quadIs, Prog{moduleQuad_, "__closesthit__quad"}, none), &hitgroupQuadPG_);
	make(hit(bilinearIs, Prog{moduleQuad_, "__closesthit__bilinear_patch"}, none), &hitgroupBilinearPatchPG_);
	make(hit(diskIs, Prog{moduleDisk_, "__closesthit__disk"}, none), &hitgroupDiskPG_);
	make(hit(cylinderIs, Prog{moduleCylinder_, "__closesthit__cylinder"}, none), &hitgroupCylinderPG_);
	make(hit(none, Prog{moduleTriangle_, "__closesthit__triangle"}, Prog{moduleTriangle_, "__anyhit__triangle"}), &hitgroupTrianglePG_);

	// Shadow hit groups: any-hit only (the triangle's with the hardware test, like the radiance group above)
	make(hit(sphereIs, none, Prog{core, "__anyhit__shadow_sphere"}), &shadowHitgroupSpherePG_);
	make(hit(quadIs, none, Prog{core, "__anyhit__shadow_quad"}), &shadowHitgroupQuadPG_);
	make(hit(bilinearIs, none, Prog{core, "__anyhit__shadow_bilinear_patch"}), &shadowHitgroupBilinearPatchPG_);
	make(hit(diskIs, none, Prog{core, "__anyhit__shadow_disk"}), &shadowHitgroupDiskPG_);
	make(hit(cylinderIs, none, Prog{core, "__anyhit__shadow_cylinder"}), &shadowHitgroupCylinderPG_);
	make(hit(none, none, Prog{core, "__anyhit__shadow_triangle"}), &shadowHitgroupTrianglePG_);

	// RAY_TYPE_PROBE program groups (recursive backend only, Phase 1 BSSRDF - see optix_types.h's RAY_TYPE_PROBE comment and optix_probe_hit.h): one dedicated miss
	// program (a true no-op) plus one closest-hit-only hit group per geometry type, reusing each type's existing __intersection__ program (only closest-hit differs
	// per ray type).
	make(miss(core, "__miss__probe"), &probeMissPG_);
	make(hit(sphereIs, Prog{core, "__closesthit__probe_sphere"}, none), &probeHitgroupSpherePG_);
	make(hit(quadIs, Prog{core, "__closesthit__probe_quad"}, none), &probeHitgroupQuadPG_);
	make(hit(bilinearIs, Prog{core, "__closesthit__probe_bilinear_patch"}, none), &probeHitgroupBilinearPatchPG_);
	make(hit(diskIs, Prog{core, "__closesthit__probe_disk"}, none), &probeHitgroupDiskPG_);
	make(hit(cylinderIs, Prog{core, "__closesthit__probe_cylinder"}, none), &probeHitgroupCylinderPG_);
	make(hit(none, Prog{core, "__closesthit__probe_triangle"}, none), &probeHitgroupTrianglePG_);

	std::cout << "[OptiX] Created program groups: raygen, miss (radiance + shadow + probe), sphere hit (radiance + shadow + probe), quad hit (radiance + shadow + probe), bilinear patch hit (radiance + shadow + probe), disk hit (radiance + shadow + probe), cylinder hit (radiance + shadow + probe), triangle hit (radiance + shadow + probe)\n";
	return true;
}

bool OptiXRenderer::linkPipeline() {
	// Collect all program groups (radiance + shadow + probe)
	OptixProgramGroup programGroups[] = {
		raygenPG_,
		missPG_,
		shadowMissPG_,
		probeMissPG_,
		hitgroupSpherePG_,
		hitgroupQuadPG_,
		hitgroupBilinearPatchPG_,
		hitgroupDiskPG_,
		hitgroupCylinderPG_,
		hitgroupTrianglePG_,
		shadowHitgroupSpherePG_,
		shadowHitgroupQuadPG_,
		shadowHitgroupBilinearPatchPG_,
		shadowHitgroupDiskPG_,
		shadowHitgroupCylinderPG_,
		shadowHitgroupTrianglePG_,
		probeHitgroupSpherePG_,
		probeHitgroupQuadPG_,
		probeHitgroupBilinearPatchPG_,
		probeHitgroupDiskPG_,
		probeHitgroupCylinderPG_,
		probeHitgroupTrianglePG_
	};

	// Pipeline link options. maxTraceDepth stays 2 (primary + one nested
	// trace): the probe walk (bssrdf_probe_walk(), optix_device_helpers.h)
	// issues a bounded LOOP of SEQUENTIAL, non-nested trace_probe_ray()
	// calls from within the primary hit's own closest-hit program - each
	// one completes before the next starts, exactly like trace_shadow_ray()
	// already does for NEE - so the deepest simultaneous trace nesting is
	// still 2, not 2+numProbeSteps.
	OptixPipelineLinkOptions pipelineLinkOptions = {};
	pipelineLinkOptions.maxTraceDepth = 2;  // Primary + shadow/probe/indirect

	// Create pipeline
	char log[2048];
	size_t logSize = sizeof(log);
	OPTIX_CHECK(optixPipelineCreate(
		context_,
		&pipelineCompileOptions_,  // Use member variable from createModule
		&pipelineLinkOptions,
		programGroups,
		sizeof(programGroups) / sizeof(programGroups[0]),
		log,
		&logSize,
		&pipeline_
	));

	if (logSize > 1) {
		std::cout << "[OptiX] Pipeline creation log:\n" << log << "\n";
	}

	// Set stack sizes
	OptixStackSizes stackSizes = {};
	for (auto pg : programGroups) {
		OPTIX_CHECK(optixUtilAccumulateStackSizes(pg, &stackSizes, pipeline_));
	}

	uint32_t maxTraceDepth = 10;  // Match renderer max depth
	uint32_t maxCCDepth = 0;
	uint32_t maxDCDepth = 0;
	uint32_t directCallableStackSizeFromTraversal;
	uint32_t directCallableStackSizeFromState;
	uint32_t continuationStackSize;

	OPTIX_CHECK(optixUtilComputeStackSizes(
		&stackSizes,
		maxTraceDepth,
		maxCCDepth,
		maxDCDepth,
		&directCallableStackSizeFromTraversal,
		&directCallableStackSizeFromState,
		&continuationStackSize
	));

	OPTIX_CHECK(optixPipelineSetStackSize(
		pipeline_,
		directCallableStackSizeFromTraversal,
		directCallableStackSizeFromState,
		continuationStackSize,
		2  // maxTraversableGraphDepth: IAS -> GAS is 2 levels (single-level instancing)
	));

	std::cout << "[OptiX] Pipeline linked successfully\n";
	return true;
}

