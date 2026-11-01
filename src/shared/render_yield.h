#pragma once
// render_yield.h -- a place inside a frame where a render may give the graphics card back. A renderer calls between() when it has come back to the program between two passes; whoever runs it
// can install what happens there (launcher/smart_power.h: a pause while something else needs the card). With nothing installed it is one atomic read.

#include <atomic>

namespace render_yield {

using Hook = void (*)();
inline std::atomic<Hook> &hook() {
	static std::atomic<Hook> installed{nullptr};
	return installed;
}
inline void between() {
	if (const Hook h = hook().load(std::memory_order_relaxed)) h();
}

}  // namespace render_yield
