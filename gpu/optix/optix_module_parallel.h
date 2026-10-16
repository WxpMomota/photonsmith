// optix_module_parallel.h -- optixModuleCreate, with OptiX's own compile work spread over every core.
//
// optixModuleCreate compiles a PTX module on the calling thread. The recursive module has six closest-hit programs that each inline the
// whole material system, and a cold OptiX cache meant ~5 minutes of one core (the wavefront and SPPM modules take a fraction of that). The same call
// split into tasks (optixModuleCreateWithTasks / optixTaskExecute) lets OptiX compile independent entry functions at the same time. The result is the
// same module, the compiled code is cached the same way, and a warm cache makes this as quick as before: nothing is recompiled.
#pragma once

#include <optix.h>
#include <optix_stubs.h>

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>
#include <vector>

// One module to create: its PTX (or OptiX-IR) and where to put the module and the compile log.
struct OptixModuleRequest {
	const char* input;
	size_t inputSize;
	char* logString;
	size_t* logStringSize;
	OptixModule* module;
};

// Creates several modules at once. The first tasks of ALL of them go into one queue worked by every core: a module with only a few entry functions (the recursive
// backend's geometry modules have two to four) would otherwise leave most cores idle while it compiles, and the modules are created one after another.
inline OptixResult optixModulesCreateParallel(OptixDeviceContext context,
                                              const OptixModuleCompileOptions* moduleCompileOptions,
                                              const OptixPipelineCompileOptions* pipelineCompileOptions,
                                              const std::vector<OptixModuleRequest>& requests) {
	std::vector<OptixTask> queue;
	for (const OptixModuleRequest& r : requests) {
		OptixTask firstTask = nullptr;
		const OptixResult result = optixModuleCreateWithTasks(context, moduleCompileOptions, pipelineCompileOptions, r.input, r.inputSize,
		                                                      r.logString, r.logStringSize, r.module, &firstTask);
		if (result != OPTIX_SUCCESS) return result;
		if (firstTask != nullptr) queue.push_back(firstTask);
	}
	if (queue.empty()) return OPTIX_SUCCESS;

	const unsigned int workers = std::clamp(std::thread::hardware_concurrency(), 1u, 32u);
	const unsigned int maxAdditional = workers * 2;

	std::mutex mutex;
	std::condition_variable wake;
	size_t unfinished = queue.size();  // queued + executing
	OptixResult firstError = OPTIX_SUCCESS;

	auto work = [&]() {
		std::vector<OptixTask> additional(maxAdditional);
		for (;;) {
			OptixTask task;
			{
				std::unique_lock<std::mutex> lock(mutex);
				wake.wait(lock, [&] { return !queue.empty() || unfinished == 0; });
				if (queue.empty()) return;  // nothing queued and nothing running: all done
				task = queue.back();
				queue.pop_back();
			}
			unsigned int created = 0;
			const OptixResult r = optixTaskExecute(task, additional.data(), maxAdditional, &created);
			{
				std::lock_guard<std::mutex> lock(mutex);
				if (r != OPTIX_SUCCESS && firstError == OPTIX_SUCCESS) firstError = r;
				if (r == OPTIX_SUCCESS) {
					queue.insert(queue.end(), additional.begin(), additional.begin() + created);
					unfinished += created;
				}
				--unfinished;
			}
			wake.notify_all();
		}
	};

	std::vector<std::thread> threads;
	for (unsigned int i = 1; i < workers; ++i) threads.emplace_back(work);
	work();
	for (std::thread& t : threads) t.join();
	return firstError;
}

// One module: optixModuleCreate with the compile work spread over every core.
inline OptixResult optixModuleCreateParallel(OptixDeviceContext context,
                                             const OptixModuleCompileOptions* moduleCompileOptions,
                                             const OptixPipelineCompileOptions* pipelineCompileOptions,
                                             const char* input, size_t inputSize,
                                             char* logString, size_t* logStringSize,
                                             OptixModule* module) {
	return optixModulesCreateParallel(context, moduleCompileOptions, pipelineCompileOptions, {OptixModuleRequest{input, inputSize, logString, logStringSize, module}});
}
