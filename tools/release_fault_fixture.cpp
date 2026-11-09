#include "thandor/pck.hpp"
#include <iostream>
int main(int argc,char** argv)try{
    if(argc!=2)throw std::runtime_error("usage: release_fault_fixture OUTPUT_PATCH_PCK");
    const std::vector<thandor::PckWriteEntry> entries{
        {"test/broken.spr",std::vector<std::byte>(32),0},
        {"test/broken.gfx",std::vector<std::byte>(32),0},
        {"test/broken.pal",std::vector<std::byte>(32),0}};
    thandor::write_stored_pck(argv[1],entries);
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
