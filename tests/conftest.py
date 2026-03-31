import pytest
import sqlite3
import os


def _vec_debug():
    db = sqlite3.connect(":memory:")
    db.enable_load_extension(True)
    db.load_extension("dist/vec0")
    db.enable_load_extension(False)
    return db.execute("SELECT vec_debug()").fetchone()[0]


def _has_build_flag(flag):
    return flag in _vec_debug().split("Build flags:")[-1]


def pytest_collection_modifyitems(config, items):
    has_ivf = _has_build_flag("ivf")
    if has_ivf:
        return
    skip_ivf = pytest.mark.skip(reason="IVF not enabled (compile with -DSQLITE_VEC_EXPERIMENTAL_IVF_ENABLE=1)")
    ivf_prefixes = ("test-ivf",)
    for item in items:
        if any(item.fspath.basename.startswith(p) for p in ivf_prefixes):
            item.add_marker(skip_ivf)


@pytest.fixture()
def db():
    db = sqlite3.connect(":memory:")
    db.row_factory = sqlite3.Row
    db.enable_load_extension(True)
    db.load_extension("dist/vec0")
    db.enable_load_extension(False)
    return db
