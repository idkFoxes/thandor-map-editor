#include "thandor/pioneer_site_selector_trace.hpp"
#include <stdexcept>
#include <iostream>
using thandor::PioneerSiteSelectorTrace;
using thandor::MovementPoint;
static void require(bool v,const char* what){if(!v) throw std::runtime_error(what);}
int main() try {
    PioneerSiteSelectorTrace trace;
    trace.begin(10,0);
    require(trace.calls==1 && trace.empty_workspace_calls==1 && !trace.last_best_exists,
            "empty workspace must be distinguished from zero-score site");
    trace.finish();
    require(trace.zero_positive_score_calls==1,"empty workspace should have no positive candidate");
    trace.begin(74,3);
    trace.observe(0,{1,1});
    trace.observe(-5,{2,2});
    trace.observe(0,{3,3});
    trace.finish();
    require(trace.calls==2 && trace.last_candidates==3 && trace.last_scores_evaluated==3 &&
            trace.last_positive_scores==0 && trace.last_best_score==0 &&
            !trace.last_best_exists && trace.zero_positive_score_calls==2 &&
            trace.empty_workspace_calls==1,"zero score must be distinct from absent workspace");
    trace.begin(138,4);
    trace.observe(20,{4,4});
    trace.observe(20,{5,5});
    trace.observe(12,{6,6});
    trace.observe(32,{7,7});
    trace.finish();
    require(trace.last_scores_evaluated==4 && trace.last_positive_scores==4 &&
            trace.last_best_score==32 && trace.last_best_point.x==7 && trace.last_best_point.y==7 &&
            trace.last_best_exists && !trace.retail_admission_evaluated,
            "selector chose wrong positive best or asserted unmodeled retail gates");
    trace.begin(202,2);
    trace.observe(9,{8,8});
    trace.observe(9,{9,9});
    trace.finish();
    require(trace.last_best_point.x==8 && trace.last_best_point.y==8,
            "strictly-greater tie must preserve first candidate");
    require(trace.calls==4 && trace.zero_positive_score_calls==2 &&
            trace.empty_workspace_calls==1, "selector cumulative counters unexpectedly reset");
    std::cout<<"V593_PIONEER_SITE_SELECTOR_TRACE PASS\n";
    return 0;
} catch(const std::exception& e) {
    std::cerr<<"V593_PIONEER_SITE_SELECTOR_TRACE FAIL: "<<e.what()<<'\n';
    return 1;
}
