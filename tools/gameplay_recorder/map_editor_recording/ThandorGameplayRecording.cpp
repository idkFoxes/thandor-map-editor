#include "ThandorGameplayRecording.hpp"
#include <cstring>
#include <unordered_set>

namespace thandor::recording {
static_assert(sizeof(FileHeader)==192);
static_assert(sizeof(Tick)==48);
static_assert(sizeof(UserCommand)==32);
static_assert(sizeof(AiPlanner)==16);
static_assert(sizeof(FactionState)==296);
static_assert(sizeof(ActorState)==164);
static_assert(sizeof(AiActorAction)==20);
static_assert(sizeof(AiFactionAction)==16);
static_assert(sizeof(ActorRemove)==8);
static_assert(sizeof(TickEnd)==16);

bool Reader::fail(std::string* error,const char* message){ if(error)*error=message; return false; }

bool Reader::open(const std::string& path,std::string* error,bool allowLegacyActorState){
    if(error)error->clear();
    file_.close(); file_.clear(); file_.open(path,std::ios::binary);
    if(!file_) return fail(error,"cannot open recording");
    file_.read(reinterpret_cast<char*>(&header_),sizeof(header_));
    if(file_.gcount()!=static_cast<std::streamsize>(sizeof(header_))) return fail(error,"truncated recording header");
    static const std::uint8_t magic[8]={'T','H','D','T','G','R','1',0};
    if(std::memcmp(header_.magic,magic,8)!=0) return fail(error,"not a THDTGR1 recording");
    if((header_.version!=1 && header_.version!=2) || header_.headerBytes!=sizeof(FileHeader)) return fail(error,"unsupported recording version/header size");
    if(header_.version==1 && !allowLegacyActorState)
        return fail(error,"legacy V1 ACTR contains model payloads misread as army state; opt in for forensic inspection");
    if(header_.version==2 && !(header_.flags & 4u))
        return fail(error,"V2 recording lacks corrected army-owner flag");
    return true;
}

bool Reader::readHeader(RecordHeader& h,std::string* error){
    file_.read(reinterpret_cast<char*>(&h),sizeof(h));
    if(file_.gcount()==0 && file_.eof()) return false;
    if(file_.gcount()!=static_cast<std::streamsize>(sizeof(h))) return fail(error,"truncated record header");
    if(h.payloadBytes>16u*1024u*1024u) return fail(error,"record payload is implausibly large");
    return true;
}

template<class T> bool Reader::readPayload(std::uint32_t bytes,T& out,std::string* error){
    if(bytes!=sizeof(T)) return fail(error,"unexpected record payload size");
    file_.read(reinterpret_cast<char*>(&out),sizeof(T));
    if(file_.gcount()!=static_cast<std::streamsize>(sizeof(T))) return fail(error,"truncated record payload");
    return true;
}

bool Reader::nextTick(TickFrame& f,std::string* error){
    if(error)error->clear();
    f=TickFrame{};
    std::unordered_set<std::uint32_t> actorIds, factionIds;
    std::uint32_t recordCount=1;
    RecordHeader h{};
    if(!readHeader(h,error)) return false;
    if(h.tag!=TAG_TICK) return fail(error,"record outside TICK/ENDT frame");
    if(!readPayload(h.payloadBytes,f.tick,error)) return false;
    for(;;){
        if(!readHeader(h,error)) return fail(error,"recording ended before ENDT");
        ++recordCount;
        switch(h.tag){
        case TAG_TICK: return fail(error,"nested TICK before ENDT");
        case TAG_UCMD: { UserCommand x{}; if(!readPayload(h.payloadBytes,x,error))return false; f.userCommands.push_back(x); break; }
        case TAG_AIPH: { AiPlanner x{}; if(!readPayload(h.payloadBytes,x,error))return false; f.aiPlannerPasses.push_back(x); break; }
        case TAG_FACT: { FactionState x{}; if(!readPayload(h.payloadBytes,x,error))return false; if(x.factionIndex>=8 || !factionIds.insert(x.factionIndex).second)return fail(error,"invalid or duplicate faction"); f.factions.push_back(x); break; }
        case TAG_ACTR: { ActorState x{}; if(!readPayload(h.payloadBytes,x,error))return false; if(!x.actorId || !actorIds.insert(x.actorId).second)return fail(error,"invalid or duplicate actor"); f.actors.push_back(x); break; }
        case TAG_AIAA: { AiActorAction x{}; if(!readPayload(h.payloadBytes,x,error))return false; f.aiActorActions.push_back(x); break; }
        case TAG_AIFA: { AiFactionAction x{}; if(!readPayload(h.payloadBytes,x,error))return false; f.aiFactionActions.push_back(x); break; }
        case TAG_RMOV: { ActorRemove x{}; if(!readPayload(h.payloadBytes,x,error))return false; f.removedActors.push_back(x); break; }
        case TAG_ENDT:
            if(!readPayload(h.payloadBytes,f.end,error))return false;
            if(f.end.simulationTick!=f.tick.simulationTick)return fail(error,"TICK/ENDT tick mismatch");
            if(f.end.recordCount!=recordCount || f.end.actorCount!=f.actors.size() || f.end.factionCount!=f.factions.size())
                return fail(error,"ENDT record/actor/faction count mismatch");
            return true;
        default:
            file_.ignore(h.payloadBytes);
            if(file_.gcount()!=static_cast<std::streamsize>(h.payloadBytes)) return fail(error,"truncated unknown record");
            break;
        }
    }
}

std::string utf8MapName(const FileHeader& h){
    std::string out;
    for(std::size_t i=0;i<64 && h.mapName[i];++i){
        std::uint32_t c=h.mapName[i];
        if(c<0x80)out.push_back(static_cast<char>(c));
        else if(c<0x800){out.push_back(char(0xc0|(c>>6)));out.push_back(char(0x80|(c&0x3f)));}
        else{out.push_back(char(0xe0|(c>>12)));out.push_back(char(0x80|((c>>6)&0x3f)));out.push_back(char(0x80|(c&0x3f)));}
    }
    return out;
}
}
