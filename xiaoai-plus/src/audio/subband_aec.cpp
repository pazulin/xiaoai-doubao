#include "audio/subband_aec.hpp"

#include <algorithm>
#include <cmath>

namespace xiaoai_plus::audio {

SubbandAec::SubbandAec(int n_fft, int hop, int taps, double tau_s, int solve_every, int sample_rate)
    : n_(n_fft), hop_(hop), taps_(taps), solve_every_(std::max(1, solve_every)),
      bins_(n_fft / 2 + 1), lam_(std::exp(-double(hop) / (sample_rate * tau_s))) {
  constexpr double pi = 3.14159265358979323846;
  window_.resize(n_);
  for (int i = 0; i < n_; ++i) window_[i] = std::sqrt(0.5 - 0.5 * std::cos(2 * pi * i / n_));
  rxx_.assign(size_t(bins_) * taps_ * taps_, C{});
  for (int b = 0; b < bins_; ++b)
    for (int i = 0; i < taps_; ++i) rxx_[(size_t(b) * taps_ + i) * taps_ + i] = 1e-12;
  rxy_.assign(size_t(bins_) * taps_, C{});
  filter_.assign(size_t(bins_) * taps_, C{});
  history_.assign(size_t(bins_) * taps_, C{});
  mic_buf_.assign(n_ - hop_, 0.0);
  ref_buf_.assign(n_ - hop_, 0.0);
  overlap_.assign(n_, 0.0);
  norm_.assign(n_, 0.0);
  out_pending_.assign(hop_, 0.0);
  work_.resize(n_);
  x_.resize(bins_);
  y_.resize(bins_);
  twiddle_.resize(n_ / 2);
  for (int k = 0; k < n_ / 2; ++k) twiddle_[k] = std::polar(1.0, -2 * pi * k / n_);
  rev_.resize(n_);
  int bits = 0;
  while ((1 << bits) < n_) ++bits;
  for (int i = 0; i < n_; ++i) {
    size_t r = 0;
    for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= size_t(1) << (bits - 1 - b);
    rev_[i] = r;
  }
}

void SubbandAec::Fft(std::vector<C>& a, bool inverse) const {
  for (int i = 0; i < n_; ++i) if (size_t(i) < rev_[i]) std::swap(a[i], a[rev_[i]]);
  for (int len = 2; len <= n_; len <<= 1) {
    const int half = len / 2, step = n_ / len;
    for (int i = 0; i < n_; i += len)
      for (int j = 0; j < half; ++j) {
        C w = twiddle_[size_t(j) * step];
        if (inverse) w = std::conj(w);
        const C u = a[i + j], v = a[i + j + half] * w;
        a[i + j] = u + v;
        a[i + j + half] = u - v;
      }
  }
}

void SubbandAec::Solve(size_t b) {
  const int t = taps_;
  C* r = &rxx_[b * t * t];
  double trace = 0;
  for (int i = 0; i < t; ++i) trace += r[i * t + i].real();
  const double reg = trace / t * 1e-3 + 1e-12;
  C a[16][17];
  for (int i = 0; i < t; ++i) {
    for (int j = 0; j < t; ++j) a[i][j] = std::conj(r[i * t + j]) + (i == j ? C(reg) : C{});
    a[i][t] = rxy_[b * t + i];
  }
  for (int c = 0; c < t; ++c) {
    int p = c;
    for (int i = c + 1; i < t; ++i) if (std::abs(a[i][c]) > std::abs(a[p][c])) p = i;
    if (std::abs(a[p][c]) < 1e-300) return;
    if (p != c) for (int j = c; j <= t; ++j) std::swap(a[p][j], a[c][j]);
    for (int i = c + 1; i < t; ++i) {
      const C f = a[i][c] / a[c][c];
      for (int j = c; j <= t; ++j) a[i][j] -= f * a[c][j];
    }
  }
  for (int i = t - 1; i >= 0; --i) {
    C s = a[i][t];
    for (int j = i + 1; j < t; ++j) s -= a[i][j] * filter_[b * t + j];
    filter_[b * t + i] = s / a[i][i];
  }
}

void SubbandAec::Frame() {
  const int t = taps_;
  for (int i = 0; i < n_; ++i) work_[i] = C(ref_buf_[i] * window_[i], 0);
  Fft(work_, false);
  std::copy(work_.begin(), work_.begin() + bins_, x_.begin());
  for (int i = 0; i < n_; ++i) work_[i] = C(mic_buf_[i] * window_[i], 0);
  Fft(work_, false);
  std::copy(work_.begin(), work_.begin() + bins_, y_.begin());

  ref_active_ = false;
  for (int i = n_ - hop_; i < n_; ++i) if (std::abs(ref_buf_[i]) > 2.0) { ref_active_ = true; break; }

  const bool solve = ref_active_ && frames_ % solve_every_ == 0;
  for (int b = 0; b < bins_; ++b) {
    C* h = &history_[size_t(b) * t];
    for (int i = t - 1; i > 0; --i) h[i] = h[i - 1];
    h[0] = x_[b];
    if (ref_active_) {
      C* r = &rxx_[size_t(b) * t * t];
      C* xy = &rxy_[size_t(b) * t];
      for (int i = 0; i < t; ++i) {
        for (int j = 0; j < t; ++j) r[i * t + j] = lam_ * r[i * t + j] + (1 - lam_) * h[i] * std::conj(h[j]);
        xy[i] = lam_ * xy[i] + (1 - lam_) * y_[b] * std::conj(h[i]);
      }
      if (solve) Solve(size_t(b));
    }
    C echo{};
    for (int i = 0; i < t; ++i) echo += filter_[size_t(b) * t + i] * h[i];
    y_[b] -= echo;
  }
  if (ref_active_) ++frames_;

  for (int k = 0; k < bins_; ++k) work_[k] = y_[k];
  for (int k = 1; k < n_ / 2; ++k) work_[n_ - k] = std::conj(y_[k]);
  Fft(work_, true);
  for (int i = 0; i < n_; ++i) {
    overlap_[i] += work_[i].real() / n_ * window_[i];
    norm_[i] += window_[i] * window_[i];
  }
  double mp = 0, op = 0;
  for (int i = 0; i < hop_; ++i) {
    const double o = overlap_[i] / std::max(norm_[i], 1e-6);
    out_pending_.push_back(o);
    mp += mic_buf_[i] * mic_buf_[i];
    op += o * o;
  }
  overlap_.erase(overlap_.begin(), overlap_.begin() + hop_);
  overlap_.resize(n_, 0.0);
  norm_.erase(norm_.begin(), norm_.begin() + hop_);
  norm_.resize(n_, 0.0);
  if (ref_active_) {
    mic_power_ = 0.95 * mic_power_ + 0.05 * mp / hop_;
    out_power_ = 0.95 * out_power_ + 0.05 * op / hop_;
  }
}

std::vector<int16_t> SubbandAec::Process(const int16_t* mic, const int16_t* ref, size_t n) {
  for (size_t i = 0; i < n; ++i) { mic_buf_.push_back(mic[i]); ref_buf_.push_back(ref[i]); }
  while (mic_buf_.size() >= size_t(n_)) {
    Frame();
    mic_buf_.erase(mic_buf_.begin(), mic_buf_.begin() + hop_);
    ref_buf_.erase(ref_buf_.begin(), ref_buf_.begin() + hop_);
  }
  const size_t take = std::min(n, out_pending_.size());
  std::vector<int16_t> out(n, 0);
  for (size_t i = 0; i < take; ++i)
    out[i] = static_cast<int16_t>(std::lround(std::clamp(out_pending_[i], -32768.0, 32767.0)));
  out_pending_.erase(out_pending_.begin(), out_pending_.begin() + take);
  return out;
}

double SubbandAec::reduction_db() const {
  if (mic_power_ <= 0 || out_power_ <= 0) return -1;
  return 10 * std::log10(mic_power_ / out_power_);
}

}  // namespace xiaoai_plus::audio
