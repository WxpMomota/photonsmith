<#
.SYNOPSIS
    Sets up the optional "act it yourself" helper for films: a video of you acting becomes a character's body and face.

.DESCRIPTION
    Makes a private Python environment with Google's MediaPipe (Apache 2.0), OpenCV and NumPy in the program's per-user data
    folder, and downloads MediaPipe's two models (the pose landmarker, about 30 MB, and the face landmarker, about 4 MB) beside
    the helper's environment. Then

        <folder>\venv\Scripts\python.exe tools\video_to_acting\video_to_acting.py me.mp4 me --models <folder>

    writes me.bvh (the body, for a film's `mocap` line) and me.face (the face, for its `act` line). It runs on this computer;
    nothing is uploaded. About 400 MB of disk. No graphics card is needed.

.PARAMETER Folder
    Where to put the environment and the models (default %LOCALAPPDATA%\RayTracerActing). Keep it short: Windows stops at 260
    characters.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\setup_video_to_acting.ps1
#>
param(
	[string]$Folder = (Join-Path $env:LOCALAPPDATA "RayTracerActing")
)

$ErrorActionPreference = "Stop"

function Find-Python {
	foreach ($candidate in @(@("py", "-3.12"), @("py", "-3.11"), @("py", "-3.10"), @("python"))) {
		try {
			$exe = $candidate[0]
			$extra = @($candidate | Select-Object -Skip 1)
			$ver = & $exe @extra -c "import sys; print('%d.%d' % sys.version_info[:2])" 2>$null
			if ($LASTEXITCODE -eq 0 -and $ver -match '^3\.(10|11|12)$') { return ,@($exe) + $extra }
		} catch { }
	}
	return $null
}

$py = Find-Python
if (-not $py) { throw "Python 3.10, 3.11 or 3.12 was not found on PATH. Install one from python.org and run this again." }

Write-Host "Setting up the acting helper in: $Folder"
New-Item -ItemType Directory -Force -Path $Folder | Out-Null
$venv = Join-Path $Folder "venv"
$python = Join-Path $venv "Scripts\python.exe"
if (-not (Test-Path $python)) {
	Write-Host "Creating the Python environment..."
	& $py[0] @($py | Select-Object -Skip 1) -m venv $venv
	if ($LASTEXITCODE -ne 0) { throw "Could not create the Python environment." }
}
& $python -m pip install --quiet --upgrade pip
Write-Host "Installing MediaPipe, OpenCV and NumPy..."
& $python -m pip install mediapipe opencv-python numpy
if ($LASTEXITCODE -ne 0) { throw "Installing the packages failed." }

$models = @{
	"pose_landmarker_full.task" = "https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_full/float16/latest/pose_landmarker_full.task";
	"face_landmarker.task"      = "https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/latest/face_landmarker.task"
}
foreach ($name in $models.Keys) {
	$target = Join-Path $Folder $name
	if (-not (Test-Path $target)) {
		Write-Host "Downloading $name..."
		Invoke-WebRequest -Uri $models[$name] -OutFile $target
	}
}

Write-Host ""
Write-Host "Done. Film yourself (the whole body in the picture, facing the camera), then:"
Write-Host "  $python tools\video_to_acting\video_to_acting.py me.mp4 me --models $Folder"
Write-Host "and in the film file: mocap NAME me.bvh at T  /  act NAME me.face at T"
