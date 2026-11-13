"""Read, edit and convert Virtools .nmo/.cmo/.vmo files with libnmo.

    import nmo

    with nmo.open("Level_01.NMO") as doc:
        print(doc.info.file_version, len(doc))
        for obj in doc.objects("CK3dObject"):
            print(obj.id, obj.name, obj.fields["world_matrix"])
        doc.export_gltf("level.glb")

A thin layer over libnmo's C API (document/nmo_document_*.h). Objects are named
by id and read again on each access, so an object removed by an edit raises
instead of reading freed memory. Field values come as `nmo object export`
writes them and are written back through `nmo object import`.
"""

import ctypes
import json
import os
from typing import Any, Dict, Iterator, List, NamedTuple, Optional, Union

from ._native import (
    GLTF_STATS_FIELDS,
    ContextDesc,
    GltfExportStats,
    LoadOptions,
    lib,
)

__all__ = ["NmoError", "Context", "Document", "Object", "FileInfo", "open", "version"]

PROFILES = {"full": 0, "metadata": 1, "header": 2, "objects": 3}


class NmoError(Exception):
    """A libnmo call failed; `status` is its nmo_status_t."""

    def __init__(self, status: int, message: str):
        super().__init__(message)
        self.status = status


def _text(raw: Optional[bytes]) -> Optional[str]:
    """Virtools names are not UTF-8; libnmo reads their bytes as Latin-1."""
    if raw is None:
        return None
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("latin-1")


def _check(status: int, what: str) -> None:
    if status == 0:
        return
    message = lib.nmo_last_error_message() if lib.nmo_last_error_code() == status else None
    detail = _text(message) or _text(lib.nmo_error_string(status)) or "error"
    raise NmoError(status, f"{what}: {detail}")


def version() -> str:
    """libnmo's version string."""
    return _text(lib.nmo_version())


def _default_data_dir() -> Optional[str]:
    env = os.environ.get("NMO_DATA_DIR")
    if env:
        return env
    bundled = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
    return bundled if os.path.isdir(bundled) else None


class Context:
    """Types, building blocks and plugins the documents are read with.

    data_dir is the directory of libnmo's Virtools tables (data/*.json); by
    default $NMO_DATA_DIR, or a data directory next to this package. Without
    it, files load but building blocks and plugins are unknown.
    """

    def __init__(self, data_dir: Optional[str] = None):
        self.data_dir = data_dir if data_dir is not None else _default_data_dir()
        desc = ContextDesc()
        if self.data_dir is not None:
            desc.data_dir = os.fsencode(self.data_dir)
        self._handle = lib.nmo_context_create(ctypes.byref(desc))
        if not self._handle:
            raise NmoError(1, "Cannot create a libnmo context")

    def close(self) -> None:
        if self._handle:
            lib.nmo_context_release(self._handle)
            self._handle = None

    def __del__(self):
        self.close()

    def open(self, path: Union[str, os.PathLike], *, profile: str = "full") -> "Document":
        return Document._load(self, path, profile)


_default_context: Optional[Context] = None


def open(path: Union[str, os.PathLike], *, profile: str = "full",
         context: Optional[Context] = None) -> "Document":
    """Load a file.

    profile: "full" (every object with its state), "objects" (the object
    table; states are read by Object.load_state()), "metadata" (file header,
    Header1 and plugin dependencies) or "header" (the file header only).
    The partial profiles give a document that cannot be changed or saved.
    """
    global _default_context
    if context is None:
        if _default_context is None:
            _default_context = Context()
        context = _default_context
    return context.open(path, profile=profile)


class FileInfo(NamedTuple):
    file_version: int
    ck_version: int
    product_version: int
    product_build: int
    file_size: int
    object_count: int
    manager_count: int
    write_mode: int
    crc_ok: Optional[bool]  # None when the data section was not read


