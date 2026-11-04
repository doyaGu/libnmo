"""Per-body constraints must not disappear behind another body's envelope."""
import csv
import math
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from ballance.core.motion import place
from ballance.route import motions
from ballance.route.layout import Part
from ballance.verify.motions import check
from ballance.verify.motion_trace import inspect


def fixture(kind):
    return SimpleNamespace(parts=[Part(0,1,'module',modules=[(kind,1,[30,20,50],0)])],
                           sectors=[None],balloon=None,mechanism_settings={})


class AssemblyMotionTests(unittest.TestCase):
    def test_seven_native_contracts_are_supported_and_unbounded_is_explicit(self):
        for number in (1,3,8,19,29,30,34):
            kind=f'P_Modul_{number:02d}'
            recipe=motions.catalog()[kind]
            report=check(fixture(kind),None)
            self.assertEqual(report['unsupported'],[])
            self.assertEqual(report['modeled'],1)
            self.assertEqual(bool(report['unbounded_motion']),number in (1,3,34))
            if number in (1,3,34):self.assertEqual(report['status'],'review')
            self.assertTrue(all(not j['limits_enabled'] for j in recipe['joints']))
        plate=motions.catalog()['P_Modul_29']
        self.assertIn('split',plate['states'])
        broken={j['id'] for j in plate['joints'] if j.get('breaks_in_play')}
        self.assertEqual(len(broken),1)
        for contract in plate['contracts'].values():
            if contract['physical']:
                self.assertEqual(contract['type'],'reach')
                self.assertFalse(broken.intersection(contract['joint_path']))

    def test_unlimited_slider_preserves_axis_and_orientation_even_with_free_box(self):
        kind='P_Modul_34';recipe=motions.catalog()[kind]
        body='P_Modul_34_Schiebestein'
        matrix=recipe['bodies'][body]['matrix'][:]
        matrix[12:15]=place(matrix[12:15],(30,20,50),0)
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'trace.csv'
            def write(second):
                with path.open('w',newline='') as f:
                    w=csv.writer(f);w.writerow(['frame','id','name',*[f'm{k}' for k in range(16)]])
                    w.writerow([1,42,body,*matrix]);w.writerow([2,42,body,*second])
            # Disabled -1/+1 limits must not reject long axial travel.
            moved=matrix[:];moved[13]-=100
            write(moved)
            result=inspect(path,fixture(kind))
            self.assertEqual(result['envelope_escapes'],[])
            self.assertTrue(result['unbounded_motion'])
            # The free box does not grant its slider transverse freedom.
            moved[12]+=2
            write(moved)
            self.assertEqual(inspect(path,fixture(kind))['envelope_escapes'][0]['frame'],2)
            moved=matrix[:];moved[0]*=-1;moved[2]*=-1
            write(moved)
            self.assertTrue(inspect(path,fixture(kind))['envelope_escapes'])

    def test_pose_cannot_use_another_fixed_body_as_cover(self):
        recipe=motions.catalog()['P_Modul_01'];name='P_Modul_01_Filler'
        matrix=recipe['bodies'][name]['matrix'][:]
        matrix[12:15]=place(matrix[12:15],(30,20,50),0)
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'trace.csv'
            with path.open('w',newline='') as f:
                w=csv.writer(f);w.writerow(['frame','id','name',*[f'm{k}' for k in range(16)]])
                w.writerow([1,42,name,*matrix]);matrix[12]+=3;w.writerow([2,42,name,*matrix])
            self.assertTrue(inspect(path,fixture('P_Modul_01'))['envelope_escapes'])


if __name__=='__main__':unittest.main()
