#pragma once
// gpu_memory.h -- how much memory an NVIDIA graphics card has free, asked of the driver's management library (NVML: nvml.dll on Windows, libnvidia-ml.so.1 on Linux). The library is
// loaded when asked for, so nothing links against it, and a computer without it (no NVIDIA driver, a Mac) simply gets "not known". Asking CUDA instead would open a CUDA context in the
// launcher, which holds some 300 MB of the card for the whole run - memory the frames' own workers need.

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <dlfcn.h>
#endif

namespace gpu_memory {

// Free memory of GPU `device` (as nvidia-smi numbers them) in megabytes; 0 when it cannot be said.
inline long long freeMb(int device) {
#if defined(_WIN32) || defined(__linux__)
	struct Memory {
		unsigned long long total, free, used;   // nvmlMemory_t
	};
	using Init = int (*)();
	using Shutdown = int (*)();
	using GetHandle = int (*)(unsigned int, void **);
	using GetMemory = int (*)(void *, Memory *);
#ifdef _WIN32
	HMODULE lib = LoadLibraryA("nvml.dll");
	if (!lib) return 0;
	const auto sym = [lib](const char *name) { return reinterpret_cast<void *>(GetProcAddress(lib, name)); };
#else
	void *lib = dlopen("libnvidia-ml.so.1", RTLD_NOW);
	if (!lib) return 0;
	const auto sym = [lib](const char *name) { return dlsym(lib, name); };
#endif
	const auto init = reinterpret_cast<Init>(sym("nvmlInit_v2"));
	const auto shutdown = reinterpret_cast<Shutdown>(sym("nvmlShutdown"));
	const auto handleOf = reinterpret_cast<GetHandle>(sym("nvmlDeviceGetHandleByIndex_v2"));
	const auto memoryOf = reinterpret_cast<GetMemory>(sym("nvmlDeviceGetMemoryInfo"));
	long long mb = 0;
	if (init && shutdown && handleOf && memoryOf && init() == 0) {   // (0 = NVML_SUCCESS)
		void *handle = nullptr;
		Memory memory{};
		if (handleOf(static_cast<unsigned int>(device < 0 ? 0 : device), &handle) == 0 && memoryOf(handle, &memory) == 0)
			mb = static_cast<long long>(memory.free / (1024ull * 1024ull));
		shutdown();
	}
#ifdef _WIN32
	FreeLibrary(lib);
#else
	dlclose(lib);
#endif
	return mb;
#else
	(void)device;
	return 0;
#endif
}

}  // namespace gpu_memory
