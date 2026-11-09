#include "thandor/audio.hpp"
#include <chrono>
#include <dlfcn.h>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace thandor;
using namespace std::chrono_literals;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void eventually(F condition,const char* message){
 for(int i=0;i<500;++i){if(condition())return;std::this_thread::sleep_for(2ms);}require(false,message);
}
int main()try{
 void* lib=dlopen("libasound.so.2",RTLD_NOW);require(lib,"fake ALSA unavailable");
 auto mode=reinterpret_cast<void(*)(int)>(dlsym(lib,"test_alsa_mode"));
 auto opens=reinterpret_cast<int(*)()>(dlsym(lib,"test_alsa_opens"));
 auto closes=reinterpret_cast<int(*)()>(dlsym(lib,"test_alsa_closes"));
 auto left=reinterpret_cast<int(*)()>(dlsym(lib,"test_alsa_left"));
 auto right=reinterpret_cast<int(*)()>(dlsym(lib,"test_alsa_right"));
 require(mode&&opens&&closes&&left&&right,"fake ALSA controls missing");
 SamPcmData pcm;for(int i=0;i<1024;++i){pcm.interleaved_stereo.push_back(1000);pcm.interleaved_stereo.push_back(2000);}
 mode(1);
 // Both callers share failure backoff, even when submitted concurrently.
 std::vector<std::jthread> callers;
 for(int i=0;i<8;++i)callers.emplace_back([&]{for(int j=0;j<64;++j){require(!play_pcm_loop(pcm),"failed open accepted loop");require(!play_pcm_one_shot(pcm),"failed open accepted shot");}});
 callers.clear();require(opens()==1,"failed device reopened for each source");
 mode(0);std::this_thread::sleep_for(5100ms);
 std::vector<std::unique_ptr<LoopingPcmVoice>> loops;
 for(int i=0;i<200;++i){auto voice=play_pcm_loop(pcm,0x8000,0);require(voice&&voice->active(),"overlapping loop rejected");loops.push_back(std::move(voice));}
 eventually([&]{return left()==32767&&right()==0;},"mixed sources not saturated independently");
 require(opens()==2,"mixer opened a PCM per voice");
 loops.clear();std::this_thread::sleep_for(30ms);
 auto voice=play_pcm_loop(pcm);require(bool(voice),"new loop rejected");
 voice->set_gains(0,0x4000);
 eventually([&]{return left()==0&&right()==1000;},"live stereo gains were not applied");
 voice->stop();voice->stop();require(!voice->active(),"stop is not idempotent");
 for(int i=0;i<20;++i)require(play_pcm_one_shot(pcm),"overlapping shot rejected");
 eventually([&]{return audio_playback_diagnostics().one_shots_completed==20;},"partial writes lost one-shot completion");
 require(opens()==2,"one-shots opened extra devices");
 auto failed=play_pcm_loop(pcm);require(bool(failed),"write failure fixture rejected");mode(3);
 eventually([&]{return !failed->active();},"fatal write did not invalidate loop");
 eventually([&]{return closes()==1;},"fatal write did not close shared device");
 require(!play_pcm_loop(pcm)&&!play_pcm_one_shot(pcm),"write failure bypassed retry delay");
 require(opens()==2,"write failure caused immediate device reopen");
 std::cout<<"AUDIO_LINUX_MIXER PASS shared_pcm=1 overlapping_loops=200 shots=20 failure_attempts=1024 partial_writes=1 recovery=1\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
