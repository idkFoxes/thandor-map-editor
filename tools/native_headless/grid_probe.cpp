#define main NativeHeadlessMain
#include "driver.cpp"
#undef main
int main(int argc,char** argv){int status=NativeHeadlessMain(argc,argv);if(status)return status;FILE *f=std::fopen("native_grid.bin","wb");if(!f)return 5;uint32_t dims[]={g_GridScratchWidth,uint32_t(g_GridScratchHeight)};bool ok=std::fwrite(dims,4,2,f)==2;for(uint32_t i=0;i<dims[0]*dims[1];++i){uint32_t s=g_GridScratchPrimary[i].stateMask;ok&=std::fwrite(&s,4,1,f)==1;}if(std::fclose(f))ok=false;return ok?0:5;}
