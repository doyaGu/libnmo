"""Tests of the Python binding against the shared libnmo and the test files.

Run from the repository root with NMO_LIBRARY set to the shared library:
    NMO_LIBRARY=build/libnmo.dylib PYTHONPATH=bindings/python \
        python3 -m unittest discover -s bindings/python/tests
"""

import os
import struct
import tempfile
import unittest

import nmo
from nmo import _native

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BOX = os.path.join(ROOT, "data", "Ballance", "P_Box.nmo")
BTE = os.path.join(ROOT, "data", "Demo", "BTE.cmo")


def need(path):
    if not os.path.exists(path):
        raise unittest.SkipTest(f"{path} is not here")


class NativeLayout(unittest.TestCase):
    def test_structs_match_the_c_defaults(self):
        # A field out of place would put these at the wrong offsets
        options = _native.lib.nmo_load_options_default()
        self.assertEqual(0, options.profile)
        self.assertEqual(4096, options.max_included_name_len)
        self.assertEqual(512 * 1024 * 1024, options.max_included_file_size)
        self.assertEqual(512 * 1024 * 1024, options.max_unpacked_size)
        self.assertEqual(1 << 20, options.max_object_count)
        self.assertFalse(options.collect_perf_stats)
        self.assertIsNone(options.perf_stats)
        gltf = _native.lib.nmo_gltf_export_options_default()
        self.assertTrue(gltf.embed_textures)
        self.assertTrue(gltf.apply_color_key)

    def test_version(self):
        self.assertRegex(nmo.version(), r"^\d+\.\d+\.\d+")


class Reading(unittest.TestCase):
    def test_objects_classes_and_fields(self):
        need(BOX)
        with nmo.open(BOX) as doc:
            self.assertEqual(8, doc.info.file_version)
            self.assertEqual(doc.info.object_count, len(doc))
            self.assertTrue(doc.info.crc_ok)
            self.assertFalse(doc.partial)
            names = {obj.name for obj in doc}
            self.assertIn("P_Box_MF", names)
            box = doc.objects("CK3dEntity")[0]
            self.assertEqual("CK3dObject", box.class_name)
            self.assertTrue(box.is_a("CKBeObject"))
            self.assertFalse(box.is_a("CKMesh"))
            self.assertEqual([box], doc.objects("CK3dObject", derived=False))
            self.assertEqual([box], doc.objects(name="P_Box_MF"))
            self.assertEqual(box, doc[box.id])
            fields = box.fields
            self.assertIn("world_matrix", fields)
            self.assertIn("priority", fields)
            snapshot = box.to_dict()
            self.assertEqual(box.id, snapshot["id"])
            self.assertEqual("CK3dObject", snapshot["class_name"])
            with self.assertRaises(KeyError):
                doc[0x7FFFFFFF]
            with self.assertRaises(ValueError):
                doc.objects("NoSuchClass")

    def test_object_table_profile_reads_states_on_request(self):
        need(BOX)
        with nmo.open(BOX, profile="objects") as doc:
            self.assertTrue(doc.partial)
            box = doc.objects("CK3dEntity")[0]
            self.assertEqual({}, box.fields)
            box.load_state()
            self.assertIn("world_matrix", box.fields)
            with self.assertRaises(nmo.NmoError):
                box.set(priority=1)

    def test_errors_carry_the_status(self):
        with self.assertRaises(nmo.NmoError) as caught:
            nmo.open(os.path.join(ROOT, "no such file.nmo"))
        self.assertNotEqual(0, caught.exception.status)
        with self.assertRaises(ValueError):
            nmo.open(BOX, profile="everything")


class Writing(unittest.TestCase):
    def test_fields_written_are_saved(self):
        need(BOX)
        with tempfile.TemporaryDirectory() as scratch:
            out = os.path.join(scratch, "box.nmo")
            matrix = "(1, 0, 0, 0; 0, 1, 0, 0; 0, 0, 1, 0; 7, 8, 9, 1)"
            with nmo.open(BOX) as doc:
                box = doc.objects("CK3dEntity")[0]
                self.assertEqual(2, box.set(priority=5, world_matrix=matrix))
                self.assertEqual(5, box.fields["priority"])
                with self.assertRaises(KeyError):
                    box.set(no_such_field=1)
                doc.save(out)
            with nmo.open(out) as again:
                box = again.objects("CK3dEntity")[0]
                self.assertEqual(5, box.fields["priority"])
                self.assertEqual(matrix, box.fields["world_matrix"])

    def test_gltf_export(self):
        need(BTE)
        with tempfile.TemporaryDirectory() as scratch:
            out = os.path.join(scratch, "bte.glb")
            with nmo.open(BTE) as doc:
                stats = doc.export_gltf(out)
            self.assertGreater(stats["nodes"], 0)
            self.assertGreater(stats["images_embedded"], 0)
            with open(out, "rb") as glb:
                magic, version, length = struct.unpack("<III", glb.read(12))
            self.assertEqual(0x46546C67, magic)
            self.assertEqual(2, version)
            self.assertEqual(os.path.getsize(out), length)

    def test_closed_document(self):
        need(BOX)
        doc = nmo.open(BOX)
        box = next(iter(doc))
        doc.close()
        with self.assertRaises(ValueError):
            len(doc)
        with self.assertRaises(ValueError):
            box.name


if __name__ == "__main__":
    unittest.main()
