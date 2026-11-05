"""Gameplay recovery and partial-run reporting contracts."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ballance.core import project


class RecoveryTests(unittest.TestCase):
    def test_recovery_requires_death_all_bodies_stable_and_checkpoint_respawn(self):
        import csv
        from ballance.route.motions import catalog
        from ballance.verify.recovery import plates
        recipe = catalog()['P_Modul_29']
        names = [n for n,c in recipe['contracts'].items() if c['physical'] and c['type'] != 'fixed']
        instance = {'id': 'plates', 'position': [0,0,0], 'yaw': 0}
        trace = {'observed_states': [{'mechanism': 'plates', 'first_frame': 10}],
                 'bodies': [{'mechanism': 'plates', 'body': n, 'runtime_ids': [str(i)]}
                            for i,n in enumerate(names)]}
        with tempfile.TemporaryDirectory() as tmp:
            path, log = Path(tmp)/'physics.csv', Path(tmp)/'run.log'
            def write(missing=None, respawn=True, gap=False):
                with path.open('w', newline='') as f:
                    w=csv.writer(f);w.writerow(['frame','id','name',*[f'm{k}' for k in range(16)]])
                    for frame in range(21,51):
                        if gap and frame==35:continue
                        for i,name in enumerate(names):
                            if name!=missing:w.writerow([frame,i,name,*recipe['bodies'][name]['matrix']])
                        if respawn:w.writerow([frame,100,'Ball_Stone',1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1])
            log.write_text('[20]  Send Message#12 sends "Ball Off" to All_Gameplay#22\n')
            write();r=plates(path,log,trace,instance,recipe,[0,0,0],'Stone')
            self.assertEqual(r['status'],'passed');self.assertEqual(r['restored_frame'],21)
            write(missing=names[0]);self.assertEqual(plates(path,log,trace,instance,recipe,[0,0,0],'Stone')['status'],'failed')
            write(respawn=False);self.assertEqual(plates(path,log,trace,instance,recipe,[0,0,0],'Stone')['status'],'failed')
            write(gap=True);self.assertEqual(plates(path,log,trace,instance,recipe,[0,0,0],'Stone')['status'],'failed')
            write();log.write_text('');self.assertEqual(plates(path,log,trace,instance,recipe,[0,0,0],'Stone')['status'],'failed')


class PartialRunTests(unittest.TestCase):
    def test_setup_checkpoint_is_reported_separately_without_hiding_falls(self):
        from ballance.verify.play import Play, steer_from
        from ballance.verify.report import _play_report
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            project.write_json(folder/'route_sectors.json', [{}]*7)
            start = steer_from(5)
            result = Play(frames=10000)
            result.events = [(528, 'Checkpoint reached'), (start, 'Checkpoint reached'),
                             (5000, 'Checkpoint reached'), (8000, 'Level_Finish')]
            result.trace = [(start, (0,0,0)), (8000, (1,0,1))]
            report = _play_report(result, folder, 5)
            self.assertEqual(report['status'], 'passed')
            self.assertEqual(report['setup_checkpoints'], [528])
            self.assertEqual(report['checkpoints'], [start,5000])
            result.events.insert(0,(300,'Ball Off'))
            self.assertEqual(_play_report(result, folder, 5)['status'], 'failed')