class Document:
    """A loaded file. Close it, or use it as a context manager."""

    def __init__(self):
        raise TypeError("use nmo.open()")

    @classmethod
    def _load(cls, context: Context, path, profile: str) -> "Document":
        if profile not in PROFILES:
            raise ValueError(f"profile must be one of {sorted(PROFILES)}")
        self = object.__new__(cls)
        self.context = context
        self.path = os.fspath(path)
        self._handle = None
        options = lib.nmo_load_options_default()
        options.profile = PROFILES[profile]
        handle = ctypes.c_void_p()
        _check(lib.nmo_document_load_file(context._handle, os.fsencode(self.path),
                                          ctypes.byref(options), ctypes.byref(handle)),
               f"Cannot load {self.path}")
        self._handle = handle.value
        return self

    def close(self) -> None:
        if getattr(self, "_handle", None):
            lib.nmo_document_destroy(self._handle)
            self._handle = None

    def __del__(self):
        self.close()

    def __enter__(self) -> "Document":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def _live(self):
        if not self._handle:
            raise ValueError("document is closed")
        return self._handle

    @property
    def info(self) -> FileInfo:
        raw = lib.nmo_document_get_file_info(self._live())
        return FileInfo(raw.file_version, raw.ck_version, raw.product_version,
                        raw.product_build, raw.file_size, raw.object_count,
                        raw.manager_count, raw.write_mode,
                        None if raw.crc_status == 0 else raw.crc_status == 1)

    @property
    def partial(self) -> bool:
        """Loaded with a profile other than "full": read only."""
        return bool(lib.nmo_document_is_partial_load(self._live()))

    def __len__(self) -> int:
        return lib.nmo_document_object_count(self._live())

    def __iter__(self) -> Iterator["Object"]:
        handle = self._live()
        for index in range(lib.nmo_document_object_count(handle)):
            yield Object(self, lib.nmo_document_object_at(handle, index))

    def __getitem__(self, object_id: int) -> "Object":
        """The object with this id; KeyError when there is none."""
        if not lib.nmo_document_object_class(self._live(), object_id):
            raise KeyError(object_id)
        return Object(self, object_id)

    def class_id(self, class_name: str) -> int:
        """Id of a class by name ("CK3dEntity"); ValueError when unknown."""
        found = lib.nmo_document_class_id(self._live(), class_name.encode("ascii"))
        if not found:
            raise ValueError(f"unknown class {class_name!r}")
        return found

    def objects(self, class_name: Optional[str] = None, *, name: Optional[str] = None,
                derived: bool = True) -> List["Object"]:
        """Objects of a class (and its derived classes unless derived=False), by name."""
        base = self.class_id(class_name) if class_name is not None else None
        result = []
        for obj in self:
            if base is not None:
                cid = obj.class_id
                if cid != base and not (derived and lib.nmo_document_class_is_derived(self._handle, cid, base)):
                    continue
            if name is not None and obj.name != name:
                continue
            result.append(obj)
        return result

    def import_json(self, data: Union[str, Dict[str, Any]]) -> int:
        """Write fields given as `nmo object import` reads them; returns how many were written."""
        text = (data if isinstance(data, str) else json.dumps(data)).encode("utf-8")
        written = ctypes.c_size_t()
        _check(lib.nmo_document_import_json(self._live(), text, len(text), ctypes.byref(written)),
               "Cannot write the fields")
        return written.value

    def save(self, path: Union[str, os.PathLike]) -> None:
        """Save the document as a file (version 8). The file is replaced only once it is complete."""
        _check(lib.nmo_document_save_file(self._live(), os.fsencode(os.fspath(path)), None),
               f"Cannot save {os.fspath(path)}")

    def export_gltf(self, path: Union[str, os.PathLike], *, embed_textures: bool = True,
                    color_key: bool = True) -> Dict[str, int]:
        """Write the 3D entities and what they draw as glTF 2.0 binary (.glb).

        embed_textures=False names texture files instead of embedding pixels;
        color_key=False keeps the key colour of colour-keyed textures opaque.
        Returns the counts written (nodes, meshes, materials, ...).
        """
        options = lib.nmo_gltf_export_options_default()
        options.embed_textures = embed_textures
        options.apply_color_key = color_key
        stats = GltfExportStats()
        _check(lib.nmo_document_export_gltf(self._live(), os.fsencode(os.fspath(path)),
                                            ctypes.byref(options), ctypes.byref(stats)),
               f"Cannot write {os.fspath(path)}")
        return {name: getattr(stats, name) for name in GLTF_STATS_FIELDS}


