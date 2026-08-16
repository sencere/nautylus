"""Small ctypes binding for Nautylus.

Build the shared library first:

    make

By default this module loads ../../build/libnautylus.so relative to itself.
Set NAUTYLUS_LIB to override the shared-library path.
"""

from __future__ import annotations

import ctypes
import os
import tempfile
from pathlib import Path
from typing import Iterable, Optional


NG_OK = 0


def _default_library_path() -> str:
    root = Path(__file__).resolve().parents[2]
    return str(root / "build" / "libnautylus.so")


_lib = ctypes.CDLL(os.environ.get("NAUTYLUS_LIB", _default_library_path()))

GraphPtr = ctypes.c_void_p
SymbolId = ctypes.c_uint64
NodeId = ctypes.c_uint64
RelationshipId = ctypes.c_uint64
Status = ctypes.c_int

_lib.ng_create.argtypes = [ctypes.POINTER(GraphPtr), ctypes.c_char_p]
_lib.ng_create.restype = Status
_lib.ng_open.argtypes = [ctypes.POINTER(GraphPtr), ctypes.c_char_p]
_lib.ng_open.restype = Status
_lib.ng_close.argtypes = [GraphPtr]
_lib.ng_close.restype = None
_lib.ng_save.argtypes = [GraphPtr]
_lib.ng_save.restype = Status
_lib.ng_symbol.argtypes = [GraphPtr, ctypes.c_char_p, ctypes.POINTER(SymbolId)]
_lib.ng_symbol.restype = Status
_lib.ng_node_create.argtypes = [
    GraphPtr,
    ctypes.POINTER(SymbolId),
    ctypes.c_size_t,
    ctypes.POINTER(NodeId),
]
_lib.ng_node_create.restype = Status
_lib.ng_relationship_create.argtypes = [
    GraphPtr,
    NodeId,
    SymbolId,
    NodeId,
    ctypes.POINTER(RelationshipId),
]
_lib.ng_relationship_create.restype = Status
_lib.ng_node_set_string.argtypes = [GraphPtr, NodeId, SymbolId, ctypes.c_char_p]
_lib.ng_node_set_string.restype = Status
_lib.ng_node_set_int64.argtypes = [GraphPtr, NodeId, SymbolId, ctypes.c_int64]
_lib.ng_node_set_int64.restype = Status
_lib.ng_node_set_double.argtypes = [GraphPtr, NodeId, SymbolId, ctypes.c_double]
_lib.ng_node_set_double.restype = Status
_lib.ng_node_set_bool.argtypes = [GraphPtr, NodeId, SymbolId, ctypes.c_int]
_lib.ng_node_set_bool.restype = Status
_lib.ng_relationship_set_string.argtypes = [
    GraphPtr,
    RelationshipId,
    SymbolId,
    ctypes.c_char_p,
]
_lib.ng_relationship_set_string.restype = Status
_lib.ng_relationship_set_int64.argtypes = [
    GraphPtr,
    RelationshipId,
    SymbolId,
    ctypes.c_int64,
]
_lib.ng_relationship_set_int64.restype = Status
_lib.ng_relationship_set_double.argtypes = [
    GraphPtr,
    RelationshipId,
    SymbolId,
    ctypes.c_double,
]
_lib.ng_relationship_set_double.restype = Status
_lib.ng_relationship_set_bool.argtypes = [GraphPtr, RelationshipId, SymbolId, ctypes.c_int]
_lib.ng_relationship_set_bool.restype = Status
_lib.ng_node_count.argtypes = [GraphPtr]
_lib.ng_node_count.restype = ctypes.c_size_t
_lib.ng_relationship_count.argtypes = [GraphPtr]
_lib.ng_relationship_count.restype = ctypes.c_size_t
_lib.ng_query_print_file.argtypes = [GraphPtr, ctypes.c_char_p, ctypes.c_char_p]
_lib.ng_query_print_file.restype = Status
_lib.ng_query_execute_file.argtypes = [
    GraphPtr,
    ctypes.c_char_p,
    ctypes.c_char_p,
    ctypes.POINTER(ctypes.c_int),
]
_lib.ng_query_execute_file.restype = Status
_lib.ng_status_name.argtypes = [Status]
_lib.ng_status_name.restype = ctypes.c_char_p


