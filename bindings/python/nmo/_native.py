"""ctypes declarations of the libnmo C functions the package calls.

The library is the shared build of libnmo (configure with -DNMO_BUILD_SHARED=ON).
It is found, in order, at $NMO_LIBRARY, next to this package, or by the system
loader under the name "nmo".
"""

import ctypes
import ctypes.util
import os
import sys
from pathlib import Path

c_bool = ctypes.c_bool
c_char_p = ctypes.c_char_p
c_int = ctypes.c_int
c_int32 = ctypes.c_int32
c_size_t = ctypes.c_size_t
c_uint32 = ctypes.c_uint32
c_uint64 = ctypes.c_uint64
c_void_p = ctypes.c_void_p
POINTER = ctypes.POINTER


class ContextDesc(ctypes.Structure):
    """nmo_context_desc_t"""
    _fields_ = [
        ("allocator", c_void_p),
        ("logger", c_void_p),
        ("thread_pool_size", c_int),
        ("data_dir", c_char_p),
    ]


class LoadOptions(ctypes.Structure):
    """nmo_load_options_t"""
    _fields_ = [
        ("allocator", c_void_p),
        ("flags", c_int),
        ("profile", c_int),
        ("max_included_name_len", c_uint32),
        ("max_included_file_size", c_uint32),
        ("max_unpacked_size", c_uint64),
        ("max_object_count", c_uint32),
        ("collect_perf_stats", c_bool),
        ("perf_stats", c_void_p),
        ("diagnostics", c_void_p),
    ]


class FileInfo(ctypes.Structure):
    """nmo_file_info_t"""
    _fields_ = [
        ("file_version", c_uint32),
        ("file_version2", c_uint32),
        ("ck_version", c_uint32),
        ("product_version", c_uint32),
        ("product_build", c_uint32),
        ("file_size", c_size_t),
        ("object_count", c_uint32),
        ("manager_count", c_uint32),
        ("write_mode", c_uint32),
        ("crc_status", c_int),
        ("crc_stored", c_uint32),
        ("crc_computed", c_uint32),
    ]


class GltfExportOptions(ctypes.Structure):
    """nmo_gltf_export_options_t"""
    _fields_ = [("embed_textures", c_bool), ("apply_color_key", c_bool)]


GLTF_STATS_FIELDS = (
    "nodes", "meshes", "primitives", "materials", "textures",
    "images_embedded", "images_external", "images_missing", "vertices", "triangles",
)


class GltfExportStats(ctypes.Structure):
    """nmo_gltf_export_stats_t"""
    _fields_ = [(name, c_size_t) for name in GLTF_STATS_FIELDS]


# name: (restype, argtypes)
PROTOTYPES = {
    "nmo_version": (c_char_p, []),
    "nmo_error_string": (c_char_p, [c_int32]),
    "nmo_last_error_code": (c_int, []),
    "nmo_last_error_message": (c_char_p, []),
    "nmo_last_error_clear": (None, []),
    "nmo_context_create": (c_void_p, [POINTER(ContextDesc)]),
    "nmo_context_release": (None, [c_void_p]),
    "nmo_load_options_default": (LoadOptions, []),
    "nmo_document_load_file": (c_int32, [c_void_p, c_char_p, POINTER(LoadOptions), POINTER(c_void_p)]),
    "nmo_document_destroy": (None, [c_void_p]),
    "nmo_document_save_file": (c_int32, [c_void_p, c_char_p, c_void_p]),
    "nmo_document_get_file_info": (FileInfo, [c_void_p]),
    "nmo_document_is_partial_load": (c_int, [c_void_p]),
    "nmo_document_load_object_state": (c_int32, [c_void_p, c_uint32]),
    "nmo_document_object_count": (c_size_t, [c_void_p]),
    "nmo_document_object_at": (c_uint32, [c_void_p, c_size_t]),
    "nmo_document_object_name": (c_char_p, [c_void_p, c_uint32]),
    "nmo_document_object_class": (c_uint32, [c_void_p, c_uint32]),
    "nmo_document_class_name": (c_char_p, [c_void_p, c_uint32]),
    "nmo_document_class_id": (c_uint32, [c_void_p, c_char_p]),
    "nmo_document_class_is_derived": (c_bool, [c_void_p, c_uint32, c_uint32]),
    "nmo_document_object_json": (c_int32, [c_void_p, c_uint32, POINTER(c_void_p), POINTER(c_size_t)]),
    "nmo_document_import_json": (c_int32, [c_void_p, c_char_p, c_size_t, POINTER(c_size_t)]),
    "nmo_document_free_json": (None, [c_void_p]),
    "nmo_gltf_export_options_default": (GltfExportOptions, []),
    "nmo_document_export_gltf": (c_int32, [c_void_p, c_char_p, POINTER(GltfExportOptions), POINTER(GltfExportStats)]),
}


def _library_names():
    if sys.platform == "win32":
        return ["nmo.dll", "libnmo.dll"]
    if sys.platform == "darwin":
        return ["libnmo.dylib", "libnmo.3.dylib"]
    return ["libnmo.so", "libnmo.so.3"]


def _candidates():
    env = os.environ.get("NMO_LIBRARY")
    if env:
        yield env
        return
    here = Path(__file__).resolve().parent
    for name in _library_names():
        path = here / name
        if path.exists():
            yield str(path)
    found = ctypes.util.find_library("nmo")
    if found:
        yield found


def _load():
    errors = []
    for candidate in _candidates():
        try:
            lib = ctypes.CDLL(candidate)
        except OSError as error:
            errors.append(f"{candidate}: {error}")
            continue
        for name, (restype, argtypes) in PROTOTYPES.items():
            function = getattr(lib, name)
            function.restype = restype
            function.argtypes = argtypes
        return lib
    detail = "; ".join(errors) if errors else "no candidate found"
    raise ImportError(
        "libnmo shared library not found (" + detail + "). Build libnmo with "
        "-DNMO_BUILD_SHARED=ON and set NMO_LIBRARY to the library's path.")


lib = _load()
