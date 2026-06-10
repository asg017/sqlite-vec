"""Tests for locale-independent JSON float parsing (issue #241).

strtod(3) respects LC_NUMERIC, so locales like fr_FR or de_DE that use ','
as the decimal separator would cause vec0 to reject valid JSON vectors like
'[0.1, 0.2, 0.3]'. The fix replaces strtod with a custom locale-independent
parser (strtod_c) that always treats '.' as the decimal separator.
"""

import locale
import struct
import pytest
from helpers import _f32


def test_vec0_locale_independent(db):
    db.execute("create virtual table v using vec0(embedding float[3])")

    original = locale.setlocale(locale.LC_NUMERIC)
    locale_changed = False

    for candidate in ("fr_FR.UTF-8", "de_DE.UTF-8", "it_IT.UTF-8", "fr_FR", "de_DE"):
        try:
            locale.setlocale(locale.LC_NUMERIC, candidate)
            locale_changed = True
            break
        except locale.Error:
            continue

    try:
        db.execute("insert into v(rowid, embedding) values (1, '[0.1, 0.2, 0.3]')")
        db.execute("insert into v(rowid, embedding) values (2, '[1.23, 4.56, 7.89]')")
        db.execute("insert into v(rowid, embedding) values (3, '[1e-3, 2.5e2, -0.75]')")

        row = db.execute("select embedding from v where rowid = 1").fetchone()
        assert row[0] == _f32([0.1, 0.2, 0.3])

        row = db.execute("select embedding from v where rowid = 2").fetchone()
        assert row[0] == _f32([1.23, 4.56, 7.89])

        row = db.execute("select embedding from v where rowid = 3").fetchone()
        assert row[0] == _f32([1e-3, 2.5e2, -0.75])
    finally:
        locale.setlocale(locale.LC_NUMERIC, original)

    if not locale_changed:
        pytest.skip("No non-C locale available to test locale independence")
