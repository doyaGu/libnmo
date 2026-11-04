"""Complete resource coverage and the author-facing native placement adapters."""

import copy
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from ballance.extract.elements import every_kind
from ballance.game.catalog import ELEMENT_KINDS
from ballance.game.resources import catalog, elements, mechanisms, files
from ballance.route.layout import Layout
from ballance.route.templates import expand, resource_piece
from ballance.route.build import build
from ballance.route.assets import Assets
from ballance.verify.resources import check
from ballance.verify.motions import check as check_motion


class CatalogTests(unittest.TestCase):
    def test_every_export_and_native_element_has_a_resource(self):
        descriptions=catalog()
        self.assertEqual(set(elements()),set(ELEMENT_KINDS))
        exported=every_kind()
        self.assertEqual(len(exported),len(set(exported)))
        self.assertEqual({p['name'] for s in descriptions.values() for p in s['parts']},set(exported))
        shared={}
        for spec in descriptions.values():
            self.assertTrue(files(spec))
            for part in spec['parts']:
                if part['name'] in shared:
                    self.assertEqual(part['extract'],shared[part['name']])
                shared[part['name']]=part['extract']
        self.assertEqual(len(mechanisms()),13)

    def test_all_documented_route_entries_expand_without_changing_the_adapter(self):
        for name,spec in catalog().items():
            placement=spec.get('placement',{})
            if 'route' not in placement:
                continue
            with self.subTest(name=name):
                self.assertEqual(resource_piece({'resource':name}),placement['route'])
        self.assertEqual(resource_piece({'resource':'FanBlock','rise':12}),{'fan':12})
        self.assertEqual(resource_piece({'resource':'RailSplit','drop':8}),{'fall':8,'split':True})
        self.assertEqual(resource_piece({'resource':'LogClimb','length':100}),{'log':100,'climb':True})
        for name in ('StartPad','ResetPoint','RailingClamp','SkyLayer'):
            with self.assertRaisesRegex(ValueError,'not a route resource'):
                resource_piece({'resource':name})
        for count in (0,3,True,2.5):
            with self.assertRaisesRegex(ValueError,'count'):
                resource_piece({'resource':'Pusher','count':count})

    def test_static_and_changer_routes_match_legacy_geometry_in_each_heading(self):
        cases=[([{'road':10}],[{'resource':name}],[{'road':15}]) for name in
               ('ArchedPlank','WaveWalkway','LongWaveWalkway','Twist','Jump','PaperChanger','StoneChanger','WoodChanger','BoxMaze')]
        cases += [([{'road':10},{'rail':20}],[{'resource':'Cradle'}],[{'road':15}]),
                  ([{'road':10},{'rail':20},{'fall':10}],[{'resource':'Diamond'}],[{'road':15}]),
                  ([{'road':10}],[{'resource':'Octagon','branches':[{'side':'left','route':[{'road':10}]}]}],[{'road':15}]),
                  ([{'road':10,'width':'wide'}],[{'resource':'Spiral'}],[{'road':15,'width':'wide'},{'road':15}]),
                  ([{'road':10},{'rail':20},{'fall':10}],[{'resource':'Log_2'},{'fall':10}],[{'area':[25,25]},{'road':15}]),
                  ([{'road':10},{'rail':20},{'fall':10}],[{'resource':'Trough_2'},{'fall':10}],[{'area':[25,25]},{'road':15}])]
        for before,items,after in cases:
            for heading in ('+x','-x','+z','-z'):
                with self.subTest(items=items,heading=heading):
                    route={'start':{'heading':heading},'sectors':[before+items+after]}
                    legacy=copy.deepcopy(route);legacy['sectors']=[[resource_piece(p) for p in s] for s in legacy['sectors']]
                    a,b=Layout(route),Layout(legacy)
                    self.assertEqual(a.points,b.points)
                    self.assertEqual(a.module_floors,b.module_floors)
                    self.assertEqual(a.elements,b.elements)

    def test_element_aliases_work_on_grid_and_free_surfaces_and_mirror(self):
        names=('ExtraLife','ExtraPoint','Box','Dome','PaperBall','StoneBall','WoodBall')
        for name in names:
            for piece in ({'road':30},{'floor_path':{'segments':[{'line':[30,0,0]}]}}):
                item={**piece,'elements':[{'resource':name,'at':[10,0]}]}
                a=Layout({'sectors':[[{'road':10},item,{'road':15}]]})
                b=Layout({'sectors':[[{'road':10},resource_piece(item),{'road':15}]]})
                self.assertEqual(a.elements,b.elements)
                self.assertTrue(any(e[0]==catalog()[name]['placement']['element'] for e in a.elements))
        mirrored=expand({'definitions':{'reward':{'pieces':[{'road':20,'elements':[{'resource':'ExtraPoint','at':[10,1]}]}]}},
                         'sectors':[[{'use':'reward','mirror':True}]]})[0][0]
        self.assertEqual(mirrored['elements'][0],{'kind':'P_Extra_Point','at':[10,-1]})
        with self.assertRaisesRegex(ValueError,'not an element'):
            resource_piece({'road':20,'elements':[{'resource':'PaperChanger','at':[10,0]}]})

    @unittest.skipUnless(os.environ.get('BALLANCE_ELEMENTS') and os.environ.get('BALLANCE'),'requires native assets')
    def test_remaining_mechanisms_bind_native_parts_without_claiming_motion_coverage(self):
        game=Path(os.environ['BALLANCE']);elements_path=Path(os.environ['BALLANCE_ELEMENTS'])
        names=('Pusher','Lift','Swing','Flaps','Plates','Seesaw','StoneBox','BoxMaze')
        # Keep this probe about module assembly frames, not automatic lamp fitting.
        with tempfile.TemporaryDirectory() as tmp, patch('ballance.route.scenery.towers',return_value=[]):
            for name in names:
                with self.subTest(resource=name):
                    out=Path(tmp)/name
                    route={'scenery':{'finish_poles':[]},'sectors':[[{'road':20},{'resource':name},{'road':20}]]}
                    build(route,out,elements_path,name)
                    self.assertEqual(check(out,game)['status'],'passed')
                    record=json.loads((out/'resources.json').read_text())
                    self.assertEqual(record['unbound_native_objects'],[])
                    motion=check_motion(Layout(route),Assets.existing(out),game)
                    self.assertNotEqual(motion['status'],'failed')
                    self.assertEqual(motion['unsupported'],[])
                    if name in ('Pusher','Lift','StoneBox','BoxMaze'):
                        self.assertTrue(motion['unbounded_motion'])
                        self.assertEqual(motion['status'],'review')
                    if name=='Pusher':
                        pushers=[i for i in record['instances'] if i['resource']=='Pusher']
                        floors=[next(c['objects'] for c in i['components'] if c['part']=='Floor_Modul_01') for i in pushers]
                        self.assertEqual(len(floors),2)
                        self.assertEqual(floors[0],floors[1])
                        self.assertTrue(floors[0])
                    if name=='BoxMaze':
                        stones=[i for i in record['instances'] if i['resource']=='StoneBox']
                        self.assertEqual(len(stones),3)
                        self.assertTrue(all(i['variant']=='embedded' and len(i['components'])==1 for i in stones))
                    record['unbound_native_objects']=['injected_orphan']
                    (out/'resources.json').write_text(json.dumps(record))
                    self.assertEqual(check(out,game)['status'],'failed')


if __name__=='__main__':unittest.main()
