from pathlib import Path
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from scenario_legacy_admission_tests import record

TOOL=Path(__file__).resolve().parents[1]/'tools/analyze_native_economy_timeline.py'

def frame(tick,flags,overflow=0,bad_command=False,command=False):
    actor=[0]*41;actor[0]=7;actor[1]=999;actor[38]=flags;actor[40]=201
    faction=[0]*74;faction[0]=2;faction[2]=28000
    parts=[record(b'TICK',[tick,0,0,307,0,0,123,0,0,2,1,overflow]),record(b'ACTR',actor),record(b'FACT',faction)]
    if command:parts.append(record(b'UCMD',[1,tick,0,2,0x3F0,1,0,0] if not bad_command else [1]))
    parts.append(record(b'ENDT',[tick,len(parts)+1,1,1]));return b''.join(parts)

class TimelineTests(unittest.TestCase):
    def extract(self,payload):
        with tempfile.TemporaryDirectory(prefix='thandor-v649-')as d:
            source=Path(d)/'test.tgr';out=Path(d)/'out.json'
            source.write_bytes(b'THDTGR1\0'+struct.pack('<II',1,192)+bytes(176)+payload)
            p=subprocess.run([sys.executable,str(TOOL),str(source),str(out),'--through-tick','3'],capture_output=True,text=True)
            self.assertEqual(p.returncode,0,p.stderr)
            return json.loads(out.read_text())
    def test_valid_research_and_command_without_bad_army_attribution(self):
        r=self.extract(frame(1,0x80,command=True)+frame(2,0x40)+frame(3,0))
        self.assertEqual([x['research_flags']for x in r['model_research_changes']],[0x80,0x40,0])
        self.assertEqual(r['user_commands'][0]['words'][4],0x3F0)
        self.assertEqual(r['fact_changes'][0]['faction'],2)
        self.assertNotIn('faction',r['model_research_changes'][0])
    def test_overflow_frame_never_exports_its_command_or_research(self):
        r=self.extract(frame(1,0x80,overflow=1,command=True)+frame(2,0))
        self.assertEqual(r['user_commands'],[]);self.assertEqual(r['model_research_changes'],[])
        self.assertEqual(r['admission_read_stats']['excluded_frames'],1)
    def test_bad_bounded_command_frame_recovers_at_next_frame(self):
        r=self.extract(frame(1,0x80,bad_command=True,command=True)+frame(2,0))
        self.assertEqual(r['user_commands'],[]);self.assertEqual(r['model_research_changes'],[])
        self.assertEqual(r['last_included_tick'],2)
        self.assertEqual(r['admission_read_stats']['excluded_frames'],1)
if __name__=='__main__':unittest.main()
