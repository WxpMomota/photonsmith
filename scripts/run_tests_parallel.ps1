#!/usr/bin/env pwsh
<#
.SYNOPSIS
	Run ray_tracer_tests.exe sharded across multiple parallel processes.
.DESCRIPTION
	Google Test has built-in sharding support (GTEST_TOTAL_SHARDS/
	GTEST_SHARD_INDEX env vars - no test-side code changes needed): each
	shard runs a disjoint ~1/N slice of the full suite. This script launches
	one process per shard concurrently and aggregates the results.

	Correctness caveat this script exists to handle: naively running
	multiple ray_tracer_tests.exe processes at once is NOT safe out of the
	box. Some tests write output through a fixed relative filename (e.g.
	specparity_B3_spec.ppm) with no per-process uniqueness, so two shards
	racing on the same filename corrupt each other's output; other tests
	(e.g. pbrt_example_scenes_tests.cpp) scan pbrt_scenes/ by a relative
	path to build their parameterized instantiation list. This script gives
	each shard its own scratch working directory (fixing the first problem)
	with a directory JUNCTION back to every real top-level project
	directory (fixing the second, since junctions are reads-through, not
	copies) - verified to reproduce the exact same pass count as a plain
	serial run (4009/4009 passed, 0 failed, arithmetic-exact against total
	tests ran), where an earlier, working-directory-naive version of this
	script silently lost ~150 tests to the second problem and produced
	spurious OptiX/render failures from the first.

	PERFORMANCE CAVEAT - read before assuming -Tier All makes the full
	suite faster: it measurably does NOT, for the full suite as it stands
	today. Measured on this project's own machine: 2 shards of ~half the
	suite each took ~230s wall-clock, slower than the ~215s a single
	serial process takes for the WHOLE suite. Root causes: (1) many
	render/integration tests already spawn their own worker-thread pool
	sized to every logical core (determine_render_thread_count(), src/
	TheRestOfYourLife/thread_count.h) - this script caps each shard's pool
	via that file's own RAY_TRACER_THREADS override to reduce (not
	eliminate) oversubscription; (2) a handful of very expensive GPU-
	touching integration/parity tests dominate the suite's total runtime,
	contend for the ONE shared physical GPU regardless of CPU thread caps,
	and aren't evenly distributed by gtest's count-based (not time-based)
	sharding, so wall-clock ends up bounded by whichever shard drew the
	unlucky slow tests rather than shrinking with shard count.

	Use -Tier Fast instead: it excludes every GPU-touching and thread-
	pool-oversubscribing test (see -Tier below), so none of the two root
	causes above apply, and sharding that subset actually does speed
	things up. Its two correctness fixes (per-shard scratch cwd, output
	isolation) are real and worth keeping regardless of tier.

	Requires the tests project to already be built (see build_all.ps1 /
	setup_env.bat) - this script only RUNS the existing exe, it doesn't
	build it.
.PARAMETER Configuration
	Build configuration whose test exe to run: Debug or Release (default: Release)
.PARAMETER Shards
	Number of parallel shards (default: logical processor count). Forced
	to 1 when -Tier Slow, regardless of what's passed here - see -Tier.
.PARAMETER Filter
	Optional --gtest_filter pattern. Composes with -Tier Fast (both must
	match - gtest's positive-pattern-then-negative-pattern grammar allows
	this exactly). Does NOT compose with -Tier Slow: gtest can't intersect
	two independent positive-pattern lists in one filter string, so with
	-Tier Slow this is ignored (with a warning) if also given - pass the
	full desired pattern directly via -Filter with -Tier All instead.
