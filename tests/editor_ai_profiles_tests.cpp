#include "editor_ai_profiles.hpp"
#include "editor_user_settings.hpp"
#include "thandor/simulation_lab_worker.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <stdexcept>
using namespace editor;
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F action){bool caught=false;try{action();}catch(const std::exception&){caught=true;}check(caught,"invalid profile accepted");}
int main()try{
 const auto root=std::filesystem::temp_directory_path()/("thandor-ai-profiles-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}}cleanup{root};
 AiProfileParameters stock{};for(size_t i=0;i<128;++i)stock[i]=uint32_t(i)*0x01010101U;
 stock[0]=0x80000000U;stock[127]=0xFFFFFFFFU;
 check(ai_profile_decode(ai_profile_encode(stock),{})==stock,"signed/unsigned 128-value roundtrip drift");
 check(ai_profile_knowledge(stock).parameters()==stock,"INI conversion changed KI binary values");
 EditorAiProfiles profiles;profiles.initialize(stock,root,false);
 check(std::filesystem::exists(profiles.path(false))&&!std::filesystem::exists(profiles.path(true)),"first startup did not create only default INI");
 const auto original=thandor::read_file(profiles.path(false));
 const auto old_time=std::filesystem::last_write_time(profiles.path(false))-std::chrono::hours(24);
 std::filesystem::last_write_time(profiles.path(false),old_time);profiles.initialize(stock,root,false);
 check(std::filesystem::last_write_time(profiles.path(false))==old_time,"existing default INI was rewritten on startup");
 auto edited=stock;edited[4]=12345;edited[0x7f]=0xF1234567U;
 profiles.save_custom(edited);
 check(profiles.use_custom&&profiles.custom_available&&profiles.active()==edited,"edited custom profile was not activated");
 check(thandor::read_file(profiles.path(false))==original,"custom edit overwrote default profile");
 EditorAiProfiles reopened;reopened.initialize(stock,root,true);
 check(reopened.use_custom&&reopened.active()==edited,"custom choice not restored");
 reopened.select(false);check(reopened.active()==stock,"default selection did not restore baseline");
 auto partial=ai_profile_decode("\xEF\xBB\xBF[parameters]\np04.name=0xFFFFFFFF\n",stock);
 check(partial[4]==0xFFFFFFFF&&partial[5]==stock[5],"partial/hex/BOM profile support failed");
 for(const auto* bad:{"[parameters]\np04.a=1\np04.b=2\n","[parameters]\np80.invalid=1\n","[parameters]\np01.a=4294967296\n","[parameters]\np01.a=-2147483649\n","[parameters]\np01.a=1.5\n","[parameters]\np01.a=nope\n","[parameters]\n"})rejects([&]{(void)ai_profile_decode(bad,stock);});
 ai_profile_write(profiles.path(true),"invalid custom profile");reopened.initialize(stock,root,true);
 check(!reopened.use_custom&&!reopened.message.empty()&&reopened.active()==stock,"corrupt custom did not recover to default");
 check(thandor::read_file(profiles.path(true)).size()==22,"corrupt user profile was overwritten");
 profiles.save_custom(edited);const auto valid=profiles.active();
 ai_profile_write(profiles.path(true),std::string(524289,'x'));
 rejects([&]{profiles.select(true);});check(profiles.active()==valid,"failed reload replaced active parameters");
 profiles.save_custom(edited);
 EditorUserSettings preferences;preferences.ai_profile="custom";preferences.simulation_lab.duration_ticks=108000;
 check(editor_user_settings_decode(editor_user_settings_encode(preferences),{}).simulation_lab.duration_ticks==108000,"90-minute UI duration not preserved");
 check(editor_user_settings_decode(editor_user_settings_encode(preferences),{}).ai_profile=="custom","profile preference not roundtripped");
 check(editor_user_settings_decode("[editor]\nai_profile=improved\n",{}).ai_profile=="default","unsupported alternative profile accepted");
 const auto names=thandor::known_ai_knowledge_semantics();check(names.size()==62,"original mapped parameter census changed");
 check(thandor::find_ai_knowledge_semantic(4)->label.find("Tritium tank")!=std::string_view::npos,"stock building name not used");
 for(const auto& semantic:names){check(semantic.label.find("workspace")==std::string_view::npos,"technical workspace label retained");check(semantic.label.find("structure 14")==std::string_view::npos,"numeric building label retained");}
 check(thandor::original_ai_workspace_descriptions().size()==15,"original planning-list descriptions missing");
 // Worker receives an exact chosen baseline; changing the file/profile later must
 // not mutate immutable snapshots of an already-running experiment.
 thandor::SimulationLabConfig config;config.run_count=1;config.duration_ticks=128;config.parameter_index=0x51;config.fixed_value=static_cast<int32_t>(edited[0x51]);
 thandor::SimulationLabWorker worker;worker.start(config,edited,{},thandor::FieldGrid::create(59,59,0,0));
 const auto snapshot=worker.snapshot();check(snapshot&&!snapshot->runs().empty(),"worker did not publish profile baseline");
 check(snapshot->runs()[0].ai_parameters==edited,"worker did not receive exact custom profile");
 profiles.select(false);check(snapshot->runs()[0].ai_parameters==edited,"profile selection mutated running snapshot");worker.clear();
 std::cout<<"PASS original 128-value roundtrip, first-start creation, custom-only edits, persistence, malformed/oversized recovery, names, and immutable worker baseline\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
