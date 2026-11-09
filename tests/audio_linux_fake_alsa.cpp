#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
namespace { std::atomic<int> mode{},opens{},closes{},writes{},left{},right{}; }
extern "C" {
void test_alsa_mode(int value){mode=value;}
int test_alsa_opens(){return opens;}
int test_alsa_closes(){return closes;}
int test_alsa_writes(){return writes;}
int test_alsa_left(){return left;}
int test_alsa_right(){return right;}
int snd_pcm_open(void** pcm,const char*,int,int){++opens;if(mode==1)return -1;*pcm=reinterpret_cast<void*>(1);return 0;}
int snd_pcm_set_params(void*,int,int,unsigned,unsigned,int,unsigned){return mode==2?-1:0;}
long snd_pcm_writei(void*,const void* samples,unsigned long count){
 std::this_thread::sleep_for(std::chrono::milliseconds(2));
 if(mode==3)return -1;
 const auto* data=static_cast<const int16_t*>(samples);left=data[0];right=data[1];++writes;
 return static_cast<long>(std::min(count,128UL)); // Exercise partial writes.
}
int snd_pcm_prepare(void*){return 0;}
int snd_pcm_drop(void*){return 0;}
int snd_pcm_close(void*){++closes;return 0;}
}
