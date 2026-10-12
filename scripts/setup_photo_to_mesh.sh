#!/usr/bin/env bash
# Sets up the optional "Object from a photo" helper for the Scene Builder on macOS (and Linux): the counterpart of setup_photo_to_mesh.ps1.
#
# Makes a private Python environment (PyTorch, the open TripoSR model, a background remover) in the program's per-user data folder, so the Scene
# Builder's Add > "Object from a photo..." can turn one photo into a 3D object on this computer. Nothing is uploaded. About 5 GB of disk and a
# download of about the same, once; the model weights (about 1.7 GB) come from Hugging Face.
#
# Needs Python 3.10, 3.11 or 3.12 and git. Without a suitable Python a Mac fetches its own private copy of 3.12 (about 25 MB); or install one with
# `brew install python@3.12` or the installer from python.org. git comes with the Xcode command line tools: `xcode-select --install`. Apple-silicon Macs use the Apple GPU through PyTorch's MPS backend (several minutes per photo become tens of
# seconds), falling back to the processor if MPS cannot run something. Intel Macs get PyTorch 2.2.2, the last version built for them, and run on the processor.
#
#   scripts/setup_photo_to_mesh.sh [FOLDER]
#
# FOLDER defaults to ~/Library/Application Support/RayTracerPhoto (the folder the Scene Builder looks in; change it only together with the
# RAY_TRACER_PHOTO3D_PYTHON environment variable).
set -euo pipefail

# The TripoSR commit this was built and tested against.
TRIPOSR_COMMIT="107cefdc244c39106fa830359024f6a2f1c78871"

if [[ "$(uname -s)" == "Darwin" ]]; then
	DEFAULT_FOLDER="$HOME/Library/Application Support/RayTracerPhoto"
else
	DEFAULT_FOLDER="${XDG_DATA_HOME:-$HOME/.local/share}/RayTracerPhoto"
fi
FOLDER="${1:-$DEFAULT_FOLDER}"

# A program started from Finder has a minimal PATH, without Homebrew or python.org's folders: add the usual places.
export PATH="/opt/homebrew/bin:/usr/local/bin:/Library/Frameworks/Python.framework/Versions/3.12/bin:/Library/Frameworks/Python.framework/Versions/3.11/bin:/Library/Frameworks/Python.framework/Versions/3.10/bin:$PATH"

find_python() {
	local candidate version found
	for candidate in python3.12 python3.11 python3.10 python3; do
		if found="$(command -v "$candidate" 2>/dev/null)"; then
			version="$("$candidate" -c "import sys; print('%d.%d' % sys.version_info[:2])" 2>/dev/null || true)"
			case "$version" in
				3.10|3.11|3.12) echo "Found $candidate at $found: Python $version." >&2; echo "$found"; return 0 ;;
				*) echo "Skipped $candidate at $found: Python ${version:-?} (3.10 to 3.12 is needed)." >&2 ;;
			esac
		else
			echo "No $candidate on the PATH." >&2
		fi
	done
	return 1
}

# No suitable Python (a Mac often has none, or only the old one that comes with Xcode): fetch a private CPython 3.12 from astral-sh/python-build-standalone,
# a relocatable build, into the folder. Checked against the release's own SHA256SUMS. Nothing is installed system-wide and no administrator is needed.
fetch_python() {
	[[ "$(uname -s)" == "Darwin" ]] || return 1
	local arch dest="$FOLDER/python" release tmp url name sums want got
	case "$(uname -m)" in arm64) arch=aarch64 ;; x86_64) arch=x86_64 ;; *) return 1 ;; esac
	if [[ -x "$dest/bin/python3.12" ]]; then PY="$dest/bin/python3.12"; echo "Using the Python 3.12 already fetched in $dest."; return 0; fi
	echo "No usable Python on this computer: downloading a private Python 3.12 (about 25 MB, from github.com/astral-sh/python-build-standalone)..."
	tmp="$(mktemp -d)"
	release="$(curl -fsSL https://api.github.com/repos/astral-sh/python-build-standalone/releases/latest)" || { echo "Could not reach GitHub to look up the Python build." >&2; rm -rf "$tmp"; return 1; }
	url="$(printf '%s' "$release" | grep -o "https://[^\"]*/cpython-3\.12\.[0-9]*\(+\|%2B\)[0-9]*-$arch-apple-darwin-install_only\.tar\.gz" | head -1)"
	sums="$(printf '%s' "$release" | grep -o 'https://[^"]*/SHA256SUMS' | head -1)"
	if [[ -z "$url" || -z "$sums" ]]; then echo "The release has no Python 3.12 build for $arch." >&2; rm -rf "$tmp"; return 1; fi
	name="$(basename "$url" | sed 's/%2B/+/')"   # the API writes the "+" of the file name as %2B; the checksum list has it plain
	echo "Downloading $name"
	curl -fsSL "$url" -o "$tmp/$name" || { echo "Downloading Python failed." >&2; rm -rf "$tmp"; return 1; }
	# the file name has a "+" that the checksum list writes as is; match it literally
	want="$(curl -fsSL "$sums" | grep -F "$name" | awk '{print $1}' | head -1)"
	got="$(shasum -a 256 "$tmp/$name" | awk '{print $1}')"
	if [[ -z "$want" || "$want" != "$got" ]]; then echo "The downloaded Python does not match its published checksum (wanted ${want:-none}, got $got); not using it." >&2; rm -rf "$tmp"; return 1; fi
	echo "Checksum ok ($got)."
	rm -rf "$dest.partial"; mkdir -p "$dest.partial"
	tar -xzf "$tmp/$name" -C "$dest.partial" --strip-components=1 || { echo "Could not unpack Python." >&2; rm -rf "$tmp" "$dest.partial"; return 1; }
	rm -rf "$tmp" "$dest"; mv "$dest.partial" "$dest"
	[[ -x "$dest/bin/python3.12" ]] || { echo "The unpacked Python has no python3.12." >&2; return 1; }
	PY="$dest/bin/python3.12"
}