def _bytes(value: str | os.PathLike[str]) -> bytes:
    return os.fsencode(value)


def _check(status: int) -> None:
    if status != NG_OK:
        name = _lib.ng_status_name(status)
        raise RuntimeError(name.decode("utf-8") if name else f"ng_status({status})")


class Graph:
    def __init__(self, handle: GraphPtr):
        self._handle: Optional[GraphPtr] = handle

    @classmethod
    def create(cls, path: str | os.PathLike[str]) -> "Graph":
        handle = GraphPtr()
        _check(_lib.ng_create(ctypes.byref(handle), _bytes(path)))
        return cls(handle)

    @classmethod
    def open(cls, path: str | os.PathLike[str]) -> "Graph":
        handle = GraphPtr()
        _check(_lib.ng_open(ctypes.byref(handle), _bytes(path)))
        return cls(handle)

    def close(self) -> None:
        if self._handle is not None:
            _lib.ng_close(self._handle)
            self._handle = None

    def __enter__(self) -> "Graph":
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    def _require(self) -> GraphPtr:
        if self._handle is None:
            raise RuntimeError("graph is closed")
        return self._handle

    def save(self) -> None:
        _check(_lib.ng_save(self._require()))

    def symbol(self, name: str) -> int:
        out = SymbolId()
        _check(_lib.ng_symbol(self._require(), name.encode("utf-8"), ctypes.byref(out)))
        return int(out.value)

    def create_node(self, labels: Iterable[int] = ()) -> int:
        values = list(labels)
        out = NodeId()
        if values:
            array_type = SymbolId * len(values)
            array = array_type(*values)
            labels_ptr = ctypes.cast(array, ctypes.POINTER(SymbolId))
        else:
            labels_ptr = None
        _check(_lib.ng_node_create(self._require(), labels_ptr, len(values), ctypes.byref(out)))
        return int(out.value)

    def create_relationship(self, source: int, rel_type: int, target: int) -> int:
        out = RelationshipId()
        _check(
            _lib.ng_relationship_create(
                self._require(), source, rel_type, target, ctypes.byref(out)
            )
        )
        return int(out.value)

    def set_node(self, node: int, key: int, value: str | int | float | bool) -> None:
        handle = self._require()
        if isinstance(value, bool):
            _check(_lib.ng_node_set_bool(handle, node, key, int(value)))
        elif isinstance(value, int):
            _check(_lib.ng_node_set_int64(handle, node, key, value))
        elif isinstance(value, float):
            _check(_lib.ng_node_set_double(handle, node, key, value))
        else:
            _check(_lib.ng_node_set_string(handle, node, key, str(value).encode("utf-8")))

    def set_relationship(
        self, relationship: int, key: int, value: str | int | float | bool
    ) -> None:
        handle = self._require()
        if isinstance(value, bool):
            _check(_lib.ng_relationship_set_bool(handle, relationship, key, int(value)))
        elif isinstance(value, int):
            _check(_lib.ng_relationship_set_int64(handle, relationship, key, value))
        elif isinstance(value, float):
            _check(_lib.ng_relationship_set_double(handle, relationship, key, value))
        else:
            _check(
                _lib.ng_relationship_set_string(
                    handle, relationship, key, str(value).encode("utf-8")
                )
            )

    def node_count(self) -> int:
        return int(_lib.ng_node_count(self._require()))

    def relationship_count(self) -> int:
        return int(_lib.ng_relationship_count(self._require()))

    def query(self, query: str, mutate: bool = False) -> str:
        with tempfile.NamedTemporaryFile(delete=False) as tmp:
            path = tmp.name
        try:
            if mutate:
                changed = ctypes.c_int(0)
                _check(
                    _lib.ng_query_execute_file(
                        self._require(),
                        query.encode("utf-8"),
                        _bytes(path),
                        ctypes.byref(changed),
                    )
                )
            else:
                _check(
                    _lib.ng_query_print_file(
                        self._require(), query.encode("utf-8"), _bytes(path)
                    )
                )
            return Path(path).read_text(encoding="utf-8")
        finally:
            try:
                os.unlink(path)
            except FileNotFoundError:
                pass


__all__ = ["Graph", "NG_OK"]