class Object:
    """An object of a document, by id."""

    __slots__ = ("document", "id")

    def __init__(self, document: Document, object_id: int):
        self.document = document
        self.id = object_id

    def __repr__(self) -> str:
        return f"<nmo.Object {self.id} {self.class_name} {self.name!r}>"

    def __eq__(self, other) -> bool:
        return isinstance(other, Object) and other.document is self.document and other.id == self.id

    def __hash__(self) -> int:
        return hash((id(self.document), self.id))

    def _check_exists(self) -> int:
        cid = lib.nmo_document_object_class(self.document._live(), self.id)
        if not cid:
            raise KeyError(f"object {self.id} is gone")
        return cid

    @property
    def name(self) -> Optional[str]:
        self._check_exists()
        return _text(lib.nmo_document_object_name(self.document._handle, self.id))

    @property
    def class_id(self) -> int:
        return self._check_exists()

    @property
    def class_name(self) -> Optional[str]:
        return _text(lib.nmo_document_class_name(self.document._handle, self._check_exists()))

    def is_a(self, class_name: str) -> bool:
        """Whether the object's class is class_name or derives from it."""
        base = self.document.class_id(class_name)
        cid = self._check_exists()
        return cid == base or bool(lib.nmo_document_class_is_derived(self.document._handle, cid, base))

    def to_dict(self) -> Dict[str, Any]:
        """The object as `nmo object export` writes it: id, class, name and fields."""
        self._check_exists()
        text = ctypes.c_void_p()
        size = ctypes.c_size_t()
        _check(lib.nmo_document_object_json(self.document._handle, self.id,
                                            ctypes.byref(text), ctypes.byref(size)),
               f"Cannot read object {self.id}")
        try:
            return json.loads(ctypes.string_at(text.value, size.value).decode("utf-8"))
        finally:
            lib.nmo_document_free_json(text)

    @property
    def fields(self) -> Dict[str, Any]:
        """Field values by name, from the base class to the object's own.

        A scalar is a number, bool or string; an array is its list of items.
        """
        result = {}
        for field in self.to_dict()["fields"]:
            result[field["name"]] = field["items"] if field.get("kind") == "array" else field.get("value")
        return result

    def set(self, **values: Any) -> int:
        """Write fields by name, as values in the form `fields` gives them; returns how many were written."""
        return self.update(values)

    def update(self, values: Dict[str, Any]) -> int:
        snapshot = self.to_dict()
        declared = {field["name"]: field for field in snapshot["fields"]}
        entries = []
        for name, value in values.items():
            field = declared.get(name)
            if field is None:
                raise KeyError(f"{self.class_name} has no field {name!r}")
            # The exported entry with the new value and without its raw bytes,
            # which the import would take over the value
            entry = {key: field[key] for key in ("name", "owner_type_guid", "type_guid", "kind")
                     if key in field}
            if entry.get("kind") == "array":
                items = list(value)
                entry.update(value=None, items=items, count=len(items))
            else:
                entry["value"] = value
            entries.append(entry)
        target = {key: snapshot[key] for key in ("id", "class_name", "name") if key in snapshot}
        target["fields"] = entries
        return self.document.import_json({"objects": [target]})

    def load_state(self) -> None:
        """Read the object's state, for a document opened with profile="objects"."""
        self._check_exists()
        _check(lib.nmo_document_load_object_state(self.document._handle, self.id),
               f"Cannot read the state of object {self.id}")
