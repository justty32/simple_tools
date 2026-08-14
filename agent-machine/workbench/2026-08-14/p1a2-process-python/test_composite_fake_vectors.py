from __future__ import annotations

import copy
import unittest

from p1a2_composite_fake import (
    composite_receipt_bytes, derive_child_id, fake_call_bytes,
    fake_leaf_receipt_bytes, root_fake_call_bytes,
)
from p1a2_store import Corruption, make_ref
from test_support import root_fake_call


ROOT_FAKE_BYTES = b'{"children":[{"recipe":{"kind":"fake","result":{"stderr":{"data":"","encoding":"base64"},"stdout":{"data":"b2sK","encoding":"base64"},"termination":{"code":0,"kind":"exited"}}},"slot":"first"},{"recipe":{"kind":"fake","result":{"stderr":{"data":"","encoding":"base64"},"stdout":{"data":"b2sK","encoding":"base64"},"termination":{"code":0,"kind":"exited"}}},"slot":"second"}],"first_success_oracle":{"stderr":{"data":"","encoding":"base64"},"stdout":{"data":"b2sK","encoding":"base64"},"termination":{"code":0,"kind":"exited"}},"kind":"sequence_two_fake","v":1}\n'
ROOT_FAKE_REF = {"sha256": "2c054a622a87203d3c57004af4f26f7dc5016e2ccc2f0f5c19f6a864ce5f1db6", "size": 561}
FAKE_CALL_BYTES = b'{"kind":"fake","result":{"stderr":{"data":"","encoding":"base64"},"stdout":{"data":"b2sK","encoding":"base64"},"termination":{"code":0,"kind":"exited"}},"v":1}\n'
FAKE_CALL_REF = {"sha256": "3bb33a69281456e755654ff03a563fa4b08366e8fce0a61ce849e793ce26864d", "size": 160}
FIRST_RECEIPT_BYTES = b'{"basis":{"kind":"fixture_fake","slot":"first"},"call_ref":{"sha256":"3bb33a69281456e755654ff03a563fa4b08366e8fce0a61ce849e793ce26864d","size":160},"kind":"leaf","return":{"stderr_ref":{"sha256":"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","size":0},"stdout_ref":{"sha256":"dc51b8c96c2d745df3bd5590d990230a482fd247123599548e0632fdbf97fc22","size":3},"termination":{"code":0,"kind":"exited"}},"task_id":"root--first","v":1}\n'
FIRST_RECEIPT_REF = {"sha256": "7aa45814efdc3a44b9e9897d7831a6901d188fcdd26214f86af4edb377230205", "size": 445}
SECOND_RECEIPT_BYTES = b'{"basis":{"kind":"fixture_fake","slot":"second"},"call_ref":{"sha256":"3bb33a69281456e755654ff03a563fa4b08366e8fce0a61ce849e793ce26864d","size":160},"kind":"leaf","return":{"stderr_ref":{"sha256":"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","size":0},"stdout_ref":{"sha256":"dc51b8c96c2d745df3bd5590d990230a482fd247123599548e0632fdbf97fc22","size":3},"termination":{"code":0,"kind":"exited"}},"task_id":"root--second","v":1}\n'
SECOND_RECEIPT_REF = {"sha256": "ad21b758d8048e08ad6521f186968046bcfc7a5a4bd865f43889976d163ad12b", "size": 447}
ROOT_RECEIPT_BYTES = b'{"basis":{"children":[{"receipt_ref":{"sha256":"7aa45814efdc3a44b9e9897d7831a6901d188fcdd26214f86af4edb377230205","size":445},"slot":"first","task_id":"root--first"},{"receipt_ref":{"sha256":"ad21b758d8048e08ad6521f186968046bcfc7a5a4bd865f43889976d163ad12b","size":447},"slot":"second","task_id":"root--second"}]},"call_ref":{"sha256":"2c054a622a87203d3c57004af4f26f7dc5016e2ccc2f0f5c19f6a864ce5f1db6","size":561},"kind":"composite","return":{"kind":"fixture_sequence_two_success"},"task_id":"root","v":1}\n'
ROOT_RECEIPT_REF = {"sha256": "f6385567cd5f1b6543cf76951e884a7ef097a4c16b01c1547134dd178c2f6599", "size": 506}


class CompositeFakeVectors(unittest.TestCase):
    def test_checked_in_call_and_receipt_goldens(self):
        root = root_fake_call()
        self.assertEqual(root_fake_call_bytes(root), ROOT_FAKE_BYTES)
        self.assertEqual(make_ref(ROOT_FAKE_BYTES), ROOT_FAKE_REF)
        for slot in ("first", "second"):
            self.assertEqual(fake_call_bytes(root, slot), FAKE_CALL_BYTES)
        self.assertEqual(make_ref(FAKE_CALL_BYTES), FAKE_CALL_REF)
        first = fake_leaf_receipt_bytes("root--first", FAKE_CALL_REF, "first",
                                        root["children"][0]["recipe"]["result"])
        second = fake_leaf_receipt_bytes("root--second", FAKE_CALL_REF, "second",
                                         root["children"][1]["recipe"]["result"])
        self.assertEqual(first, FIRST_RECEIPT_BYTES)
        self.assertEqual(second, SECOND_RECEIPT_BYTES)
        self.assertEqual(make_ref(first), FIRST_RECEIPT_REF)
        self.assertEqual(make_ref(second), SECOND_RECEIPT_REF)
        composite = composite_receipt_bytes("root", ROOT_FAKE_REF,
                                            FIRST_RECEIPT_REF, SECOND_RECEIPT_REF)
        self.assertEqual(composite, ROOT_RECEIPT_BYTES)
        self.assertEqual(make_ref(composite), ROOT_RECEIPT_REF)

    def test_exact_schema_and_id_negatives(self):
        cases = []
        def changed(mutator):
            value = copy.deepcopy(root_fake_call()); mutator(value); cases.append(value)
        changed(lambda x: x.__setitem__("v", True))
        changed(lambda x: x.__setitem__("extra", 1))
        changed(lambda x: x["children"].reverse())
        changed(lambda x: x["children"][0]["recipe"].__setitem__("kind", "process"))
        changed(lambda x: x["children"][0]["recipe"]["result"]["stdout"].__setitem__("data", "b2s"))
        changed(lambda x: x["first_success_oracle"]["termination"].__setitem__("code", 1))
        changed(lambda x: x["first_success_oracle"]["stderr"].__setitem__("data", "WA=="))
        for value in cases:
            with self.assertRaises(Corruption):
                root_fake_call_bytes(value)
        self.assertEqual(derive_child_id("root", "first"), "root--first")
        with self.assertRaises(Corruption):
            derive_child_id("bad--root", "first")
        with self.assertRaises(Corruption):
            derive_child_id("root", "third")


if __name__ == "__main__":
    unittest.main(verbosity=2)
