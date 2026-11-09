#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
REC=ROOT/'tools/gameplay_recorder'
SPEC=importlib.util.spec_from_file_location('schema',ROOT/'tools/audit_recording_schema.py')
M=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(M)

def record(tag,words):
    data=struct.pack('<'+'I'*len(words),*words);return tag+struct.pack('<I',len(data))+data

def fixture(version=2, flags=7, actor=None):
    h=bytearray(192);h[:8]=b'THDTGR1\0';struct.pack_into('<III',h,8,version,192,flags)
    rows=[record(b'TICK',[1]+[0]*11),record(b'ACTR',actor or [17]+[0]*40)]
    rows+=[record(b'ENDT',[1,3,1,0])]
    return bytes(h)+b''.join(rows)

class Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.dir=Path(cls.temp.name);cls.exe=cls.dir/'reader'
        p=cls.dir/'main.cpp';p.write_text(r'''
#include "ThandorGameplayRecording.hpp"
#include <cassert>
#include <iostream>
#include <map>
using u32=unsigned int;
#include "gameplay_recording_owner.h"
static std::map<u32,u32> memory;
static u32 read32(u32 a){return memory.at(a);}
int main(int argc,char**argv){
 if(argc==1){
  memory={{0x10a4,0},{0x1048,0x2000},{0x2008,0x3000},{0x3000,0x2000}};
  assert(tgr_army_for_node_v2(0x1000,read32)==0x3000);
  memory[0x3000]=0x4000;assert(!tgr_army_for_node_v2(0x1000,read32));
  memory[0x2008]=0;assert(!tgr_army_for_node_v2(0x1000,read32));
  memory[0x1048]=0;assert(!tgr_army_for_node_v2(0x1000,read32));
  memory[0x10a4]=1;assert(!tgr_army_for_node_v2(0x1000,read32));return 0;
 }
 thandor::recording::Reader r;std::string error;
 if(!r.open(argv[1],&error,argc>2)){std::cerr<<error;return 2;}
 thandor::recording::TickFrame f;int frames=0;
 while(r.nextTick(f,&error))++frames;
 if(!error.empty()){std::cerr<<error;return 3;}
 std::cout<<frames<<" corrected="<<r.hasCorrectArmyState();return 0;
}
''')
        subprocess.run(['g++','-std=c++20','-O2','-Wall','-Wextra','-Werror','-I'+str(REC/'map_editor_recording'),'-I'+str(REC/'modding'),str(p),str(REC/'map_editor_recording/ThandorGameplayRecording.cpp'),'-o',str(cls.exe)],check=True)
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def run_reader(self,data,legacy=False):
        p=self.dir/'test.tgr';p.write_bytes(data)
        return subprocess.run([str(self.exe),str(p)]+(['legacy'] if legacy else []),text=True,capture_output=True)
    def test_owner_indirection_and_non_actor_nodes(self):subprocess.run([str(self.exe)],check=True)
    def test_v2(self):self.assertEqual(self.run_reader(fixture()).stdout,'1 corrected=1')
    def test_v1_requires_explicit_forensic_opt_in(self):
        r=self.run_reader(fixture(1));self.assertIn('legacy V1',r.stderr);self.assertNotEqual(r.returncode,0)
        self.assertEqual(self.run_reader(fixture(1),True).stdout,'1 corrected=0')
    def test_v2_flag(self):self.assertIn('owner flag',self.run_reader(fixture(flags=3)).stderr)
    def test_counter(self):
        d=bytearray(fixture());struct.pack_into('<I',d,len(d)-12,4)
        self.assertIn('count mismatch',self.run_reader(d).stderr)
    def test_nested_tick(self):
        d=fixture();self.assertIn('nested TICK',self.run_reader(d[:-24]+record(b'TICK',[2]+[0]*11)).stderr)
    def test_missing_end(self):self.assertIn('before ENDT',self.run_reader(fixture()[:-24]).stderr)
    def test_duplicate_actor(self):
        d=fixture();self.assertIn('duplicate actor',self.run_reader(d[:-24]+record(b'ACTR',[17]+[0]*40)+d[-24:]).stderr)
    def test_unknown_tag_counts(self):
        d=bytearray(fixture()[:-24]+record(b'FUTR',[123])+record(b'ENDT',[1,4,1,0]))
        self.assertEqual(self.run_reader(d).returncode,0)
    def test_truncated_unknown_tag(self):
        self.assertIn('truncated unknown',self.run_reader(fixture()[:-24]+b'FUTR'+struct.pack('<I',12)+b'1234').stderr)
    def test_outside_frame(self):self.assertIn('outside TICK',self.run_reader(fixture()+record(b'FUTR',[0])).stderr)
    def test_semantic_recovery(self):
        a=[17]+[0]*40;a[1]=3;a[19]=123;a[38]=0x40;a[40]=47
        p=self.dir/'schema.tgr';p.write_bytes(fixture(1,3,a));r=M.analyze(p)
        self.assertEqual(r['attachment_count_distribution'],{3:1});self.assertEqual(r['researching_root_samples_by_technology'],{47:1});self.assertEqual(r['errors'],[])
    def test_research_mask_bits(self):self.assertEqual(M.added_bits([0,0],[1,1<<31]),[0,63])
if __name__=='__main__':unittest.main()
