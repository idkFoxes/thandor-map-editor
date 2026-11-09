import copy,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from audit_inferred_production_inputs import analyze,parse_source_metadata

def fixture():
    old=[0]*41;old[0]=49;old[1]=0xffffffff;old[2]=0xdeadbeef
    new=old.copy();new[26]=330;new[28]=439;new[38]=0x100
    before=[0]*74;before[0]=1;before[2]=28000
    after=before.copy();after[2]=18400
    def frame(t,w,f):
        tick=[0]*12;tick[0]=t;tick[9]=2
        return {'tick':tick,'actors':{49:w},'factions':{1:f}}
    return [frame(475,old,before),frame(476,new,after)]
MODELS={49:{'faction':1,'class':11,'asset':300}}
PRODUCTS={330:{'cost':9600,'duration':439}}
class InferredInputs(unittest.TestCase):
    def test_corroborated_root_and_balance_use_source_identity(self):
        r=analyze(fixture(),MODELS,PRODUCTS);self.assertEqual(len(r['inferred_queue_inputs']),1)
        event=r['inferred_queue_inputs'][0]
        self.assertEqual((event['tick'],event['faction_from_source'],event['asset_from_model_slot']),(476,1,330))
        self.assertIn('not a captured UCMD',event['classification'])
    def test_balance_change_must_match_exact_cost(self):
        frames=fixture();frames[1]['factions'][1][2]+=1
        r=analyze(frames,MODELS,PRODUCTS);self.assertEqual(r['inferred_queue_inputs'],[])
        self.assertIn('balance delta',r['excluded_candidates'][0]['reason'])
    def test_duration_must_match_source_and_no_unknown_products(self):
        for products in ({}, {330:{'cost':9600,'duration':438}}):
            self.assertEqual(analyze(fixture(),MODELS,products)['inferred_queue_inputs'],[])
    def test_ai_or_unresolved_actor_never_becomes_human_input(self):
        frames=fixture();frames[1]['tick'][9]=0
        self.assertEqual(analyze(frames,MODELS,PRODUCTS)['inferred_queue_inputs'],[])
        self.assertEqual(analyze(fixture(),{},PRODUCTS)['inferred_queue_inputs'],[])
    def test_counter_samples_have_gaps_and_only_admitted_batch_range(self):
        frames=fixture();third=copy.deepcopy(frames[-1]);third['tick'][0]=477;third['actors'][49][27]=5
        fourth=copy.deepcopy(third);fourth['tick'][0]=478;fourth['actors'][49][27]=11
        r=analyze(frames+[third,fourth],MODELS,PRODUCTS)
        self.assertEqual(r['observed_callback_batch_intervals'],[{'from_tick':477,'through_tick':477,'batch':5}])
    def test_source_metadata_is_explicitly_required(self):
        with self.assertRaises(ValueError):parse_source_metadata('')
        models,products=parse_source_metadata('INITIAL_MAP checked=1 exact_xy=1 reversed=1\nINITIAL_MODEL id=49 arm=300 faction=1 class=11\nCATALOG_BUILD arm=330 cost=9600 duration=439\n')
        self.assertEqual(models,MODELS);self.assertEqual(products,PRODUCTS)
if __name__=='__main__':unittest.main()
