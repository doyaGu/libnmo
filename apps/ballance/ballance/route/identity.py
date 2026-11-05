"""Source identity carried through expansion without adding geometry options.

Authored IDs are local to an instance (global for top-level pieces). Legacy
pieces retain positional IDs; their identity is explicitly not insertion-stable.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
import re


def pointer(value: str) -> str:
    return value.replace('~', '~0').replace('/', '~1')


@dataclass(frozen=True)
class Origin:
    entity_id: str
    pointer: str
    instances: tuple[dict, ...] = ()
    stable: bool = False
    override_pointer: str | None = None

    def data(self) -> dict:
        result = asdict(self)
        if self.override_pointer is None:
            result.pop('override_pointer')
        return result


class Piece(dict):
    """A normal piece mapping with provenance preserved by deepcopy/mirror."""

    def __init__(self, value: dict, origin: Origin):
        super().__init__(value)
        self.origin = origin


def identify(item: dict, path: str, scope: str, fallback: str, version: int,
             stable_parent: bool) -> tuple[str, bool]:
    name = item.get('id')
    if 'id' not in item:
        return f'{scope}/position/{fallback}', False
    if version != 1:
        raise ValueError(f'{path}: authored IDs require route schema_version 1')
    if not isinstance(name, str) or not re.fullmatch(r'[A-Za-z][A-Za-z0-9_.-]*', name):
        raise ValueError(f'{path}/id: use a letter followed by letters, digits, _, . or -')
    return f'{scope}/id/{name}', stable_parent
