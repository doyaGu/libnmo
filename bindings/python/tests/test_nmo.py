"""Tests of the Python binding against the shared libnmo.

Run from the repository root with NMO_LIBRARY set to the shared library:
    NMO_LIBRARY=build/libnmo.dylib PYTHONPATH=bindings/python \
        python3 -m unittest discover -s bindings/python/tests

data/box.cmo, a textured cube, is what the tests read, so they run where the
Virtools samples are not (CI). libnmo's project layer made it from box.json:
    nmo patch apply --project bindings/python/tests/data/box.json \
        -o bindings/python/tests/data/box.cmo
The Corpus cases also read Virtools samples under data/ when they are there.
"""

import os
import struct
import tempfile
import unittest

import nmo
from nmo import _native

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
SAMPLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "box.cmo")
P_BOX = os.path.join(ROOT, "data", "Ballance", "P_Box.nmo")
BTE = os.path.join(ROOT, "data", "Demo", "BTE.cmo")


def need(path):
    if not os.path.exists(path):
        raise unittest.SkipTest(f"{path} is not here")


def read_glb_header(path):
    with open(path, "rb") as glb:
        return struct.unpack("<III", glb.read(12))


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
        with nmo.open(SAMPLE) as doc:
            self.assertEqual(9, doc.info.file_version)
            self.assertEqual(doc.info.object_count, len(doc))
            self.assertTrue(doc.info.crc_ok)
            self.assertFalse(doc.partial)
            names = {obj.name for obj in doc}
            self.assertTrue({"Box", "Box_Mesh", "Box_Texture", "Camera"} <= names)
            box = doc.objects("CK3dObject")[0]
            self.assertEqual("Box", box.name)
            self.assertEqual("CK3dObject", box.class_name)
            self.assertEqual({"Box", "Camera"}, {obj.name for obj in doc.objects("CK3dEntity")})
            self.assertTrue(box.is_a("CKBeObject"))
            self.assertFalse(box.is_a("CKMesh"))
            self.assertEqual([box], doc.objects("CK3dObject", derived=False))
            self.assertEqual([box], doc.objects(name="Box"))
            self.assertEqual(box, doc[box.id])
            fields = box.fields
            self.assertTrue(fields["world_matrix"].endswith("1, 2, 3, 1)"))
            self.assertIn("priority", fields)
            snapshot = box.to_dict()
            self.assertEqual(box.id, snapshot["id"])
            self.assertEqual("CK3dObject", snapshot["class_name"])
            with self.assertRaises(KeyError):
                doc[0x7FFFFFFF]
            with self.assertRaises(ValueError):
                doc.objects("NoSuchClass")

    def test_object_table_profile_reads_states_on_request(self):
        with nmo.open(SAMPLE, profile="objects") as doc:
            self.assertTrue(doc.partial)
            box = doc.objects("CK3dObject")[0]
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
            nmo.open(SAMPLE, profile="everything")


class Writing(unittest.TestCase):
    def test_fields_written_are_saved(self):
        with tempfile.TemporaryDirectory() as scratch:
            out = os.path.join(scratch, "box.cmo")
            matrix = "(1, 0, 0, 0; 0, 1, 0, 0; 0, 0, 1, 0; 7, 8, 9, 1)"
            with nmo.open(SAMPLE) as doc:
                box = doc.objects("CK3dObject")[0]
                self.assertEqual(2, box.set(priority=5, world_matrix=matrix))
                self.assertEqual(5, box.fields["priority"])
                with self.assertRaises(KeyError):
                    box.set(no_such_field=1)
                doc.save(out)
            with nmo.open(out) as again:
                box = again.objects("CK3dObject")[0]
                self.assertEqual(5, box.fields["priority"])
                self.assertEqual(matrix, box.fields["world_matrix"])

    def test_gltf_export(self):
        with tempfile.TemporaryDirectory() as scratch:
            out = os.path.join(scratch, "box.glb")
            with nmo.open(SAMPLE) as doc:
                stats = doc.export_gltf(out)
            self.assertEqual(1, stats["meshes"])
            self.assertEqual(12, stats["triangles"])
            self.assertEqual(1, stats["images_embedded"])
            magic, version, length = read_glb_header(out)
            self.assertEqual(0x46546C67, magic)
            self.assertEqual(2, version)
            self.assertEqual(os.path.getsize(out), length)

    def test_closed_document(self):
        doc = nmo.open(SAMPLE)
        box = next(iter(doc))
        doc.close()
        with self.assertRaises(ValueError):
            len(doc)
        with self.assertRaises(ValueError):
            box.name


class Corpus(unittest.TestCase):
    """The same calls on files Virtools wrote."""

    def test_reads_a_virtools_file(self):
        need(P_BOX)
        with nmo.open(P_BOX) as doc:
            self.assertEqual(8, doc.info.file_version)
            self.assertTrue(doc.info.crc_ok)
            box = doc.objects("CK3dEntity")[0]
            self.assertEqual("P_Box_MF", box.name)
            self.assertIn("world_matrix", box.fields)

    def test_exports_a_virtools_scene(self):
        need(BTE)
        with tempfile.TemporaryDirectory() as scratch:
            out = os.path.join(scratch, "bte.glb")
            with nmo.open(BTE) as doc:
                stats = doc.export_gltf(out)
            self.assertGreater(stats["nodes"], 0)
            self.assertGreater(stats["images_embedded"], 0)
            self.assertEqual(os.path.getsize(out), read_glb_header(out)[2])


if __name__ == "__main__":
    unittest.main()