PY="$(find_python || true)"
if [[ -z "$PY" ]]; then
	mkdir -p "$FOLDER"
	fetch_python || { echo "Python 3.10, 3.11 or 3.12 was not found and a private copy could not be fetched. Install one (macOS: brew install python@3.12, or the installer from python.org) and run this again." >&2; exit 1; }
fi
command -v git >/dev/null 2>&1 || { echo "git was not found (it fetches the TripoSR code). On a Mac: xcode-select --install" >&2; exit 1; }

echo "Setting up the photo helper in: $FOLDER"
echo "Using Python: $PY ($("$PY" --version 2>&1))"
mkdir -p "$FOLDER"
VENV="$FOLDER/venv"
PYTHON="$VENV/bin/python"

if [[ ! -x "$PYTHON" ]]; then
	echo "Creating the Python environment..."
	"$PY" -m venv "$VENV" || { echo "Could not create the Python environment." >&2; exit 1; }
fi
"$PYTHON" -m pip install --quiet --upgrade pip

ARCH="$("$PYTHON" -c "import platform; print(platform.machine())")"
EXTRA_PINS=()
if [[ "$(uname -s)" == "Darwin" && "$ARCH" == "x86_64" ]]; then
	# PyTorch stopped publishing Intel-Mac wheels after 2.2.2, which needs NumPy 1.x.
	echo "Installing PyTorch 2.2.2 (the last build for Intel Macs; it runs on the processor)..."
	"$PYTHON" -m pip install "torch==2.2.2" "torchvision==0.17.2" "numpy<2" || { echo "Installing PyTorch failed." >&2; exit 1; }
	EXTRA_PINS=("numpy<2")
else
	echo "Installing PyTorch (about 1 GB)..."
	"$PYTHON" -m pip install torch torchvision || { echo "Installing PyTorch failed." >&2; exit 1; }
fi

echo "Installing the other packages..."
"$PYTHON" -m pip install omegaconf einops "transformers>=4.40,<5" trimesh "rembg[cpu]" huggingface-hub xatlas scikit-image scipy pillow numpy onnxruntime "${EXTRA_PINS[@]+"${EXTRA_PINS[@]}"}" \
	|| { echo "Installing the packages failed." >&2; exit 1; }

TRIPOSR="$FOLDER/TripoSR"
if [[ ! -d "$TRIPOSR/tsr" ]]; then
	echo "Fetching the TripoSR code..."
	git clone --quiet https://github.com/VAST-AI-Research/TripoSR "$TRIPOSR" || { echo "Could not fetch the TripoSR code." >&2; exit 1; }
fi
git -C "$TRIPOSR" checkout --quiet "$TRIPOSR_COMMIT" || { echo "Could not check out the tested TripoSR version." >&2; exit 1; }

echo "Downloading the model weights (about 1.7 GB)..."
"$PYTHON" -c "from huggingface_hub import hf_hub_download as d; [d('stabilityai/TripoSR', f) for f in ('config.yaml', 'model.ckpt')]" \
	|| { echo "Could not download the model weights." >&2; exit 1; }

echo "Downloading the background-removal model (U2-Net, about 176 MB)..."
"$PYTHON" -c "import rembg; rembg.new_session('u2net')" || { echo "Could not download the background-removal model." >&2; exit 1; }

echo
echo 'Done. In the Scene Builder choose Add > Object from a photo...'
echo "Diagnostics (the Diagnostics tab) lists what this helper has and what is missing."
if [[ "$(uname -s)" == "Darwin" && "$ARCH" == "x86_64" ]]; then echo "(An Intel Mac has no GPU path here: expect several minutes per photo.)"; fi
