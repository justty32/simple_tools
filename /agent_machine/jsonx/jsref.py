"""Pure operations used by the JSON reference resolver."""


def split_ref(ref: str) -> tuple[str, str]:
    if "#" not in ref:
        return ref, ""
    file, _, fragment = ref.partition("#")
    return file, fragment.removeprefix("/")


def lookup(document: object, fragment: str) -> tuple[object, bool]:
    if not fragment:
        return document, True
    value = document
    for segment in fragment.split("/"):
        if isinstance(value, dict):
            if segment not in value:
                return None, False
            value = value[segment]
        elif isinstance(value, list):
            if not segment.isdigit() or int(segment) >= len(value):
                return None, False
            value = value[int(segment)]
        else:
            return None, False
    return value, True


def merge(first: object, second: object) -> object:
    if not isinstance(first, dict) or not isinstance(second, dict):
        return second
    result = dict(first)
    for key, value in second.items():
        result[key] = merge(result[key], value) if key in result else value
    return result
