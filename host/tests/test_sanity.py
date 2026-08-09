"""Sanity test - proves the pytest harness is wired correctly.

Run with:  pytest host/tests
"""

import rhi


def test_package_imports_and_reports_version() -> None:
    """The package should import and expose a version string.

    This is deliberately trivial. Its job is to fail loudly if the editable
    install (`pip install -e "host[dev]"`) has not been done, which is the
    most common first-day problem.
    """
    assert isinstance(rhi.__version__, str)
    assert rhi.__version__.count(".") == 2  # e.g. "0.1.0"
