#include "thandor/simulation_lab_ai_owner_list_v718.hpp"
#include <cassert>
#include <vector>
using namespace thandor;
int main() {
    std::vector<LabAiOwnerKindV718> owners;
    owners.insert(owners.end(), 150, LabAiOwnerKindV718::Structure00);
    owners.insert(owners.end(), 90, LabAiOwnerKindV718::Unit01);
    owners.insert(owners.end(), 200, LabAiOwnerKindV718::Visible02);
    owners.insert(owners.end(), 530, LabAiOwnerKindV718::Unseen03);
    const auto ws=lab_ai_admit_owner_list_v718(owners.size(), [&](std::size_t i){return owners[i];});
    assert(ws.structures00.size()==128 && ws.units01.size()==64);
    assert(ws.visible02.size()==128 && ws.targets07.size()==64);
    assert(ws.unseen03.size()==512);
    assert(ws.unseen03.front()==owners.size()-1 && ws.unseen03.back()==owners.size()-512);
    assert(ws.visible02.front()==439 && ws.visible02.back()==312);
    assert(ws.targets07.front()==439 && ws.targets07.back()==376);
    assert(ws.units01.front()==239 && ws.units01.back()==176);
    assert(ws.structures00.front()==149 && ws.structures00.back()==22);
    assert(&ws.workspace(0)==&ws.structures00 && &ws.workspace(1)==&ws.units01);
    assert(&ws.workspace(2)==&ws.visible02 && &ws.workspace(3)==&ws.unseen03);
    assert(&ws.workspace(7)==&ws.targets07);
    // Excluded owners do not consume a slot or change owner traversal order.
    owners.assign(202,LabAiOwnerKindV718::Visible02);
    owners.back()=LabAiOwnerKindV718::Skip;
    const auto skipped=lab_ai_admit_owner_list_v718(owners.size(),[&](std::size_t i){return owners[i];});
    assert(skipped.visible02.size()==128 && skipped.targets07.size()==64);
    assert(skipped.visible02.front()==200 && skipped.visible02.back()==73);
    assert(skipped.targets07.back()==137);
    // A visible workspace02 overflow must not leak later entries into 07.
    assert(skipped.visible02.front()==skipped.targets07.front());
}