.PARAMETER Tier
	Which slice of the suite to run (default: Split, the whole suite the fast way; All when a -Filter is
	given):
	  - Fast: excludes every GPU-touching test (matched by the *Gpu*/
	    *GPU*/*gpu* naming convention this codebase's test suites follow,
	    plus two suites that don't follow it - WavefrontRenderTest,
	    OptixValidationSweepTest) and the two known thread-pool-
	    oversubscribing suites (BdptFirstRender, SppmFirstSlice). Safe to
	    shard aggressively: no GPU contention, no thread-pool
	    oversubscription beyond this script's own per-shard cap.
	  - Slow: only the tests Fast excludes (423 of the suite's 4113, most
	    of them individual instances of a few parameterized per-scene
	    suites like PbrtExampleSceneTest/CpuGpuLightParityTest, not 423
	    distinct hand-written tests - confirmed to exactly partition the
	    full suite with Fast's 3690, nothing double-counted or missing).
	    Forces -Shards 1 -
	    these tests contend for the one physical GPU or spawn full-core
	    pools themselves, so sharding them provides no benefit and only
	    reintroduces the exact oversubscription/contention -Tier Fast
	    exists to avoid.
	  - Render: the CPU-render suites that make up most of what used to be Fast (the backend agreement, chromatic-media, bundled-scene and
	    reference-pack tests: about 380 of Fast's 426 s of test time, October 2026). Each already renders with every core, so sharding them
	    only oversubscribes the machine: a 16-shard Fast run of that mix timed out after 1800 s. They run in 4 shards (-RenderShards N; 4 or 8
	    take 154 s, one process 338 s, 16 shards 187 s). Matched by $renderSuites below; a heavy new suite that is not listed lands in
	    Fast, which still works but loses time.
	  - All: no tier filter (original behavior).
	  - Split: the whole suite: Fast (cheap tests, 16 shards, seconds), then Render and Slow at the same time (Render is CPU-bound, Slow
	    mostly waits on the GPU), or one after the other with -Sequential. Exits non-zero if any part fails. -Filter is not supported (see Slow).
	    Measured: sharding Slow over 2-4 processes is NOT faster (170 s with 1, 193-198 s with 2-4) - separate processes time-slice the GPU.
	NAMING-CONVENTION CAVEAT: the GPU exclusion pattern relies on GPU
	tests naming themselves with Gpu/GPU/gpu somewhere in the suite OR
	test name (confirmed to work even for suites that mix CPU and GPU
	tests, e.g. BenchmarkTest.SmallGPURender vs .SmallCPURender - gtest
	filters match the full "Suite.Test" string). A new GPU test added
	without that in its name will silently land in the Fast tier instead
	of being excluded - if you add a GPU test with an unconventional name
	(like the two already special-cased above), add it to
	$gpuAndOversubscribingFilter explicitly. The failure mode runs the
	other way too, harmlessly: a handful of pure-CPU tests that merely
	mention "Gpu" in their own name (e.g. FindSceneTest.
	CornellBoxIsGpuCompatible, checking a scene's gpu_compatible metadata
	flag - never touches the GPU) get needlessly pulled into the Slow
	tier along with the real GPU tests. Confirmed safe (they just lose
	the chance to be sharded, nothing runs incorrectly), not worth a more
	precise pattern for the handful of tests this affects.
.EXAMPLE
	.\run_tests_parallel.ps1
	.\run_tests_parallel.ps1 -Tier Fast -Shards 8
	.\run_tests_parallel.ps1 -Tier Slow
	.\run_tests_parallel.ps1 -Tier Fast -Filter "CameraMathTest.*"
#>

param(
	[ValidateSet("Debug", "Release")]
	[string]$Configuration = "Release",

	[int]$Shards = [System.Environment]::ProcessorCount,

	[string]$Filter = "",

	[ValidateSet("", "All", "Fast", "Render", "Slow", "Split")]
	[string]$Tier = "",

	# -Tier Render / Split: how many processes the CPU-render tests are spread over (each gets its share of the cores). Measured on 16 cores (509 tests): 1 process 338 s,
	# 4 or 8 processes 154 s, 16 processes 187 s.
	[int]$RenderShards = 4,

	# -Tier Split: run Render and Slow one after the other instead of at the same time.
	[switch]$Sequential,

	# Generous default (the full serial suite alone takes ~215s) - exists so
	# one hung shard can't block the script forever, not to tightly bound
	# normal runs.
	[int]$TimeoutSeconds = 1800
)

$ErrorActionPreference = "Stop"

# No -Tier: the whole suite the fast way (Split, about 6.5 minutes on this machine; -Tier All, one set of shards for everything, is the slowest way to run it and takes 10 to 25),
# or, when a -Filter is given (which Split cannot take), All with that filter.
if (-not $Tier) { $Tier = if ($Filter) { "All" } else { "Split" } }

if ($Tier -eq "Split") {
	if ($Filter) { Write-Host "[FAIL] -Filter cannot be combined with -Tier Split." -ForegroundColor Red; exit 1 }
	$splitTimer = [System.Diagnostics.Stopwatch]::StartNew()
	& $PSCommandPath -Configuration $Configuration -Shards $Shards -Tier Fast -TimeoutSeconds $TimeoutSeconds
	$fastExit = $LASTEXITCODE
	Write-Host ("Split: Fast took {0:N0} s" -f $splitTimer.Elapsed.TotalSeconds) -ForegroundColor Cyan
	if ($Sequential) {
		& $PSCommandPath -Configuration $Configuration -Shards $RenderShards -Tier Render -TimeoutSeconds $TimeoutSeconds
		$renderExit = $LASTEXITCODE
		& $PSCommandPath -Configuration $Configuration -Tier Slow -TimeoutSeconds $TimeoutSeconds
		$slowExit = $LASTEXITCODE
	} else {
		# Two child scripts at once, each with its own scratch directory and TEMP (see the per-shard setup below), their output in files shown afterwards.
		$splitLogDir = Join-Path $env:TEMP "ray_tracer_split_${PID}_$(Get-Date -Format 'yyyyMMdd_HHmmss')"
		New-Item -ItemType Directory -Force -Path $splitLogDir | Out-Null
		$children = foreach ($part in @(@("Render", $RenderShards), @("Slow", 1))) {
			$childArgs = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Configuration $Configuration -Tier $($part[0]) -Shards $($part[1]) -TimeoutSeconds $TimeoutSeconds"
			$proc = Start-Process -FilePath "powershell.exe" -ArgumentList $childArgs -NoNewWindow -PassThru `
				-RedirectStandardOutput (Join-Path $splitLogDir "$($part[0]).log") -RedirectStandardError (Join-Path $splitLogDir "$($part[0]).err")
			$null = $proc.Handle   # keeps the exit code readable after the process ends
			[pscustomobject]@{ Name = $part[0]; Process = $proc }
		}
		foreach ($child in $children) { $child.Process.WaitForExit() }
		foreach ($child in $children) {
			# (how long each part took: the longer of the two is what a Split run waits for, so that is the one to speed up)
			$partSeconds = ($child.Process.ExitTime - $child.Process.StartTime).TotalSeconds
			Write-Host ("----- {0} ({1:N0} s) -----" -f $child.Name, $partSeconds) -ForegroundColor Cyan
			Get-Content (Join-Path $splitLogDir "$($child.Name).log") | Select-Object -Last 12
		}
		$renderExit = ($children | Where-Object Name -eq "Render").Process.ExitCode
		$slowExit = ($children | Where-Object Name -eq "Slow").Process.ExitCode
	}
	Write-Host ("Split: total {0:N0} s" -f $splitTimer.Elapsed.TotalSeconds) -ForegroundColor Cyan
	if ($fastExit -ne 0 -or $renderExit -ne 0 -or $slowExit -ne 0) {
		Write-Host "[FAIL] Split: Fast exit $fastExit, Render exit $renderExit, Slow exit $slowExit" -ForegroundColor Red
		exit 1
	}
	Write-Host "Split: Fast, Render and Slow all passed." -ForegroundColor Green
	exit 0
}

# See -Tier's own parameter comment above for the full rationale and the
# naming-convention caveat. Verified suite-by-suite (not assumed) that
# gtest's *Gpu*/*GPU*/*gpu* wildcard correctly separates every GPU test
# from its CPU siblings even within suites that mix both - the two
# entries after it are the only suites found that don't follow the
# naming convention at all.
# LivePreview*: the Live Preview integration tests (tests/integration/live_preview_*_test.cpp) render on the GPU but do not say so in their names. In Fast they made every shard
# JIT-compile the OptiX modules at once after a device-code change (16 processes x about 2 CPU-minutes, so a "seconds" tier ran for the full timeout).
$gpuAndOversubscribingFilter = "*GPU*:*Gpu*:*gpu*:LivePreview*:WavefrontRenderTest.*:OptixValidationSweepTest.*:BdptFirstRender.*:SppmFirstSlice.*"

# The CPU-render suites (see -Tier Render above), measured in a full serial run (October 2026, seconds in brackets): PbrtBackendAgreementTest [97], ChromaticMediaPerChannelTest [87],
# BundledPbrtLightCoverageTest [72], PbrtReferencePackCpuTest [39], SceneBuilderRenderTest [23], PbrtReferenceTest [10], RenderIntegrationTest [8],
# PbrtExampleSceneTest [6], SceneVolumeRenderTest [6]; FilmRenderTest (every film through the program: the baseline, stills, a video) added later. Parameterized suites carry a prefix ("Bundled/..."), hence the leading *. Anything that also matches the GPU filter stays in Slow.
$renderSuites = "FilmRenderTest.*:*FilmRenderTestParts.*:PbrtBackendAgreementTest.*:ChromaticMediaPerChannelTest.*:*BundledPbrtLightCoverageTest.*:*PbrtReferencePackCpuTest.*:SceneBuilderRenderTest.*:PbrtReferenceTest.*:RenderIntegrationTest.*:*PbrtExampleSceneTest.*:SceneVolumeRenderTest.*"
$renderFilter = "$renderSuites-$gpuAndOversubscribingFilter"
$cheapExclusion = "${gpuAndOversubscribingFilter}:$renderSuites"

# gtest's own --gtest_filter grammar is positivePatterns[-negativePatterns],
# split on the FIRST '-' in the whole string, each side colon-separated.
# Naively string-concatenating "$Filter-$ExcludePattern" breaks the moment
# $Filter already contains a '-' of its own (i.e. the caller is already
# using gtest's negative-filter syntax) - gtest splits on the FIRST dash,
# not the one this code intends, gluing the caller's own last negative
# pattern to this function's first exclusion entry into one pattern that
# can never match a real "Suite.Test" name (no test name contains a
# literal '-'). Confirmed empirically: that glued pattern always destroys
# $gpuAndOversubscribingFilter's own first entry (*GPU*) specifically,
# letting tests named only in that exact casing (e.g.
# RenderIntegrationTest.BasicGPURender) leak back into what should have
# been the exclusion. Splitting $Filter ourselves and re-joining the two
# negative-pattern lists with ':' (rather than gluing strings) composes
# correctly regardless of what $Filter already contains.
function Add-GTestExclusion {
	param([string]$Filter, [string]$ExcludePattern)
	if (-not $Filter) { return "-$ExcludePattern" }
	$dashIndex = $Filter.IndexOf('-')
	if ($dashIndex -ge 0) {
		$positive = $Filter.Substring(0, $dashIndex)
		$existingNegative = $Filter.Substring($dashIndex + 1)
		return "$positive-${existingNegative}:$ExcludePattern"
	}
	return "$Filter-$ExcludePattern"
}

switch ($Tier) {
	"Fast" {
		$Filter = Add-GTestExclusion -Filter $Filter -ExcludePattern $cheapExclusion
	}
	"Render" {
		if ($Filter) {
			Write-Host "[WARN] -Filter is ignored with -Tier Render (gtest can't intersect two positive-pattern lists)." -ForegroundColor Yellow
		}
		$Filter = $renderFilter
		if (-not $PSBoundParameters.ContainsKey("Shards")) { $Shards = $RenderShards }
	}
	"Slow" {
		if ($Filter) {
			Write-Host "[WARN] -Filter is ignored with -Tier Slow (gtest can't intersect two positive-pattern lists) - pass the full pattern via -Filter with -Tier All instead." -ForegroundColor Yellow
		}
		$Filter = $gpuAndOversubscribingFilter
		if ($Shards -ne 1) {
			Write-Host "[INFO] -Tier Slow forces -Shards 1 (these tests contend for the one GPU / spawn full-core pools - sharding them helps nothing)." -ForegroundColor Cyan
			$Shards = 1
		}
	}
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

$testsExe = Join-Path $PSScriptRoot "..\bin\$Configuration\ray_tracer_tests.exe"
if (-not (Test-Path $testsExe)) {
	Write-Host "[FAIL] Tests executable not found: $testsExe" -ForegroundColor Red
	Write-Host "Build it first (see scripts\build_all.ps1 or the full ray_tracer.sln)."
	exit 1
}
$testsExe = (Resolve-Path $testsExe).Path

# This script runs the test program it finds; it does not build it. A program older than the sources it is built from tests code that is no longer there, and "All shards passed"
# then says nothing about the change (a snapshot change went unseen that way on 2026-10-23). The render tests also run the launcher (x64\<Configuration>\ray_tracer.exe), which is
# built separately. Both are compared with the newest source file and a program older than it is reported, here and again beside the result. (A warning, not a failure: a build
# that finds nothing to do leaves its program's time alone, and the newest file may be one that program does not use.)
function Get-NewestSource([string[]]$Dirs) {
	Get-ChildItem -Path ($Dirs | ForEach-Object { Join-Path $repoRoot $_ }) -Recurse -File -Include *.cpp, *.h, *.hpp, *.cu, *.cuh -ErrorAction SilentlyContinue |
		Where-Object { $_.FullName -notmatch '\\(build|generated|_deps)\\' } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
}
$rendererDirs = @("src", "gpu\optix", "launcher", "cpu_renderer")
$staleWarnings = @()
foreach ($check in @(@{ Exe = $testsExe; Dirs = $rendererDirs + @("tests") }, @{ Exe = (Join-Path $repoRoot "x64\$Configuration\ray_tracer.exe"); Dirs = $rendererDirs })) {
	$exe = $check.Exe
	$newestSource = Get-NewestSource $check.Dirs
	if (-not $newestSource -or -not (Test-Path $exe)) { continue }
	$built = (Get-Item $exe).LastWriteTime
	if ($built -lt $newestSource.LastWriteTime) {
		$staleWarnings += "$(Split-Path $exe -Leaf) was built at $built, before $($newestSource.FullName) was changed ($($newestSource.LastWriteTime)): if that change is one it is built from, these results are for the old code - rebuild (ray_tracer.sln)."
	}
}
foreach ($w in $staleWarnings) { Write-Host "[STALE?] $w" -ForegroundColor Yellow }

# A count of the tiers against the whole suite (--gtest_list_tests: cheap, it only enumerates). It checks that the three filters together cover every test
# once; it cannot tell a GPU test that its name does not give away (that one lands in Fast and adds up all the same). That is caught when it runs: the Fast
# shards set RT_FORBID_GPU, and the GPU renderer stops the shard (gpu/optix/optix_renderer_init.cpp).
function Get-GTestCount {
	param([string]$Exe, [string]$Filter)
	$argList = @("--gtest_list_tests")
	if ($Filter) { $argList += "--gtest_filter=$Filter" }
	$output = & $Exe @argList 2>$null
	($output | Select-String -Pattern '^\s\s\S').Count
}

if ($Tier -ne "All") {
	$totalCount = Get-GTestCount -Exe $testsExe -Filter ""
	$fastCount = Get-GTestCount -Exe $testsExe -Filter "-$cheapExclusion"
	$renderCount = Get-GTestCount -Exe $testsExe -Filter $renderFilter
	$slowCount = Get-GTestCount -Exe $testsExe -Filter $gpuAndOversubscribingFilter
	if ($fastCount + $renderCount + $slowCount -ne $totalCount) {
		Write-Host "[FAIL] Tier partition check failed: Fast ($fastCount) + Render ($renderCount) + Slow ($slowCount) = $($fastCount + $renderCount + $slowCount), expected $totalCount total tests." -ForegroundColor Red
		Write-Host "`$gpuAndOversubscribingFilter no longer exactly partitions the suite - most likely a new GPU test's name doesn't follow the Gpu/GPU/gpu convention (see -Tier's own doc comment above) and needs to be added to it explicitly. Fix that before trusting -Tier Fast/Slow results." -ForegroundColor Red
		exit 1
	}
	Write-Host "[OK] Tier partition verified: Fast ($fastCount) + Render ($renderCount) + Slow ($slowCount) = $totalCount total tests" -ForegroundColor Green
}

Write-Host "Running $Shards shards of $testsExe" -ForegroundColor Cyan
if ($Filter) { Write-Host "Filter: $Filter" -ForegroundColor Cyan }

# $PID-namespaced: without this, two concurrent invocations of this script
# (two terminals, overlapping CI runs) would reuse the exact same cwd_N/
# shard_N.log paths and race on them - precisely the cross-process
# collision this script's own per-shard isolation exists to prevent, just
# one level up (between invocations instead of between shards).
# A timestamp as well: two runs from the SAME PowerShell session share a $PID, and the second then found the first run's junctions still in place
# ("Directory cwd_0\.claude cannot be removed because it is not empty").
$logDir = Join-Path $env:TEMP "ray_tracer_test_shards_${PID}_$(Get-Date -Format 'yyyyMMdd_HHmmss')"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

# Computed once and reused for every shard below (was re-enumerated inside
# the loop, redoing the same filesystem walk once per shard for no reason -
# the repo root's own top-level directory listing doesn't change between
# shards).
$repoTopLevelDirs = Get-ChildItem -Path $repoRoot -Directory
$repoTopLevelDocs = Get-ChildItem -Path $repoRoot -File -Filter *.md

$jobs = for ($i = 0; $i -lt $Shards; $i++) {
	$logFile = Join-Path $logDir "shard_$i.log"
	# Several tests write output through a FIXED relative filename (e.g.
	# specparity_B3_spec.ppm) with no per-process uniqueness of their own -
	# fine for a single process, but two shards racing on the SAME filename
	# in the SAME working directory corrupt each other's output and fail
	# with things like "render failed to produce a valid PPM" that have
	# nothing to do with a real bug. Each shard gets its own scratch working
	# directory so relative paths can never collide between shards. Several
	# OTHER tests read fixed INPUT resources by a relative path instead
	# (e.g. pbrt_example_scenes_tests.cpp scans pbrt_scenes/ for its
	# parameterized instantiation list) - an empty scratch directory would
	# make those find nothing and silently register far fewer test
	# instances than the real suite has. Junctioning (not copying) every
	# top-level directory from the real repo root into the scratch
	# directory keeps all such reads resolving exactly as they do from the
	# real root, while a bare-filename WRITE (nothing this project does
	# writes through a subdirectory) still lands fresh in the scratch
	# directory itself, never touching the shared original.
	$shardDir = Join-Path $logDir "cwd_$i"
	New-Item -ItemType Directory -Force -Path $shardDir | Out-Null
	foreach ($dir in $repoTopLevelDirs) {
		New-Item -ItemType Junction -Path (Join-Path $shardDir $dir.Name) -Target $dir.FullName -Force | Out-Null
	}
	# The documents at the top (README.md ...) are copied, not linked (a copy can never be written through): a test that checks what they say read nothing here
	# and skipped without a word until missing files were made failures (RT_REQUIRE_FIXTURES).
	foreach ($doc in $repoTopLevelDocs) { Copy-Item -Path $doc.FullName -Destination $shardDir -Force }
	# Many render/integration tests spawn their OWN worker-thread pool sized
	# to the full logical-core count (determine_render_thread_count(),
	# src/TheRestOfYourLife/thread_count.h) - fine for one process, but N
	# concurrent shards each independently claiming every core oversubscribes
	# the machine and makes the WHOLE run slower than running serially in one
	# process (measured: 2 shards of ~half the suite each took LONGER wall-
	# clock than the full suite in one process). RAY_TRACER_THREADS is that
	# same file's own explicit override - capping each shard to its fair
	# share of the machine (rounded up so the last shard doesn't starve on a
	# core count that doesn't divide evenly) keeps total concurrent threads
	# across all shards close to the real core count instead of Shards-times
	# over it.
	$threadsPerShard = [Math]::Max(1, [Math]::Ceiling([System.Environment]::ProcessorCount / $Shards))
	$machineTemp = if ($env:RT_MACHINE_TEMP) { $env:RT_MACHINE_TEMP } else { $env:TEMP }
	# The test program is started as a process of its own, through cmd only to send its output to the shard's log. (Each shard used to be a Start-Job, a whole PowerShell
	# started for it: sixteen of them took 20 s to start, a third of a Fast run; sixteen plain processes take 3 s.)
	$psi = New-Object System.Diagnostics.ProcessStartInfo
	$psi.FileName = Join-Path $env:SystemRoot "System32\cmd.exe"
	$gtestArgs = @("--gtest_brief=1", "--gtest_output=xml:$(Join-Path $shardDir 'times.xml')")   # (each test's time, for the summary of the slowest shard and tests after the run)
	if ($Filter) { $gtestArgs += "--gtest_filter=$Filter" }
	$quotedArgs = ($gtestArgs | ForEach-Object { '"' + $_ + '"' }) -join ' '
	$psi.Arguments = '/d /s /c ""' + $testsExe + '" ' + $quotedArgs + ' > "' + $logFile + '" 2>&1"'
	$psi.WorkingDirectory = $shardDir
	$psi.UseShellExecute = $false
	$psi.CreateNoWindow = $true
	# Each shard's own TEMP/TMP: about ten test fixtures build a scratch tree under a FIXED name in %TEMP% (pbrt_load_tests, pbrt_discover_tests,
	# ...), and tests of one fixture land in different shards, so two shards used to delete each other's files mid-test ("found.size() 0, expected 2").
	$shardTemp = Join-Path $shardDir "tmp"
	New-Item -ItemType Directory -Force -Path $shardTemp | Out-Null
	$psi.EnvironmentVariables["TEMP"] = $shardTemp
	$psi.EnvironmentVariables["TMP"] = $shardTemp
	# (the machine's own, for what is made to be shared between processes: the film tests run the program with it, so their films use the film cache every run of the
	# program shares - built once - and not a cold one in every shard: tests/unit/film_whole_tests.cpp)
	$psi.EnvironmentVariables["RT_MACHINE_TEMP"] = $machineTemp
	# (and the films the tests build in their own process use that cache too, src/shared/cartoon/film_cache.h: with a cold one in every shard, a frame of the Nomai
	# film took a minute in each shard that drew one)
	$psi.EnvironmentVariables["RAY_TRACER_FILM_CACHE"] = Join-Path $machineTemp "photonsmith_film_cache"
	$psi.EnvironmentVariables["GTEST_TOTAL_SHARDS"] = "$Shards"
	$psi.EnvironmentVariables["GTEST_SHARD_INDEX"] = "$i"
	$psi.EnvironmentVariables["RAY_TRACER_THREADS"] = "$threadsPerShard"
	# The Fast tier runs its shards side by side on the assumption that no test in them uses the GPU: a GPU test that its name does not give away stops its shard
	# (RT_FORBID_GPU, gpu/optix/optix_renderer_init.cpp) instead of running there unnoticed.
	if ($Tier -eq "Fast") { $psi.EnvironmentVariables["RT_FORBID_GPU"] = "1" }
	# The shards run in folders linked to the repository, so every file of it is there: a test that cannot find one fails instead of skipping (tests/unit/fixture_skip.h).
	$psi.EnvironmentVariables["RT_REQUIRE_FIXTURES"] = "1"
	[pscustomobject]@{ Name = "shard_$i"; Process = [System.Diagnostics.Process]::Start($psi); Hung = $false }
}

Write-Host "Waiting for $($jobs.Count) shards (timeout: ${TimeoutSeconds}s)..."
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
foreach ($job in $jobs) {
	$left = [int][Math]::Max(0, ($deadline - (Get-Date)).TotalMilliseconds)
	if (-not $job.Process.WaitForExit($left)) { $job.Hung = $true }
}

# A shard still running at the deadline is stopped with its test program (cmd's child: the whole tree), and said - a deadlocked or hung test (a GPU driver
# stall ...) used to keep the run waiting with no word at all.
foreach ($job in ($jobs | Where-Object { $_.Hung })) {
	Write-Host "[FAIL] $($job.Name): timed out after ${TimeoutSeconds}s - stopping" -ForegroundColor Red
	& taskkill /T /F /PID $job.Process.Id 2>&1 | Out-Null
}

$failedShards = @()
$totalPassed = 0
$totalFailed = 0
$totalSkipped = 0
foreach ($job in $jobs) {
	$logFile = Join-Path $logDir "$($job.Name).log"
	$content = if (Test-Path $logFile) { Get-Content $logFile -Raw } else { "" }
	if ($null -eq $content) { $content = "" }   # (Get-Content -Raw of an empty log is $null: a shard that died before writing anything)
	# The test program's own exit code (cmd /c passes it on): a hard crash (an access violation ...) is not a clean run.
	$exitCode = if ($job.Hung) { $null } else { $job.Process.ExitCode }
	$passedMatch = [regex]::Match($content, '\[\s*PASSED\s*\]\s*(\d+)')
	$failedMatch = [regex]::Match($content, '\[\s*FAILED\s*\]\s*(\d+)')
	$passedCount = if ($passedMatch.Success) { [int]$passedMatch.Groups[1].Value } else { 0 }
	# --gtest_brief=1 omits gtest's own "N FAILED TESTS" summary, so count the per-test "[  FAILED  ] Suite.Test (N ms)" lines too
	# (without this a failing test left the shard "0 failed" and showed up only as an unexplained exit code 1). A failing parameterized test prints
# ", where GetParam() = N" before its timing, which the pattern allows for.
	$failedNames = @([regex]::Matches($content, '(?m)^\[\s*FAILED\s*\]\s+(\S+\.\S+?)(?:, where [^
]*?)?\s+\(\d+ ms\)') | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)
	$failedCount = [Math]::Max($(if ($failedMatch.Success) { [int]$failedMatch.Groups[1].Value } else { 0 }), $failedNames.Count)
	$totalPassed += $passedCount
	$totalFailed += $failedCount
	$skippedMatch = [regex]::Match($content, '\[\s*SKIPPED\s*\]\s*(\d+)\s+test')
	if ($skippedMatch.Success) { $totalSkipped += [int]$skippedMatch.Groups[1].Value }

	# A crash (or a timeout kill above) before gtest ever prints its own
	# summary line looks identical to "0 passed, 0 failed" to the two
	# regexes above - genuinely distinguishing that from a real 0-test shard
	# (e.g. an over-narrow -Filter) needs both the real exit code AND
	# whether a PASSED line ever appeared at all, neither of which the old
	# $job.State/text-search checks here actually caught.
	$crashedOrHung = $job.Hung -or ($null -ne $exitCode -and $exitCode -ne 0) -or (-not $passedMatch.Success)
	if ($crashedOrHung -or $failedCount -gt 0) {
		$failedShards += $job.Name
		$reason = if ($failedCount -gt 0) { "$failedCount test(s) failed: $($failedNames -join ', ')" } elseif ($crashedOrHung) { "crashed/timed out (exit code: $exitCode)" } else { "failed" }
		Write-Host "[FAIL] $($job.Name): $passedCount passed, $failedCount failed - $reason - see $logFile" -ForegroundColor Red
	} else {
		Write-Host "[OK] $($job.Name): $passedCount passed" -ForegroundColor Green
	}
}

foreach ($job in $jobs) { $job.Process.Dispose() }

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Total: $totalPassed passed, $totalFailed failed across $Shards shards" -ForegroundColor Cyan
# (a test that skips itself - no GPU, a scene file or fixture not found - is neither; the count shows when that changes; a shard log's "[  SKIPPED ]" lines say which)
if ($totalSkipped -gt 0) { Write-Host "Skipped: $totalSkipped" -ForegroundColor Yellow }
Write-Host "========================================" -ForegroundColor Cyan
foreach ($w in $staleWarnings) { Write-Host "[STALE?] $w" -ForegroundColor Yellow }

# What set the run's length: the shards are given tests by count, not by time, so the run lasts as long as its slowest shard, and one long test can be most of it
# (a sweep over every film took 150 s of one shard of the Fast tier until it was cut into parts: tests/unit/test_parts.h).
$shardTimes = @()
$testTimes = @()
foreach ($report in (Get-ChildItem -Path $logDir -Filter "times.xml" -Recurse -ErrorAction SilentlyContinue)) {
	try { $xml = [xml](Get-Content $report.FullName -Raw) } catch { continue }
	$shardTimes += [pscustomobject]@{ Name = $report.Directory.Name -replace '^cwd_', 'shard_'; Seconds = [double]$xml.testsuites.time }
	foreach ($case in $xml.SelectNodes("//testcase")) {
		if ($case.status -eq "run") { $testTimes += [pscustomobject]@{ Name = "$($case.classname).$($case.name)"; Seconds = [double]$case.time } }
	}
}
if ($shardTimes.Count -gt 0) {
	$slowest = $shardTimes | Sort-Object Seconds -Descending | Select-Object -First 1
	$median = ($shardTimes | Sort-Object Seconds)[[int][Math]::Floor($shardTimes.Count / 2)].Seconds
	Write-Host ("Slowest shard: {0} {1:N0} s (the median shard {2:N0} s). Slowest tests:" -f $slowest.Name, $slowest.Seconds, $median)
	foreach ($t in ($testTimes | Sort-Object Seconds -Descending | Select-Object -First 5)) { Write-Host ("  {0,6:N1} s  {1}" -f $t.Seconds, $t.Name) }
}

if ($failedShards.Count -gt 0) {
	Write-Host "Failed shards: $($failedShards -join ', ')" -ForegroundColor Red
	Write-Host "Full logs kept in: $logDir"
	exit 1
}

# Every passing run used to leave its scratch directory (logs and junctions) in %TEMP% for good. Junctions are deleted one by one first: a
# recursive Remove-Item on a junction can reach into the directory it points at.
foreach ($shardCwd in (Get-ChildItem -Path $logDir -Directory -Filter "cwd_*")) {
	foreach ($child in (Get-ChildItem -Path $shardCwd.FullName -Force)) {
		if ($child.Attributes -band [IO.FileAttributes]::ReparsePoint) { [System.IO.Directory]::Delete($child.FullName) }
	}
}
Remove-Item -Path $logDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "All shards passed." -ForegroundColor Green
exit 0
