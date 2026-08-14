"""Run every offline Agent Machine check."""

import unittest

from . import cli, jsonx, new, runs


loader = unittest.TestLoader()
suite = unittest.TestSuite(loader.loadTestsFromModule(module) for module in (
    new,
    jsonx,
    runs,
    cli,
))
result = unittest.TextTestRunner(verbosity=2).run(suite)
raise SystemExit(not result.wasSuccessful())
