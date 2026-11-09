#include "thandor/military_anchor_v599.hpp"
#include <iostream>
#include <vector>
using namespace thandor;
int main(){int32_t mx,my,px,py,bias,scale,weight;unsigned count;
 while(std::cin>>mx>>my>>px>>py>>bias>>scale>>weight>>count){
  if(count>32)return 2;
  std::vector<MilitarySiteV599> sites(count);for(auto& s:sites)std::cin>>s.x_q12>>s.y_q12>>s.score;
  MilitaryScoreParametersV599 p{};p.site_bias=bias;p.site_scale=scale;
  auto c=military_choose_anchor_action_v599(mx,my,p,weight,0,0,sites,{},{},{},false,MilitaryPositionQ12{px,py});
  std::cout<<c.score<<' '<<(c.kind==1?int(c.site_index):-1)<<'\n';
 }
}
