#include "thandor/simulation.hpp"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

int main(int argc,char** argv) try {
    if(argc!=2)throw std::runtime_error("usage: v545_class21_pitch_oracle_compare <oracle.tsv>");
    std::ifstream input(argv[1]); if(!input)throw std::runtime_error("cannot open oracle TSV");
    std::string line; std::getline(input,line);
    std::size_t compared=0;
    while(std::getline(input,line)){
        if(line.empty())continue;
        std::istringstream row(line);
        std::string phase_s,coeff_s,slope_s,pitch_s;
        std::getline(row,phase_s,'\t'); std::getline(row,coeff_s,'\t');
        std::getline(row,slope_s,'\t'); std::getline(row,pitch_s,'\t');
        const auto phase=static_cast<std::int32_t>(std::stoll(phase_s));
        const auto coeff=static_cast<std::int32_t>(std::stoll(coeff_s));
        const auto binary=static_cast<std::uint32_t>(std::stoul(pitch_s,nullptr,16))&0xffffU;
        const auto editor=thandor::simulation_class21_flight_pitch_turn16(phase,coeff)&0xffffU;
        if(binary!=editor){
            std::ostringstream error; error<<"pitch mismatch phase="<<phase<<" coeff="<<coeff
                <<" binary=0x"<<std::hex<<binary<<" editor=0x"<<editor;
            throw std::runtime_error(error.str());
        }
        ++compared;
    }
    if(compared==0)throw std::runtime_error("oracle contained no class-21 pitch rows");
    std::cout<<"V545_CLASS21_PITCH_ORACLE_COMPARE PASS: "<<compared<<" exact comparisons\n";
    return 0;
} catch(const std::exception& e){
    std::cerr<<"V545_CLASS21_PITCH_ORACLE_COMPARE FAIL: "<<e.what()<<'\n'; return 1;
}
