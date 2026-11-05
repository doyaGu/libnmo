"""Shared spatial queries and author-owned attachment constraints."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ballance.core.space import Space, Volume, bounds, inflate
from ballance.route.layout import Layout, Part
from ballance.route import scenery, attachments
from ballance.route.build import write_sector
from ballance.route.manifest import build_manifest
from ballance.kit.rails import Ring
from ballance.verify.scenery import check
from ballance.view.scene import load_project
from test_scenery import scenery_assets, returning_rail


class SpaceTests(unittest.TestCase):
    def test_states_sources_and_sweeps_share_one_query(self):
        space = Space()
        segment = ((0, 0, 0), (20, 0, 20))
        space.add(
            Volume(
                'rail/a',
                'ball clearance',
                inflate(bounds(segment), 2),
                segment=segment,
                radius=2,
                source={'entity_id': 'rail/a', 'source': {'pointer': '/sectors/0/1'}},
            )
        )
        space.add(Volume('balloon', 'departure', (-5, 5, 0, 10, -5, 5), 'finish'))
        # Same broad-phase box, but away from the actual diagonal sweep.
        self.assertEqual(space.query([(0, 1, -1, 1, 18, 19)], states={'play'}), [])
        hits = space.query([(0, 1, 0, 1, 0, 1)])
        self.assertEqual({h['owner'] for h in hits}, {'rail/a', 'balloon'})
        self.assertEqual(hits[0]['source']['pointer'], '/sectors/0/1')
        self.assertEqual(len(space.query([(0, 1, 0, 1, 0, 1)], states={'play'})), 1)
        self.assertEqual(space.query([(0, 1, 0, 1, 0, 1)], ignore=('rail/a', 'balloon')), [])

    def test_reservations_move_without_leaving_phantom_obstacles(self):
        space = Space()
        space.add(Volume('lamp', 'separation', (0, 2, 0, 2, 0, 2)))
        space.remove('lamp')
        space.add(Volume('lamp', 'separation', (10, 12, 0, 2, 0, 2)))
        self.assertEqual(space.query([(0, 1, 0, 1, 0, 1)]), [])
        self.assertEqual(space.query([(10, 11, 0, 1, 0, 1)])[0]['owner'], 'lamp')
        self.assertEqual(len(space.data()), 1)
        # A flat mesh face inside a volume must not disappear as zero volume.
        self.assertTrue(space.query([(11, 11, 1, 1, 1, 1)]))

    def test_invalid_bounds_and_sweep_envelopes_fail(self):
        for box in ((1, 0, 0, 1, 0, 1), (0, float('nan'), 0, 1, 0, 1)):
            with self.assertRaises(ValueError):
                Volume('a', 'b', box)
        with self.assertRaisesRegex(ValueError, 'contain'):
            Volume('a', 'b', (0, 1, 0, 1, 0, 1), segment=((0, 0, 0), (10, 0, 0)), radius=1)


class AttachmentTests(unittest.TestCase):
    def _layout(self):
        return Layout(
            {
                'schema_version': 1,
                'start': {'heading': '+x'},
                'sectors': [
                    [
                        {'id': 'road', 'road': 10},
                        {'id': 'platform', 'area': [20, 20]},
                        {'id': 'exit', 'road': 10},
                    ]
                ],
            }
        )

    def _export(self, folder, layout, assets, lamps, poles):
        for i, sector in enumerate(layout.sectors, 1):
            write_sector(sector, i, folder)
        manifest = build_manifest([], assets, layout, 'Attachment', lamps, poles)
        attachments.bind_objects(layout, manifest)
        (folder / 'manifest.json').write_text(json.dumps(manifest))
        return json.loads(json.dumps(manifest))

    def test_parent_contact_is_checked_even_if_no_rail_is_nearby(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            layout = self._layout()
            assets = scenery_assets(folder)
            lamps, layout.pillars, poles, _ = scenery.fit(layout, assets, [], [])
            manifest = self._export(folder, layout, assets, lamps, poles)
            self.assertEqual(check(load_project(folder), layout, layout.scenery_bindings), [])
            target = next(
                o for o in manifest['scenes'][0]['objects'] if o['name'] == 'Floor_Columns_01'
            )
            target['transform']['position'][0] -= 12.5
            (folder / 'manifest.json').write_text(json.dumps(manifest))
            findings = check(load_project(folder), layout, layout.scenery_bindings)
            self.assertTrue(
                any(
                    f.get('rule') == 'capital has no contact with its owning platform'
                    for f in findings
                )
            )

    def test_parent_hole_is_not_filled_by_its_bounding_box(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            layout = self._layout()
            assets = scenery_assets(folder)
            lamps, layout.pillars, poles, _ = scenery.fit(layout, assets, [], [])
            self._export(folder, layout, assets, lamps, poles)
            # Keep the same broad footprint but remove the supporting top.
            (folder / 'sector1.obj').write_text('v 0 100 0\nv 1 100 0\nv 0 100 1\nf 1 2 3\n')
            findings = check(load_project(folder), layout, layout.scenery_bindings)
            self.assertTrue(
                any(
                    f.get('rule') == 'capital has no contact with its owning platform'
                    for f in findings
                )
            )

    def test_lamp_and_column_cannot_be_separated_after_export(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            layout = returning_rail(80)
            assets = scenery_assets(folder)
            lamps, layout.pillars, poles, _ = scenery.fit(
                layout, assets, scenery.towers(layout), []
            )
            manifest = self._export(folder, layout, assets, lamps, poles)
            target = next(o for o in manifest['scenes'][0]['objects'] if o['name'] == 'Tower_01')
            target['transform']['position'] = list(target['transform']['position'])
            target['transform']['position'][0] += 5
            (folder / 'manifest.json').write_text(json.dumps(manifest))
            findings = check(load_project(folder), layout, layout.scenery_bindings)
            self.assertTrue(any('share their attachment' in f.get('rule', '') for f in findings))

    def test_locked_lamp_fails_instead_of_moving_and_unknown_rules_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            layout = returning_rail(-12)
            lamps = scenery.towers(layout)
            assets = scenery_assets(Path(tmp))
            with self.assertRaisesRegex(ValueError, 'cannot satisfy'):
                scenery.fit(layout, assets, lamps, [], {'marker/start/lamp/0': {'mode': 'locked'}})
            with self.assertRaisesRegex(ValueError, 'unknown scenery rule'):
                scenery.fit(layout, assets, lamps, [], {'misspelled': {}})

    def test_omission_is_only_allowed_explicitly_and_keeps_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            layout = returning_rail(-12)
            lamps = scenery.towers(layout)
            assets = scenery_assets(Path(tmp))
            fitted, _, _, changes = scenery.fit(
                layout,
                assets,
                lamps,
                [],
                {'marker/start/lamp/0': {'mode': 'locked', 'allow_omit': True}},
            )
            omitted = next(b for b in layout.scenery_bindings if b['id'] == 'marker/start/lamp/0')
            self.assertIsNone(omitted['position'])
            self.assertEqual(omitted['action'], 'omitted')
            self.assertLess(len(fitted), len(lamps))
            self.assertEqual(
                next(c for c in changes if c['action'] == 'omitted')['id'], omitted['id']
            )

    def test_finish_pair_is_preserved_and_asymmetry_is_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            layout = self._layout()
            assets = scenery_assets(folder)
            poles = scenery.sticks(layout, [[0, 24, 3.7], [0, -24, 3.7]])
            lamps, layout.pillars, poles, _ = scenery.fit(layout, assets, [], poles)
            manifest = self._export(folder, layout, assets, lamps, poles)
            self.assertEqual(check(load_project(folder), layout, layout.scenery_bindings), [])
            obj = next(o for o in manifest['scenes'][0]['objects'] if o['name'] == 'Stick_01')
            obj['transform']['position'] = list(obj['transform']['position'])
            obj['transform']['position'][2] += 2
            (folder / 'manifest.json').write_text(json.dumps(manifest))
            findings = check(load_project(folder), layout, layout.scenery_bindings)
            self.assertTrue(any('symmetric' in f.get('rule', '') for f in findings))
            with self.assertRaisesRegex(ValueError, 'symmetric'):
                scenery.fit(self._layout(), assets, [], [[0, 0, 24], [0, 0, -22]])

    def test_attached_search_cannot_switch_to_an_unrelated_platform(self):
        layout = self._layout()
        with tempfile.TemporaryDirectory() as tmp:
            assets = scenery_assets(Path(tmp))
            with self.assertRaisesRegex(ValueError, 'unknown parent'):
                scenery.fit(
                    layout,
                    assets,
                    [],
                    [],
                    {'route/id/platform/pillar/0': {'parent': 'another-platform'}},
                )
            with self.assertRaisesRegex(ValueError, 'discard its attachment'):
                scenery.fit(
                    layout, assets, [], [], {'route/id/platform/pillar/0': {'mode': 'nearby'}}
                )

    def test_nearby_floor_cannot_rescue_a_column_bound_to_a_blocked_platform(self):
        layout = Layout(
            {
                'schema_version': 1,
                'start': {'heading': '+x'},
                'sectors': [
                    [
                        {'id': 'road', 'road': 10},
                        {'id': 'platform', 'area': [15, 15]},
                        {'id': 'exit', 'road': 10},
                    ]
                ],
            }
        )
        layout.pillars = [[29.89, -5, 0]]

        def other_floor(mesh, x, z):
            for y, up in ((0, 1), (-5, -1)):
                mesh.quad(
                    'Floor_Side',
                    (0, up, 0),
                    *((p, (0, 0)) for p in ((20, y, -10), (40, y, -10), (40, y, 30), (20, y, 30))),
                )

        layout.parts.append(Part(len(layout.parts), 1, 'other', extras=[(other_floor, (0, 0))]))
        layout.parts.append(
            Part(
                len(layout.parts),
                1,
                'rail',
                segments=[(Ring(15, -14, z, 1, 0), Ring(45, -14, z, 1, 0)) for z in (-4, 0, 4)],
            )
        )
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(ValueError, 'route/id/platform/pillar/0'):
                scenery.fit(layout, scenery_assets(Path(tmp)), [], [])


if __name__ == '__main__':
    unittest.main()
