#pragma once
// cartoon_audio.h -- sound for the cartoon films, made from nothing: a few instruments (a marimba-like mallet, a glockenspiel, a string pluck, a pad, a bass), the noises a cartoon needs
// (a splash, a knock on wood, a boing, a whoosh, a twinkle), a small voice for a character that chirps instead of talking, and a mixer with a reverb and a limiter. A film's soundtrack is
// a function that places these in time on a `Mix`; the launcher renders it to a .wav and adds it to the video (cartoon_render.h).
//
// Everything is a pure function of its arguments (the noise is a fixed-seed generator), so a film sounds the same every time it is rendered and a test can check it.
//
// A sound is first made on its own as a list of mono samples (`std::vector<float>`), then `Mix::put` adds it at a time, with a gain, a pan (-1 left, +1 right) and how much of it goes to
// the reverb. `Mix::finish` runs the reverb, softly limits the sum and returns 16-bit stereo.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <fstream>
#include <string>
#include <vector>

namespace cartoon {
namespace audio {

constexpr int kRate = 44100;
constexpr double kTau = 6.283185307179586;

// The frequency of a MIDI note number (69 = A4 = 440 Hz; 60 = middle C).
inline double hz(double midiNote) { return 440.0 * std::pow(2.0, (midiNote - 69.0) / 12.0); }

// A fixed-seed noise source: the same numbers every run.
// A seed mixed through an integer hash (lowbias32): neighbouring seeds give unrelated numbers. (Through the LCG alone they did not: unit(s + 1) - unit(s) was the same for
// every s, so a run of seeds 1, 2, 3 made a bird's notes climb like a ladder and footsteps rise in pitch.)
inline unsigned mixSeed(unsigned x) {
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}
struct Noise {
	unsigned s;
	explicit Noise(unsigned seed) : s(mixSeed(seed * 2654435761u + 12345u)) {}
	double next() {   // -1 .. 1
		s = s * 1664525u + 1013904223u;
		return static_cast<double>((s >> 8) & 0xFFFF) / 32768.0 - 1.0;
	}
};
// A number from 0 to 1 that depends only on `seed`: a score's choices (which note, how loud) that must be the same every time the film is made.
inline double unit(unsigned seed) {
	Noise n(seed);
	n.next();
	return 0.5 * (n.next() + 1.0);
}

inline std::size_t samplesFor(double seconds) { return static_cast<std::size_t>(std::max(0.0, seconds) * kRate) + 1; }

// A short linear fade at both ends of a sound, so that no sound clicks where it starts or stops.
inline void edges(std::vector<float> &v, double attack = 0.003, double release = 0.02) {
	const std::size_t a = std::min(v.size() / 2, samplesFor(attack)), r = std::min(v.size() / 2, samplesFor(release));
	for (std::size_t i = 0; i < a; ++i) v[i] *= static_cast<float>(static_cast<double>(i) / a);
	for (std::size_t i = 0; i < r; ++i) v[v.size() - 1 - i] *= static_cast<float>(static_cast<double>(i) / r);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Instruments
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
struct Partial {
	double mult, amp, decay;   // frequency as a multiple of the note, loudness, and how fast it dies away (per second, as in exp(-decay t))
};

inline std::vector<float> additive(double f, const std::vector<Partial> &parts, double length, double attack = 0.004) {
	std::vector<float> v(samplesFor(length), 0.0f);
	for (const Partial &p : parts) {
		const double w = kTau * f * p.mult;
		if (f * p.mult > kRate * 0.45) continue;
		for (std::size_t i = 0; i < v.size(); ++i) {
			const double t = static_cast<double>(i) / kRate;
			v[i] += static_cast<float>(p.amp * std::sin(w * t) * std::exp(-p.decay * t) * std::min(1.0, t / attack));
		}
	}
	edges(v, 0.001, 0.03);
	return v;
}

// A wooden mallet on a bar (a marimba): a round tone and a short bright overtone.
inline std::vector<float> mallet(double f, double length = 1.2) { return additive(f, {{1.0, 1.0, 3.2}, {4.0, 0.30, 9.0}, {9.9, 0.06, 22.0}}, length); }
// A glockenspiel or a music box: the inharmonic overtones of a small metal bar.
inline std::vector<float> bell(double f, double length = 2.4) { return additive(f, {{1.0, 1.0, 1.6}, {2.756, 0.45, 2.6}, {5.404, 0.22, 3.6}, {8.933, 0.10, 5.5}}, length, 0.002); }
// A low round note.
inline std::vector<float> bass(double f, double length = 1.6) { return additive(f, {{1.0, 1.0, 1.7}, {2.0, 0.30, 3.0}, {3.0, 0.08, 5.0}}, length, 0.01); }

// A soft sustained tone: three slightly detuned voices with a few overtones, rising slowly and fading slowly. `length` is the held part; the tail adds the release.
inline std::vector<float> pad(double f, double length, double attack = 0.7, double release = 1.2) {
	const double total = length + release;
	std::vector<float> v(samplesFor(total), 0.0f);
	const double detune[3] = {0.997, 1.0, 1.0035};
	for (int d = 0; d < 3; ++d) {
		for (int k = 1; k <= 4; ++k) {
			const double fk = f * detune[d] * k;
			if (fk > kRate * 0.45) break;
			const double amp = 0.33 / (k * k);
			const double w = kTau * fk;
			for (std::size_t i = 0; i < v.size(); ++i) {
				const double t = static_cast<double>(i) / kRate;
				const double env = std::min(1.0, t / attack) * (t < length ? 1.0 : std::max(0.0, 1.0 - (t - length) / release));
				v[i] += static_cast<float>(amp * std::sin(w * t + d) * env);
			}
		}
	}
	return v;
}

// A plucked string (Karplus-Strong): a burst of noise circulating in a delay line that is a little duller each time round.
// The loop's period is the whole samples of the line, half a sample of the averaging and the fraction left over, taken by an allpass filter (Jaffe and Smith): in tune
// (a whole-sample loop is up to 15 cents off at the top of the treble, and beats against a body tuned exactly).
inline std::vector<float> pluck(double f, double length = 1.6, unsigned seed = 1, double damping = 0.996) {
	const double period = kRate / std::max(1.0, f);
	std::size_t n = static_cast<std::size_t>(std::max(2.0, std::floor(period - 0.5)));
	double frac = period - 0.5 - static_cast<double>(n);
	if (frac < 0.1 && n > 2) --n, frac += 1.0;   // (an allpass delay near 0 rings: keep it between 0.1 and 1.1)
	frac = std::clamp(frac, 0.1, 1.1);
	const double c = (1.0 - frac) / (1.0 + frac);
	std::vector<double> line(n);
	Noise noise(seed);
	for (double &x : line) x = noise.next() * 0.8;
	std::vector<float> v(samplesFor(length), 0.0f);
	std::size_t pos = 0;
	double previous = 0.0, inPast = 0.0, outPast = 0.0;
	for (std::size_t i = 0; i < v.size(); ++i) {
		const double cur = line[pos];
		v[i] = static_cast<float>(cur);
		const double averaged = damping * 0.5 * (cur + previous);
		previous = cur;
		const double passed = c * averaged + inPast - c * outPast;
		inPast = averaged, outPast = passed;
		line[pos] = passed;
		pos = (pos + 1) % n;
	}
	edges(v, 0.001, 0.05);
	return v;
}

// A small voice: a tone that glides from f0 to f1 over `length` seconds with a little vibrato, a round overtone and a quick rise and fall. For a character that chirps.
inline std::vector<float> chirp(double f0, double f1, double length, double vibratoHz = 6.0, double vibrato = 0.02, double volume = 1.0) {
	std::vector<float> v(samplesFor(length), 0.0f);
	double phase = 0.0;
	for (std::size_t i = 0; i < v.size(); ++i) {
		const double t = static_cast<double>(i) / kRate, u = std::min(1.0, t / std::max(1e-6, length));
		const double s = u * u * (3.0 - 2.0 * u);
		const double f = (f0 + (f1 - f0) * s) * (1.0 + vibrato * std::sin(kTau * vibratoHz * t) * std::min(1.0, t * 8.0));
		phase += kTau * f / kRate;
		const double env = std::min(1.0, t / 0.012) * std::min(1.0, (length - t) / 0.05);
		v[i] = static_cast<float>(volume * env * (std::sin(phase) + 0.32 * std::sin(2.0 * phase) + 0.12 * std::sin(3.0 * phase)));
	}
	return v;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Noises
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Filtered noise: a rise over `attack` seconds, then a fall at `decay` per second; its brightness moves from `lowpass0` to `lowpass1` Hz. A splash, a whoosh, wind, rustling.
inline std::vector<float> noiseBurst(double length, double attack, double decay, double lowpass0, double lowpass1, unsigned seed = 7) {
	std::vector<float> v(samplesFor(length), 0.0f);
	Noise noise(seed);
	double y = 0.0;
	for (std::size_t i = 0; i < v.size(); ++i) {
		const double t = static_cast<double>(i) / kRate, u = std::min(1.0, t / std::max(1e-6, length));
		const double fc = lowpass0 + (lowpass1 - lowpass0) * u;
		const double a = 1.0 - std::exp(-kTau * fc / kRate);
		y += a * (noise.next() - y);
		const double env = std::min(1.0, t / std::max(1e-6, attack)) * std::exp(-decay * t);
		v[i] = static_cast<float>(y * env * 1.8);
	}
	edges(v, 0.0005, 0.02);
	return v;
}

// A knock on wood: a low tone that drops a little, and a short click.
inline std::vector<float> knock(double f = 160.0, double length = 0.22, unsigned seed = 3) {
	std::vector<float> v(samplesFor(length), 0.0f);
	Noise noise(seed);
	double phase = 0.0, y = 0.0;
	for (std::size_t i = 0; i < v.size(); ++i) {
		const double t = static_cast<double>(i) / kRate;
		phase += kTau * f * (1.0 - 0.35 * std::min(1.0, t * 14.0)) / kRate;
		y += 0.35 * (noise.next() - y);
		v[i] = static_cast<float>(std::sin(phase) * std::exp(-t * 26.0) + 0.5 * y * std::exp(-t * 90.0));
	}
	edges(v, 0.0003, 0.01);
	return v;
}

// A rubbery boing: a tone that wobbles in pitch round f and dies away.
inline std::vector<float> boing(double f = 220.0, double length = 0.6) {
	std::vector<float> v(samplesFor(length), 0.0f);
	double phase = 0.0;
	for (std::size_t i = 0; i < v.size(); ++i) {
		const double t = static_cast<double>(i) / kRate;
		phase += kTau * f * (1.0 + 0.55 * std::exp(-t * 6.0) * std::sin(kTau * 11.0 * t) + 0.5 * std::exp(-t * 9.0)) / kRate;
		v[i] = static_cast<float>((std::sin(phase) + 0.4 * std::sin(2.0 * phase)) * std::exp(-t * 6.5) * std::min(1.0, t / 0.004));
	}
	edges(v, 0.0005, 0.02);
	return v;
}

// A glide: a tone sliding from f0 to f1 and fading (a swoosh of a falling object, a rising "whee").
inline std::vector<float> slide(double f0, double f1, double length, double decay = 3.0) {
	std::vector<float> v(samplesFor(length), 0.0f);
	double phase = 0.0;
	for (std::size_t i = 0; i < v.size(); ++i) {
		const double t = static_cast<double>(i) / kRate, u = t / length;
		phase += kTau * (f0 * std::pow(f1 / f0, u)) / kRate;
		v[i] = static_cast<float>(std::sin(phase) * std::exp(-decay * t) * std::min(1.0, t / 0.01));
	}
	edges(v, 0.001, 0.03);
	return v;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The mixer
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
class Reverb {
  public:
	explicit Reverb(int spread) {
		static const int combs[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
		static const int allpasses[4] = {556, 441, 341, 225};
		for (int i = 0; i < 8; ++i) comb_[i].assign(static_cast<std::size_t>(combs[i] + spread), 0.0f);
		for (int i = 0; i < 4; ++i) all_[i].assign(static_cast<std::size_t>(allpasses[i] + spread), 0.0f);
	}
	float process(float in) {
		const float x = in * 0.015f;
		float sum = 0.0f;
		for (int i = 0; i < 8; ++i) {
			std::vector<float> &b = comb_[i];
			const float out = b[cpos_[i]];
			lp_[i] = out * (1.0f - damp_) + lp_[i] * damp_;
			b[cpos_[i]] = x + lp_[i] * feedback_;
			cpos_[i] = (cpos_[i] + 1) % b.size();
			sum += out;
		}
		for (int i = 0; i < 4; ++i) {
			std::vector<float> &b = all_[i];
			const float buffered = b[apos_[i]];
			const float out = -sum + buffered;
			b[apos_[i]] = sum + buffered * 0.5f;
			apos_[i] = (apos_[i] + 1) % b.size();
			sum = out;
		}
		return sum;
	}

  private:
	std::vector<float> comb_[8], all_[4];
	std::size_t cpos_[8] = {}, apos_[4] = {};
	float lp_[8] = {};
	float feedback_ = 0.86f, damp_ = 0.25f;
};

// A biquad filter, one channel; the two of BS.1770's K-weighting made for our rate the way libebur128 makes them (the standard gives them at 48 kHz only).
struct Biquad {
	double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
	static Biquad kShelf() {   // (the head's effect: up 4 dB above about 1.7 kHz)
		const double K = std::tan(kTau / 2.0 * 1681.974450955533 / kRate), Q = 0.7071752369554196, Vh = std::pow(10.0, 3.999843853973347 / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
		const double a0 = 1.0 + K / Q + K * K;
		Biquad f;
		f.b0 = (Vh + Vb * K / Q + K * K) / a0, f.b1 = 2.0 * (K * K - Vh) / a0, f.b2 = (Vh - Vb * K / Q + K * K) / a0;
		f.a1 = 2.0 * (K * K - 1.0) / a0, f.a2 = (1.0 - K / Q + K * K) / a0;
		return f;
	}
	static Biquad kHighPass() {   // (RLB: the lowest notes count for little)
		const double K = std::tan(kTau / 2.0 * 38.13547087602444 / kRate), Q = 0.5003270373238773, a0 = 1.0 + K / Q + K * K;
		Biquad f;
		f.b0 = 1.0, f.b1 = -2.0, f.b2 = 1.0, f.a1 = 2.0 * (K * K - 1.0) / a0, f.a2 = (1.0 - K / Q + K * K) / a0;
		return f;
	}
	double operator()(double x) {
		const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
		x2 = x1, x1 = x, y2 = y1, y1 = y;
		return y;
	}
};
// How loud a stereo mix is, in LUFS (ITU-R BS.1770: K-weighted - a shelf up 4 dB above 1.7 kHz and a high-pass at 38 Hz, as the ear weighs it - in 400 ms blocks a
// tenth of a second apart, those under -70 LUFS and then those 10 LU under the rest's mean left out). -100 for silence.
inline double loudness(const std::vector<float> &left, const std::vector<float> &right) {
	const std::size_t n = std::min(left.size(), right.size()), block = static_cast<std::size_t>(0.4 * kRate), hop = block / 4;
	std::vector<double> sq(n, 0.0);
	for (int c = 0; c < 2; ++c) {
		Biquad shelf = Biquad::kShelf(), pass = Biquad::kHighPass();
		const std::vector<float> &x = c == 0 ? left : right;
		for (std::size_t i = 0; i < n; ++i) {
			const double y = pass(shelf(x[i]));
			sq[i] += y * y;
		}
	}
	std::vector<double> blocks;
	double sum = 0.0;
	std::size_t i = 0;
	for (; i < n; ++i) {   // (a running sum over each block)
		sum += sq[i];
		if (i + 1 >= block && (i + 1 - block) % hop == 0) {
			blocks.push_back(sum / block);
			sum = 0.0;
			for (std::size_t k = i + 1 - block + hop; k <= i; ++k) sum += sq[k];   // (start the next block's sum from its overlap with this one)
		}
	}
	const auto lufs = [](double meanSquare) { return meanSquare > 0.0 ? -0.691 + 10.0 * std::log10(meanSquare) : -100.0; };
	double kept = 0.0;
	int count = 0;
	for (double b : blocks)
		if (lufs(b) > -70.0) kept += b, ++count;
	if (count == 0) return -100.0;
	const double gate = lufs(kept / count) - 10.0;
	kept = 0.0, count = 0;
	for (double b : blocks)
		if (lufs(b) > -70.0 && lufs(b) > gate) kept += b, ++count;
	return count ? lufs(kept / count) : -100.0;
}
// A look-ahead limiter: the gain falls smoothly over the 5 ms before a peak that would pass `ceiling`, just enough to keep it under, and recovers over a quarter of a second -
// the loudest moments brought down without the distortion of clipping them.
inline void limit(std::vector<float> &left, std::vector<float> &right, double ceiling) {
	const std::size_t n = std::min(left.size(), right.size());
	if (n == 0) return;
	std::vector<float> gain(n);
	for (std::size_t i = 0; i < n; ++i) {
		const double p = std::max(std::abs(left[i]), std::abs(right[i]));
		gain[i] = static_cast<float>(p > ceiling ? ceiling / p : 1.0);
	}
	const double attack = 1.0 / (0.005 * kRate), release = 1.0 - std::exp(-1.0 / (0.25 * kRate));
	for (std::size_t i = n - 1; i-- > 0;) gain[i] = std::min(gain[i], static_cast<float>(gain[i + 1] + attack));   // (down ahead of a peak)
	double g = gain[0];
	for (std::size_t i = 0; i < n; ++i) {
		g = std::min(static_cast<double>(gain[i]), g + (1.0 - g) * release);   // (back up slowly after it)
		left[i] = static_cast<float>(left[i] * g);
		right[i] = static_cast<float>(right[i] * g);
	}
}

class Mix {
  public:
	// A mix of `seconds` seconds. (The buffers run on a few seconds past the end so that a sound placed near the end is not cut off; finish() returns exactly `seconds`.)
	explicit Mix(double seconds) : seconds_(seconds) {
		const std::size_t n = samplesFor(seconds + 4.0);
		for (int c = 0; c < 2; ++c) {
			dry_[c].assign(n, 0.0f);
			wet_[c].assign(n, 0.0f);
		}
	}
	double seconds() const { return seconds_; }

	// Adds a sound at `t` seconds. `pan` -1 (left) to 1 (right); `send` is how much of it also goes to the reverb (0 dry, 1 as loud again as the dry).
	void put(double t, const std::vector<float> &sound, double gain, double pan = 0.0, double send = 0.2) {
		if (t < 0.0) return;
		const std::size_t start = static_cast<std::size_t>(t * kRate);
		const double angle = (std::max(-1.0, std::min(1.0, pan)) + 1.0) * kTau / 8.0;
		const float g[2] = {static_cast<float>(gain * std::cos(angle)), static_cast<float>(gain * std::sin(angle))};
		for (std::size_t i = 0; i < sound.size() && start + i < dry_[0].size(); ++i) {
			for (int c = 0; c < 2; ++c) {
				dry_[c][start + i] += sound[i] * g[c];
				wet_[c][start + i] += sound[i] * g[c] * static_cast<float>(send);
			}
		}
	}

	// Another mix laid on this one, its level `gain` (one value each 1/100 s, eased between them; past its end the last): music under dialogue, ducked.
	void add(const Mix &other, const std::vector<float> &gain) {
		const std::size_t hop = static_cast<std::size_t>(kRate / 100);
		for (std::size_t i = 0; i < dry_[0].size() && i < other.dry_[0].size(); ++i) {
			const std::size_t k = i / hop;
			const double u = static_cast<double>(i % hop) / hop;
			const double g0 = gain.empty() ? 1.0 : gain[std::min(k, gain.size() - 1)], g1 = gain.empty() ? 1.0 : gain[std::min(k + 1, gain.size() - 1)];
			const float g = static_cast<float>(g0 + (g1 - g0) * u);
			for (int c = 0; c < 2; ++c) {
				dry_[c][i] += other.dry_[c][i] * g;
				wet_[c][i] += other.wet_[c][i] * g;
			}
		}
	}

	// Another mix laid on this one from `t` seconds (a part of a film on the whole film's mix): faded in over `fadeIn` seconds from its start and out over `fadeOut` before its
	// own end, and nothing of it after that end (so one part's sound gives way to the next's at the cut between them).
	void addAt(const Mix &other, double t, double fadeIn = 0.0, double fadeOut = 0.0) {
		if (t < 0.0) return;
		const std::size_t start = static_cast<std::size_t>(t * kRate), length = samplesFor(other.seconds_) - 1;
		const double in = std::max(1.0, fadeIn * kRate), out = std::max(1.0, fadeOut * kRate);
		for (std::size_t i = 0; i < length && i < other.dry_[0].size() && start + i < dry_[0].size(); ++i) {
			const double u = static_cast<double>(i), left = static_cast<double>(length - i);
			const float g = static_cast<float>((fadeIn > 0.0 ? std::min(1.0, u / in) : 1.0) * (fadeOut > 0.0 ? std::min(1.0, left / out) : 1.0));
			for (int c = 0; c < 2; ++c) {
				dry_[c][start + i] += other.dry_[c][i] * g;
				wet_[c][start + i] += other.wet_[c][i] * g;
			}
		}
	}

	// The reverb; brought to a film's loudness (`targetLufs`, BS.1770: a video's soundtrack is heard at about -16 LUFS, and one made quieter is not turned up by the
	// player); a fade in and out; a look-ahead limiter that keeps every peak under `headroom` without the distortion of clipping; 16-bit stereo, interleaved, exactly
	// `seconds` long. (`targetLufs` of 0 or more: leave the level as it is.)
	std::vector<std::int16_t> finish(double fadeIn = 0.5, double fadeOut = 2.0, double headroom = 0.89, double targetLufs = -16.0) const {
		const std::size_t n = samplesFor(seconds_) - 1;
		std::vector<float> out[2];
		for (int c = 0; c < 2; ++c) {
			Reverb room(c == 0 ? 0 : 23);
			out[c].resize(n);
			for (std::size_t i = 0; i < n; ++i) out[c][i] = dry_[c][i] + room.process(wet_[c][i]) * 2.5f;
		}
		const double loud = loudness(out[0], out[1]);
		const double level = targetLufs < 0.0 && loud > -70.0 ? std::clamp(std::pow(10.0, (targetLufs - loud) / 20.0), 0.25, 10.0) : 1.0;   // (at most 20 dB up: a silent mix stays silent)
		for (int c = 0; c < 2; ++c)
			for (std::size_t i = 0; i < n; ++i) {
				const double t = static_cast<double>(i) / kRate;
				const double fade = std::min(1.0, t / std::max(1e-6, fadeIn)) * std::min(1.0, (seconds_ - t) / std::max(1e-6, fadeOut));
				out[c][i] = static_cast<float>(out[c][i] * level * fade);
			}
		limit(out[0], out[1], headroom);
		double peak = 1e-9;
		for (int c = 0; c < 2; ++c)
			for (std::size_t i = 0; i < n; ++i) peak = std::max(peak, static_cast<double>(std::abs(out[c][i])));
		const double scale = peak > headroom ? headroom / peak : 1.0;
		std::vector<std::int16_t> pcm(n * 2);
		for (std::size_t i = 0; i < n; ++i)
			for (int c = 0; c < 2; ++c) pcm[2 * i + static_cast<std::size_t>(c)] = static_cast<std::int16_t>(std::lround(out[c][i] * scale * 32767.0));
		return pcm;
	}

  private:
	double seconds_;
	std::vector<float> dry_[2], wet_[2];
};

// A finished mix (16-bit stereo, interleaved) made `frames` sample frames long at the same pitch: the soundtrack of a video of a whole film made at another length than the film's own
// (its frames spread over more or fewer seconds: another frame count, --speed), in step with its pictures. Windows of 40 ms overlap by half, each taken from near where the time
// scale puts it, at the place (within 10 ms) that best continues the window before it (WSOLA), so a held note neither warbles nor changes its pitch. Within 0.1% of the length the
// sound is only cut or padded with silence.
inline std::vector<std::int16_t> stretched(const std::vector<std::int16_t> &pcm, std::size_t frames) {
	const std::size_t in = pcm.size() / 2;
	if (in == 0 || frames == 0) return std::vector<std::int16_t>(frames * 2, 0);
	if (std::abs(static_cast<double>(frames) - static_cast<double>(in)) <= 0.001 * static_cast<double>(in)) {
		std::vector<std::int16_t> out(pcm.begin(), pcm.begin() + static_cast<std::ptrdiff_t>(std::min(in, frames) * 2));
		out.resize(frames * 2, 0);
		return out;
	}
	const std::size_t N = static_cast<std::size_t>(kRate / 25), hop = N / 2, reach = static_cast<std::size_t>(kRate / 100);
	const double step = static_cast<double>(hop) * static_cast<double>(in) / static_cast<double>(frames);   // (the input moved on for each output hop)
	std::vector<float> mono(in);
	for (std::size_t i = 0; i < in; ++i) mono[i] = 0.5f * (static_cast<float>(pcm[2 * i]) + static_cast<float>(pcm[2 * i + 1]));
	std::vector<float> window(N);
	for (std::size_t n = 0; n < N; ++n) window[n] = static_cast<float>(0.5 - 0.5 * std::cos(kTau * static_cast<double>(n) / static_cast<double>(N)));   // (periodic Hann: halves sum to 1)
	std::vector<float> out[2];
	for (int c = 0; c < 2; ++c) out[c].assign(frames + N, 0.0f);
	const auto at = [&](std::ptrdiff_t i) { return i >= 0 && static_cast<std::size_t>(i) < in ? mono[static_cast<std::size_t>(i)] : 0.0f; };
	std::ptrdiff_t previous = 0;
	for (std::size_t k = 0; k * hop < frames; ++k) {
		const std::ptrdiff_t nominal = static_cast<std::ptrdiff_t>(std::llround(static_cast<double>(k) * step));
		std::ptrdiff_t chosen = nominal;
		if (k > 0) {   // the offset within `reach` whose first half matches best what would have followed the window before (every 8th sample, every other offset, then the neighbours)
			const std::ptrdiff_t follow = previous + static_cast<std::ptrdiff_t>(hop);
			const auto match = [&](std::ptrdiff_t start) {
				double sum = 0.0;
				for (std::size_t n = 0; n < hop; n += 8) sum += static_cast<double>(at(start + static_cast<std::ptrdiff_t>(n))) * at(follow + static_cast<std::ptrdiff_t>(n));
				return sum;
			};
			double best = -1e300;
			const std::ptrdiff_t r = static_cast<std::ptrdiff_t>(reach);
			for (std::ptrdiff_t d = -r; d <= r; d += 2) {
				const double v = match(nominal + d);
				if (v > best) best = v, chosen = nominal + d;
			}
			for (const std::ptrdiff_t d : {chosen - 1, chosen + 1}) {
				const double v = match(d);
				if (v > best) best = v, chosen = d;
			}
		}
		previous = chosen;
		for (std::size_t n = 0; n < N; ++n) {
			const std::ptrdiff_t i = chosen + static_cast<std::ptrdiff_t>(n);
			if (i < 0 || static_cast<std::size_t>(i) >= in) continue;
			for (int c = 0; c < 2; ++c) out[c][k * hop + n] += window[n] * static_cast<float>(pcm[2 * static_cast<std::size_t>(i) + static_cast<std::size_t>(c)]);
		}
	}
	std::vector<std::int16_t> result(frames * 2);   // (the first 20 ms rise with their window: a soundtrack fades in over half a second anyway)
	for (std::size_t i = 0; i < frames; ++i)
		for (int c = 0; c < 2; ++c) result[2 * i + static_cast<std::size_t>(c)] = static_cast<std::int16_t>(std::lround(std::max(-32767.0f, std::min(32767.0f, out[c][i]))));
	return result;
}

// A 16-bit stereo PCM WAV file. False when the file cannot be written.
inline bool writeWav(const std::string &path, const std::vector<std::int16_t> &pcm) {
	std::ofstream f(path, std::ios::binary);
	if (!f) return false;
	auto u32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) f.put(static_cast<char>((v >> (8 * i)) & 0xFF)); };
	auto u16 = [&](std::uint16_t v) { for (int i = 0; i < 2; ++i) f.put(static_cast<char>((v >> (8 * i)) & 0xFF)); };
	const std::uint32_t bytes = static_cast<std::uint32_t>(pcm.size() * 2);
	f.write("RIFF", 4);
	u32(36 + bytes);
	f.write("WAVEfmt ", 8);
	u32(16);
	u16(1);   // PCM
	u16(2);   // stereo
	u32(kRate);
	u32(kRate * 4);
	u16(4);
	u16(16);
	f.write("data", 4);
	u32(bytes);
	for (std::int16_t s : pcm) u16(static_cast<std::uint16_t>(s));
	return static_cast<bool>(f);
}

// A WAV file's sound as one channel at kRate, -1 .. 1: 8, 16, 24 or 32-bit PCM or 32-bit float, any number of channels (averaged), any rate (resampled linearly). False,
// with why, when it is not one this reads (a recording to lay on a film: music_cues.h's `sample`).
inline bool readWav(const std::string &path, std::vector<float> &out, std::string *why = nullptr) {
	out.clear();
	const auto fail = [&](const std::string &w) {
		if (why) *why = w;
		return false;
	};
	std::ifstream f(path, std::ios::binary);
	if (!f) return fail("cannot be read");
	const std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	const auto u16 = [&](std::size_t at) { return static_cast<unsigned>(b[at] | (b[at + 1] << 8)); };
	const auto u32 = [&](std::size_t at) { return static_cast<std::uint32_t>(b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24)); };
	if (b.size() < 12 || std::string(b.begin(), b.begin() + 4) != "RIFF" || std::string(b.begin() + 8, b.begin() + 12) != "WAVE") return fail("is not a WAV file");
	unsigned format = 0, channels = 0, rate = 0, bits = 0;
	std::size_t data = 0, dataSize = 0;
	for (std::size_t at = 12; at + 8 <= b.size();) {
		const std::string id(b.begin() + static_cast<std::ptrdiff_t>(at), b.begin() + static_cast<std::ptrdiff_t>(at) + 4);
		const std::size_t size = u32(at + 4);
		if (id == "fmt " && size >= 16 && at + 8 + 16 <= b.size()) {
			format = u16(at + 8);
			channels = u16(at + 10);
			rate = u32(at + 12);
			bits = u16(at + 22);
			if (format == 0xFFFE && size >= 26 && at + 8 + 26 <= b.size()) format = u16(at + 8 + 24);   // (WAVE_FORMAT_EXTENSIBLE: the sub-format's first two bytes)
		} else if (id == "data") {
			data = at + 8;
			dataSize = std::min(size, b.size() - data);
		}
		at += 8 + size + (size & 1);
	}
	if (!data || !channels || !rate) return fail("has no sound in it (no fmt or data chunk)");
	const bool pcm = format == 1, flt = format == 3;
	if (!(pcm && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) && !(flt && bits == 32)) return fail("is not 8/16/24/32-bit PCM or 32-bit float");
	const std::size_t step = bits / 8, frames = dataSize / (step * channels);
	std::vector<float> mono(frames);
	for (std::size_t i = 0; i < frames; ++i) {
		double sum = 0.0;
		for (unsigned c = 0; c < channels; ++c) {
			const std::size_t at = data + (i * channels + c) * step;
			double v = 0.0;
			if (flt) {
				const std::uint32_t raw = u32(at);
				float x;
				std::memcpy(&x, &raw, 4);
				v = x;
			} else if (bits == 8) {
				v = (static_cast<int>(b[at]) - 128) / 128.0;
			} else if (bits == 16) {
				v = static_cast<std::int16_t>(u16(at)) / 32768.0;
			} else if (bits == 24) {
				const std::int32_t s = static_cast<std::int32_t>((b[at] << 8) | (b[at + 1] << 16) | (static_cast<std::uint32_t>(b[at + 2]) << 24)) >> 8;
				v = s / 8388608.0;
			} else {
				v = static_cast<std::int32_t>(u32(at)) / 2147483648.0;
			}
			sum += v;
		}
		mono[i] = static_cast<float>(sum / channels);
	}
	if (rate == static_cast<unsigned>(kRate)) {
		out = std::move(mono);
		return true;
	}
	const double ratio = static_cast<double>(rate) / kRate;
	out.resize(static_cast<std::size_t>(frames / ratio));
	for (std::size_t i = 0; i < out.size(); ++i) {
		const double x = i * ratio;
		const std::size_t k = static_cast<std::size_t>(x);
		const double u = x - k;
		out[i] = static_cast<float>(k + 1 < mono.size() ? mono[k] * (1.0 - u) + mono[k + 1] * u : mono[std::min(k, mono.size() - 1)]);
	}
	return true;
}

}  // namespace audio
}  // namespace cartoon
