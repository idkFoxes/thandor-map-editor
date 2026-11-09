import struct,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from audit_tec201_recording import analyze

def record(tag,words):
 b=struct.pack('<'+'I'*len(words),*words);return tag+struct.pack('<I',len(b))+b

def sample(faction=0x53c811,phase=1):
 header=bytearray(192);header[:8]=b'THDTGR1\0';struct.pack_into('<II',header,8,1,192)
 return bytes(header)+record(b'TICK',[90,90,0,307,0,0,0,0,0,2,1,0])+record(b'AIPH',[90,faction,phase,0])+record(b'ENDT',[90,3,0,0])

class AiphAdmission(unittest.TestCase):
 def test_bad_argument_does_not_discard_reliable_frame(self):
  r=analyze(sample());self.assertEqual(r['admission']['accepted_frames'],1)
  self.assertEqual(r['invalid_aiph_count'],1);self.assertEqual(r['valid_planner_factions'],[])
  self.assertEqual(r['legacy_hook_address_plus_one_count'],1)
 def test_valid_unscheduled_invocation_is_not_execution(self):
  r=analyze(sample(2));self.assertEqual(r['valid_planner_factions'],[2])
  self.assertFalse(r['direct_planner_calls'][0]['passes_tick_slot'])
 def test_valid_scheduled_invocation(self):
  self.assertTrue(analyze(sample(3))['direct_planner_calls'][0]['passes_tick_slot'])
 def test_bad_phase_rejects_only_aiph(self):
  r=analyze(sample(3,0));self.assertEqual(r['valid_aiph_count'],0)
  self.assertEqual(r['admission']['accepted_frames'],1)
if __name__=='__main__':unittest.main()
