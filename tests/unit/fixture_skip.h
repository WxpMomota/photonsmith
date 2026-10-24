#pragma once
// fixture_skip.h -- what a test does when a file it reads, one that is in the repository (a scene in pbrt_scenes/, a snapshot in tests/data/, a list in qt_gui/), is not found
// from the working directory.
//
// Run from somewhere else (a build folder, an IDE's own folder) it cannot find them, and skipping is right. But a fixture that was renamed or deleted turned its tests into
// quiet skips that the summary never mentioned. With RT_REQUIRE_FIXTURES set - CI and scripts/run_tests_parallel.ps1 set it, as they set RT_REQUIRE_LAUNCHER for the
// launcher - a missing fixture is a failure.
//
//     if (!in) RT_SKIP_MISSING_FIXTURE(path << " is not next to the working directory");

#include <cstdlib>

#include <gtest/gtest.h>

#define RT_SKIP_MISSING_FIXTURE(message)                                                          \
	do {                                                                                          \
		if (std::getenv("RT_REQUIRE_FIXTURES")) {                                                 \
			FAIL() << message << " (a file of the repository; RT_REQUIRE_FIXTURES is set)";       \
		} else {                                                                                  \
			GTEST_SKIP() << message;                                                              \
		}                                                                                         \
	} while (0)
