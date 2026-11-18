// Host check: runs SubbandAec over a 4-ch S32 WAV (ch0 mic, ch3 loopback) and writes
// cleaned S16 mono raw output. Usage: subband_aec_check in.wav out.raw
#include "audio/subband_aec.hpp"
#include <cstdio>
#include <vector>
int main(int argc, char** argv) {
  if (argc < 3) return 2;
  FILE* f = std::fopen(argv[1], "rb");
  if (!f) return 3;
  std::fseek(f, 44, SEEK_SET);
  std::vector<int32_t> s;
  int32_t buf[4096];
  size_t n;
  while ((n = std::fread(buf, 4, 4096, f)) > 0) s.insert(s.end(), buf, buf + n);
  std::fclose(f);
  const size_t frames = s.size() / 4;
  xiaoai_plus::audio::SubbandAec aec;
  FILE* o = std::fopen(argv[2], "wb");
  for (size_t i = 0; i < frames; i += 1440) {
    const size_t k = std::min<size_t>(1440, frames - i);
    std::vector<int16_t> mic(k), ref(k);
    for (size_t j = 0; j < k; ++j) { mic[j] = int16_t(s[(i + j) * 4] >> 16); ref[j] = int16_t(s[(i + j) * 4 + 3] >> 16); }
    auto out = aec.Process(mic.data(), ref.data(), k);
    std::fwrite(out.data(), 2, out.size(), o);
  }
  std::fclose(o);
  std::printf("frames=%zu reduction_db=%.1f\n", frames, aec.reduction_db());
}
