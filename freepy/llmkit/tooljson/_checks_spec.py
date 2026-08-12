"""Portable offline checks for schema and exec-recipe validation."""

from copy import deepcopy

from . import Spec, SpecError


BASE = {
    "type": "function",
    "function": {"name": "probe", "description": "probe", "parameters": {
        "type": "object", "properties": {"value": {"type": "integer"}},
        "required": ["value"],
    }},
    "_extra": {"_version": "0.1.0", "_type": "exec", "exec": ["probe"],
               "argv": {"value": {"position": 1}},
               "limits": {"value": {"min": 0, "max": 10}}},
}


def check(condition, label):
    if not condition:
        raise AssertionError(label)
    print(f"ok  {label}")


def rejected(change, text):
    data = deepcopy(BASE)
    change(data)
    try:
        Spec(data)
    except SpecError as exc:
        return text in str(exc)
    return False


def main():
    spec = Spec(deepcopy(BASE))
    check(spec.props == {"value": {"type": "integer"}}
          and spec.required == ["value"], "valid schema is normalized for execution")
    empty = deepcopy(BASE)
    empty["function"] = {"name": "empty"}
    empty["_extra"].pop("argv")
    empty["_extra"].pop("limits")
    check(Spec(empty).props == {},
          "parameters may be omitted for an argument-free tool")

    schema_cases = [
        (lambda d: d["function"].update(description=1), "description"),
        (lambda d: d["function"].update(parameters=None), "parameters"),
        (lambda d: d["function"].update(parameters=[]), "parameters"),
        (lambda d: d["function"]["parameters"].update(type="array"), "type"),
        (lambda d: d["function"]["parameters"].update(properties=[]), "properties"),
        (lambda d: d["function"]["parameters"]["properties"].update(value="integer"),
         "schema object"),
        (lambda d: d["function"]["parameters"].update(required="value"), "required"),
        (lambda d: d["function"]["parameters"].update(required=["value", "value"]),
         "重複"),
        (lambda d: d["function"]["parameters"].update(required=["missing"]), "未知"),
    ]
    check(all(rejected(change, text) for change, text in schema_cases),
          "malformed executable schemas fail at load time")

    recipe_cases = [
        (lambda d: d["_extra"].update(stdout="head"), "stdout"),
        (lambda d: d["_extra"].update(stderr=[]), "stderr"),
        (lambda d: d["_extra"].update(stdin={"param": "value", "extra": 1}), "stdin"),
        (lambda d: d["_extra"]["argv"]["value"].update(position=True), "position"),
        (lambda d: d["_extra"]["argv"]["value"].update(repeat="yes"), "repeat"),
        (lambda d: d["_extra"].update(timeout=0), "timeout"),
        (lambda d: d["_extra"].update(cwd=""), "cwd"),
        (lambda d: d["_extra"].update(limits=[]), "limits"),
        (lambda d: d["_extra"].update(limits={"missing": {"max_bytes": 1}}), "未知"),
        (lambda d: d["_extra"].update(limits={"value": {"max_bytes": True}}),
         "max_bytes"),
        (lambda d: d["_extra"].update(limits={"value": {"min": 2, "max": 1}}),
         "min"),
    ]
    check(all(rejected(change, text) for change, text in recipe_cases),
          "malformed exec recipes fail at load time")

    bad_source = deepcopy(BASE)
    bad_source["_extra"]["source"] = "old metadata"
    check(Spec(bad_source).stale is None,
          "unusable optional source metadata reports unknown instead of crashing")
    print("tooljson schema checks: all passed")


if __name__ == "__main__":
    main()
