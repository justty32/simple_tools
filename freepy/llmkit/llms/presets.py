"""Load a small JSON preset into an Engine.

The JSON object maps an id to exactly the connection data llms needs:
endpoint, model, parameters, and an optional description. It deliberately does
not describe model weights, capabilities, sources, aliases, or modes.
"""

import json
from pathlib import Path

from .engine import Engine
from .params import Params


PRESETS = Path(__file__).with_name("presets.json")
FIELDS = {"endpoint", "model", "parameters", "description"}


def _read_presets(path):
    """Read and minimally validate the id-to-preset JSON object."""
    try:
        with open(path, encoding="utf-8") as handle:
            raw = json.load(handle)
    except (OSError, ValueError) as exc:
        raise ValueError(f"cannot read presets from {path}: {exc}") from None
    if not isinstance(raw, dict):
        raise ValueError("presets must be a JSON object keyed by id")
    for preset_id, preset in raw.items():
        if not isinstance(preset_id, str) or not preset_id:
            raise ValueError("preset ids must be non-empty strings")
        if not isinstance(preset, dict):
            raise ValueError(f"preset {preset_id!r} must be an object")
        unknown = set(preset) - FIELDS
        missing = {"endpoint", "model", "parameters"} - set(preset)
        if unknown or missing:
            raise ValueError(
                f"preset {preset_id!r}: missing {sorted(missing)}, unknown {sorted(unknown)}"
            )
        if not all(isinstance(preset[key], str) and preset[key]
                   for key in ("endpoint", "model")):
            raise ValueError(f"preset {preset_id!r}: endpoint and model must be strings")
        if not isinstance(preset["parameters"], dict):
            raise ValueError(f"preset {preset_id!r}: parameters must be an object")
        if "description" in preset and not isinstance(preset["description"], str):
            raise ValueError(f"preset {preset_id!r}: description must be a string")
    return raw


def load_preset(preset_id, path=PRESETS):
    """Return an Engine configured by one preset id."""
    presets = _read_presets(path)
    try:
        preset = presets[preset_id]
    except KeyError:
        raise ValueError(
            f"unknown preset {preset_id!r}; available: {sorted(presets)}"
        ) from None
    parameters = dict(preset["parameters"])
    native = {
        name: parameters.pop(name)
        for name in Params.__dataclass_fields__
        if name != "extra" and name in parameters
    }
    return Engine(
        model=preset["model"],
        url=preset["endpoint"],
        params=Params(**native, extra=parameters),
    )
