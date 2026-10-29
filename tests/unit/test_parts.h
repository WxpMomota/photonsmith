#pragma once
// test_parts.h - a sweep over many things (every film at several times, every scene) cut into parts that are tests of their own.
//
// scripts/run_tests_parallel.ps1 shares the tests out among its processes by count, not by time, so one test that checks every film keeps one process busy for minutes
// while the others are done: three such sweeps set the Fast tier at 280 s (2026-10-28). A sweep written as a TEST_P over the parts below is kParts tests, which the
// processes share. Part k checks the things whose index is k modulo kParts; what is swept (the film table, the scene registry) is still read inside the test, not
// when the tests are listed, so listing them reads no scene folder and needs no setting.
//
//     class EveryFilm : public ::testing::TestWithParam<int> {};
//     TEST_P(EveryFilm, Flattens) { for (std::size_t i = 0; i < films.size(); ++i) if (test_parts::inPart(i, GetParam())) ... }
//     INSTANTIATE_TEST_SUITE_P(Parts, EveryFilm, test_parts::all());

#include <cstddef>

#include <gtest/gtest.h>

namespace test_parts {

constexpr int kParts = 32;

inline bool inPart(std::size_t index, int part) { return static_cast<int>(index % kParts) == part; }

inline auto all() { return ::testing::Range(0, kParts); }

}  // namespace test_parts
