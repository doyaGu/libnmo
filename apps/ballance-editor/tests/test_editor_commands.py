"""Direct authoring keeps incomplete documents editable and history persistent."""
from copy import deepcopy
from pathlib import Path
import sys
import tempfile
import unittest
from support import TOOLCHAIN_ROOT
from ballance.core import project
from ballance.core.nmo import Nmo
from ballance_editor.session import Session,read
from ballance_editor import commands
from test_editor import ROUTE


class CommandTests(unittest.TestCase):
    def make(self,tmp,route=ROUTE):
        source=Path(tmp)/'route.json';project.write_json(source,route)
        s=Session(source,Path(tmp)/'editor',None,Nmo('nmo'));s.prepare();return s

    def test_insert_duplicate_delete_and_persistent_undo_redo(self):
        with tempfile.TemporaryDirectory() as tmp:
            s=self.make(tmp)
            original=deepcopy(s.route)
            result=s.edit('insert','route/id/last',s.revision(),preset='curve')
            identifier=result['selection']
            self.assertIsNotNone(result['document_error']) # A sector must end on a narrow road.
            s.edit('insert',identifier,s.revision(),preset='road')
            self.assertIsNone(s.state()['document_error'])
            valid=deepcopy(s.route)
            road='route/id/edit-002'
            result=s.edit('duplicate',road,s.revision())
            duplicate=result['selection']
            self.assertNotEqual(duplicate,road)
            s.edit('delete',duplicate,s.revision())
            self.assertEqual(s.route,valid)
            s=Session(s.source,s.root,None,Nmo('nmo'));s.prepare()
            s.edit('undo',None,s.revision())
            self.assertIn(duplicate,[e['id'] for e in s.state()['model']['entities']])
            s.edit('redo',None,s.revision());self.assertEqual(s.route,valid)
            for _ in range(4):s.edit('undo',None,s.revision())
            self.assertEqual(s.route,original)
            self.assertEqual(read(s.source),original)

    def test_invalid_parameter_survives_restart_and_can_be_repaired(self):
        with tempfile.TemporaryDirectory() as tmp:
            s=self.make(tmp);p=deepcopy(ROUTE['sectors'][1][1]);p['connector']['to']['width']=7
            previous=(s.current/'sector2.obj').read_bytes()
            state=s.edit('set','route/id/joint',s.revision(),p,'piece')
            self.assertTrue(state['document_error'])
            self.assertEqual(read(s.root/'draft.json')['sectors'][1][1],p)
            self.assertEqual((s.current/'sector2.obj').read_bytes(),previous)
            s=Session(s.source,s.root,None,Nmo('nmo'));s.prepare()
            self.assertEqual(next(e for e in s.state()['model']['entities'] if e['id']=='route/id/joint')['piece'],p)
            with self.assertRaisesRegex(ValueError,'Repair'):s.validate(frames=100)
            state=s.edit('set','route/id/joint',s.revision(),ROUTE['sectors'][1][1],'piece')
            self.assertIsNone(state['document_error'])
            self.assertEqual(project.inspect(s.current)['status'],'passed')
            self.assertEqual(s.route,ROUTE)

    def test_new_edit_discards_redo_and_empty_sector_can_accept_elements(self):
        with tempfile.TemporaryDirectory() as tmp:
            s=self.make(tmp,{'schema_version':1,'sectors':[[{'id':'road','road':10}]]})
            s.edit('delete','route/id/road',s.revision())
            self.assertIn('editor/sector/0',[e['id'] for e in s.state()['model']['entities']])
            s.edit('insert','editor/sector/0',s.revision(),preset='road')
            self.assertIsNone(s.state()['document_error'])
            s.edit('undo',None,s.revision())
            s.edit('insert','editor/sector/0',s.revision(),preset='floor')
            self.assertEqual(s.state()['redo_count'],0)

    def test_sketch_insertion_keeps_connections_and_document_identity(self):
        route={'schema_version':1,'sectors':[[{'id':'start','road':10},{'id':'end','road':10}]]}
        sketch={'rail_path':{'segments':[{'bezier':[[7,0,0],[13,6,0],[20,6,0]]}]}}
        with tempfile.TemporaryDirectory() as tmp:
            s=self.make(tmp,route)
            result=s.edit('insert','route/id/start',s.revision(),sketch,preset='curve')
            self.assertIsNone(result['document_error'])
            self.assertEqual(result['selection'],'route/id/edit-001')
            self.assertEqual(s.route['sectors'][0][1]['rail_path'],sketch['rail_path'])
            self.assertEqual(s.route['sectors'][0][2]['id'],'end')
            edges={(c['from']['owner'],c['to']['owner']) for c in result['model']['connections']['connections']}
            self.assertIn(('route/id/start','route/id/edit-001'),edges)
            self.assertIn(('route/id/edit-001','route/id/end'),edges)
            with self.assertRaises(ValueError):
                s.edit('insert','route/id/start',s.revision(),{'road':10},preset='curve')
            with self.assertRaises(ValueError):
                s.edit('insert','route/id/start',s.revision(),dict(sketch,id='start'),preset='curve')
            self.assertEqual(read(s.source),route)

    def test_failed_sketch_extrusion_keeps_world_space_handles(self):
        route={'schema_version':1,'sectors':[[{'id':'start','road':10},{'id':'end','road':10}]]}
        tight={'rail_path':{'segments':[{'bezier':[[1,0,0],[2,3,0],[3,3,0]]}]}}
        with tempfile.TemporaryDirectory() as tmp:
            s=self.make(tmp,route)
            before=s.state()['model']
            expected=next(e['end']['position'] for e in before['entities'] if e['id']=='route/id/start')
            result=s.edit('insert','route/id/start',s.revision(),tight,preset='curve')
            self.assertIsNotNone(result['document_error'])
            inserted=next(e for e in result['model']['entities'] if e['id']==result['selection'])
            self.assertEqual(inserted['frame']['position'],expected)
            self.assertGreater(len(inserted['path']),2)
            repaired={'id':'edit-001','rail_path':{'segments':[{'bezier':[[7,0,0],[13,3,0],[20,3,0]]}]}}
            result=s.edit('set','route/id/edit-001',s.revision(),repaired,'piece')
            self.assertIsNone(result['document_error'])

    def test_template_copy_has_new_identity_and_independent_override(self):
        route={'schema_version':1,'definitions':{'r':{'pieces':[{'id':'r','road':10}]}},
               'sectors':[[{'id':'call','use':'r'}]],
               'overrides':{'route/id/call/repeat/0/id/r':{'set':{'road':20}}}}
        view,_=commands.view(route)
        copied,identifier=commands.change(route,view,'duplicate','route/id/call/repeat/0/id/r')
        self.assertEqual(identifier,'route/id/edit-001/repeat/0/id/r')
        self.assertEqual(copied['overrides'][identifier],route['overrides']['route/id/call/repeat/0/id/r'])
        copied['overrides'][identifier]['set']['road']=30
        self.assertEqual(route['overrides']['route/id/call/repeat/0/id/r']['set']['road'],20)
        remaining,_=commands.change(copied,commands.view(copied)[0],'delete',identifier)
        self.assertNotIn(identifier,remaining['overrides'])


if __name__=='__main__':unittest.main()
