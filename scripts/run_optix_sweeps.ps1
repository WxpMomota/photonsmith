#!/usr/bin/env pwsh
<#
.SYNOPSIS
	Build the renderer and run the consistency sweep against both OptiX backends (recursive and wavefront).
.DESCRIPTION
	scripts/consistency_sweep.py renders hundreds of pairs of scenes that must look alike (a scene moved, scaled, mirrored or turned; a constant sky against the same
	sky as a picture; the CPU against the GPU; ...) and prints the pairs that do not. The Mac cannot run OptiX, so this is how a CPU/OptiX divergence gets caught:
	run it after any change under gpu/optix/, src/shared/ or src/TheRestOfYourLife/ that can change a picture.

	It builds launcher.vcxproj (the CPU and OptiX renderers and ray_tracer.exe), then runs the sweep twice with `--backends cpu,gpu`: once for the recursive OptiX
	backend (the default) and once with RAY_TRACER_WAVEFRONT=1 for the wavefront one. Each run takes about ten minutes, so a full check is about twenty.

	It ends with one line per backend, for example
	    recursive  PASS  522 pairs, 0 off by more than 4%   (9m 40s)
	    wavefront  PASS  522 pairs, 0 off by more than 4%   (10m 02s)
	and exits 0 only when every run finished and found nothing off. The pairs that are off are printed above those lines.
.PARAMETER NoBuild
	Do not build first: use the ray_tracer.exe that is already there.
.PARAMETER Only
	Run only these groups of the sweep (comma separated, as consistency_sweep.py --only takes them, for example "sky,shapeform"). Much faster when you know what you touched.
.PARAMETER Backend
	Which OptiX backend: Both (default), Recursive or Wavefront.
.PARAMETER Exe
	The renderer to test (default x64\Release\ray_tracer.exe in the repository).
.PARAMETER Detail
	Print every pair's numbers, not just the pairs that are off (consistency_sweep.py -v).
.EXAMPLE
	.\scripts\run_optix_sweeps.ps1
	.\scripts\run_optix_sweeps.ps1 -NoBuild -Only sky,shapeform -Backend Wavefront
#>

param(
	[switch]$NoBuild,
	[string]$Only = "",
	[ValidateSet("Both", "Recursive", "Wavefront")]
	[string]$Backend = "Both",
	[string]$Exe = "",
	[switch]$Detail
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo

function Fail([string]$message) {
	Write-Host "[FAIL] $message" -ForegroundColor Red
	exit 2
}

# ---- build -----------------------------------------------------------------------------------------------------------------------------------------------
if (-not $NoBuild) {
	# The CUDA build needs the Visual Studio environment (cl.exe), so the build runs in a cmd that has called vcvars64 first, whatever shell this is.
	$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
	if (-not (Test-Path $vswhere)) { Fail "vswhere.exe not found: is Visual Studio installed?" }
	$vsPath = (& $vswhere -latest -products * -property installationPath | Select-Object -First 1)
	$vcvars = if ($vsPath) { Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat" } else { "" }
	if (-not $vcvars -or -not (Test-Path $vcvars)) { Fail "vcvars64.bat not found (Visual Studio with the C++ workload is needed)." }
	Write-Host "Building launcher (CPU and OptiX renderers)..."
	# (the doubled backslash before the closing quote is what keeps the path's own trailing backslash from escaping it)
	$buildCmd = "call `"$vcvars`" >nul 2>&1 && cd /d `"$repo`" && msbuild launcher\launcher.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=`"$repo\\`" /v:minimal /nologo"
	$sw = [Diagnostics.Stopwatch]::StartNew()
	& cmd.exe /c $buildCmd
	if ($LASTEXITCODE -ne 0) { Fail "the build failed (see above)." }
	Write-Host ("Build done in {0:N0} s." -f $sw.Elapsed.TotalSeconds)
}

# ---- the renderer and python -----------------------------------------------------------------------------------------------------------------------------
if (-not $Exe) { $Exe = Join-Path $repo "x64\Release\ray_tracer.exe" }
if (-not (Test-Path $Exe)) { Fail "$Exe not found: build first (do not pass -NoBuild)." }
$Exe = (Resolve-Path $Exe).Path   # the sweep runs the renderer from the repository root, so a relative path would not be found
$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) { $python = Get-Command py -ErrorAction SilentlyContinue }
if (-not $python) { Fail "python not found on PATH (3.7 or later)." }

# ---- the sweeps ------------------------------------------------------------------------------------------------------------------------------------------
$runs = @()
if ($Backend -in @("Both", "Recursive")) { $runs += @{ Name = "recursive"; Wavefront = $false } }
if ($Backend -in @("Both", "Wavefront")) { $runs += @{ Name = "wavefront"; Wavefront = $true } }

$results = @()
$savedWavefront = $env:RAY_TRACER_WAVEFRONT
try {
	foreach ($run in $runs) {
		Write-Host ""
		Write-Host "=== $($run.Name) OptiX backend ===" -ForegroundColor Cyan
		if ($run.Wavefront) { $env:RAY_TRACER_WAVEFRONT = "1" } else { Remove-Item Env:RAY_TRACER_WAVEFRONT -ErrorAction SilentlyContinue }
		$sweepArgs = @("scripts\consistency_sweep.py", "--exe", $Exe, "--backends", "cpu,gpu")
		if ($Only) { $sweepArgs += @("--only", $Only) }
		if ($Detail) { $sweepArgs += "-v" }
		$sw = [Diagnostics.Stopwatch]::StartNew()
		$lines = @(& $python.Source @sweepArgs)   # the sweep prints at the end, so nothing shows until it is done
		$exit = $LASTEXITCODE
		$lines | ForEach-Object { Write-Host $_ }
		$summary = $lines | Where-Object { $_ -match "^\d+ pairs" } | Select-Object -Last 1
		$results += [pscustomobject]@{ Name = $run.Name; Exit = $exit; Summary = $summary; Seconds = $sw.Elapsed.TotalSeconds }
	}
} finally {
	if ($null -eq $savedWavefront) { Remove-Item Env:RAY_TRACER_WAVEFRONT -ErrorAction SilentlyContinue } else { $env:RAY_TRACER_WAVEFRONT = $savedWavefront }
}

# ---- one line each ---------------------------------------------------------------------------------------------------------------------------------------
Write-Host ""
$bad = 0
foreach ($r in $results) {
	$time = "{0}m {1:00}s" -f [int][math]::Floor($r.Seconds / 60), [int]($r.Seconds % 60)
	if (-not $r.Summary) {
		Write-Host ("{0,-10} ERROR the sweep did not finish (exit code {1}, see above)   ({2})" -f $r.Name, $r.Exit, $time) -ForegroundColor Red
		$bad++
	} elseif ($r.Summary -match ", 0 off" -and $r.Exit -eq 0) {
		Write-Host ("{0,-10} PASS  {1}   ({2})" -f $r.Name, $r.Summary, $time) -ForegroundColor Green
	} else {
		Write-Host ("{0,-10} FAIL  {1}   ({2})" -f $r.Name, $r.Summary, $time) -ForegroundColor Red
		$bad++
	}
}
exit ([int]($bad -gt 0))
