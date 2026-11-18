#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>
#include <dirent.h>
namespace xiaoai_plus::audio {
enum class SessionCue { kNone, kWake, kExit, kListen };
inline const char* SessionCueName(SessionCue cue) {
  switch (cue) {
    case SessionCue::kWake: return "wake";
    case SessionCue::kExit: return "exit";
    case SessionCue::kListen: return "listen";
    default: return "none";
  }
}
// 160 ms at the existing 24 kHz mono S16LE output, with a 12 ms soft edge.
inline std::vector<uint8_t> MakeSessionCue(SessionCue cue,float gain) {
  if(cue==SessionCue::kNone)return {};
  constexpr int samples=3840,edge=288;
  constexpr double pi=3.14159265358979323846;
  std::vector<uint8_t> pcm(samples*2);
  double phase=0;
  for(int i=0;i<samples;++i) {
    const double t=double(i)/samples;
    const double hz=cue==SessionCue::kExit?660-220*t:660+220*t;
    const double envelope=std::min(1.0,std::min(double(i)/edge,double(samples-1-i)/edge));
    const int16_t value=static_cast<int16_t>(std::sin(phase)*32767*0.12*gain*envelope);
    phase+=2*pi*hz/24000;
    pcm[2*i]=static_cast<uint16_t>(value)&255;pcm[2*i+1]=static_cast<uint16_t>(value)>>8;
  }
  return pcm;
}
// Spoken cue: a random pre-rendered phrase (24 kHz mono S16LE .pcm) from
// phrases/{greet,bye,listen}/ next to config.ini; the last pick is not repeated.
// Falls back to the tone when the directory is missing or empty.
inline std::vector<uint8_t> MakeSpokenCue(SessionCue cue,float gain) {
  const char* dir=cue==SessionCue::kWake?"phrases/greet":cue==SessionCue::kExit?"phrases/bye":
                  cue==SessionCue::kListen?"phrases/listen":nullptr;
  if(!dir)return {};
  std::vector<std::string> files;
  if(DIR* d=opendir(dir)) {
    while(dirent* e=readdir(d)) {
      const std::string name=e->d_name;
      if(name.size()>4 && name.compare(name.size()-4,4,".pcm")==0)files.push_back(std::string(dir)+"/"+name);
    }
    closedir(d);
  }
  if(files.empty())return MakeSessionCue(cue==SessionCue::kListen?SessionCue::kWake:cue,gain);
  std::sort(files.begin(),files.end());
  static std::mt19937 rng{std::random_device{}()};
  static std::string last[4];
  auto& previous=last[static_cast<int>(cue)];
  std::string pick;
  for(int tries=0;tries<4;++tries) {
    pick=files[std::uniform_int_distribution<size_t>(0,files.size()-1)(rng)];
    if(pick!=previous || files.size()==1)break;
  }
  previous=pick;
  std::vector<uint8_t> pcm;
  if(FILE* f=std::fopen(pick.c_str(),"rb")) {
    uint8_t buf[4096];size_t n;
    while((n=std::fread(buf,1,sizeof buf,f))>0 && pcm.size()<24000*2*8)pcm.insert(pcm.end(),buf,buf+n);
    std::fclose(f);
  }
  if(pcm.size()<2)return MakeSessionCue(cue==SessionCue::kListen?SessionCue::kWake:cue,gain);
  pcm.resize(pcm.size()&~size_t(1));
  if(gain<0.999f) {
    for(size_t i=0;i+1<pcm.size();i+=2) {
      const auto v=static_cast<int16_t>(uint16_t(pcm[i])|(uint16_t(pcm[i+1])<<8));
      const auto s=static_cast<int16_t>(std::lround(v*gain));
      pcm[i]=static_cast<uint16_t>(s)&255;pcm[i+1]=static_cast<uint16_t>(s)>>8;
    }
  }
  return pcm;
}
} // namespace xiaoai_plus::audio
