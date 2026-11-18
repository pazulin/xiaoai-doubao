#pragma once
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace xiaoai_plus::audio {

// Linear subband echo canceller: subtracts the speaker's own playback (hardware loopback,
// capture ch3) from one microphone and leaves a talker over it untouched. No residual-echo
// suppression, so near-end speech is not attenuated. C++ port of
// OwnDing/open-xiaoai examples/xiaozhi/xiaozhi/services/audio/aec.py (MIT), measured on OH2P.
// Per bin, echo = last `taps` reference frames x transfer function solved from exponentially
// averaged cross-spectra; learns only while the reference is active and keeps the filter.
class SubbandAec {
 public:
  explicit SubbandAec(int n_fft = 1024, int hop = 256, int taps = 8, double tau_s = 3.0,
                      int solve_every = 4, int sample_rate = 16000);
  // mic/ref: S16 mono, equal length. Returns as many cleaned samples, delayed by n_fft.
  std::vector<int16_t> Process(const int16_t* mic, const int16_t* ref, size_t n);
  bool ref_active() const { return ref_active_; }
  // Smoothed echo reduction while playing, dB; negative before any playback.
  double reduction_db() const;

 private:
  using C = std::complex<double>;
  void Frame();
  void Fft(std::vector<C>& a, bool inverse) const;
  void Solve(size_t bin);

  int n_, hop_, taps_, solve_every_, bins_;
  double lam_;
  std::vector<double> window_;
  std::vector<C> rxx_, rxy_, filter_, history_;
  std::vector<double> mic_buf_, ref_buf_, overlap_, norm_, out_pending_;
  std::vector<C> work_, x_, y_, twiddle_;
  std::vector<size_t> rev_;
  uint64_t frames_{0};
  bool ref_active_{false};
  double mic_power_{0}, out_power_{0};
};

}  // namespace xiaoai_plus::audio
