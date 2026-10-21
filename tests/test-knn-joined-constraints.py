import pytest

# Six points on a line, the first three tagged 1 and in partition 1, the rest
# tagged 2 and in partition 2, so the k = 2 nearest the origin are rowids 1 and
# 2 overall and 4 and 5 among tag 2.


@pytest.fixture()
def fill(db):
    db.execute(
        "create virtual table v using vec0("
        "part integer partition key, embedding float[2] distance_metric=l2, tag integer)"
    )
    db.executemany(
        "insert into v(rowid, part, embedding, tag) values (?, ?, ?, ?)",
        [[i, 1 if i <= 3 else 2, f"[{i},0]", 1 if i <= 3 else 2] for i in range(1, 7)],
    )
    db.execute("create table w(q text, n integer)")
    db.execute("insert into w values ('[0,0]', 2)")
    db.execute("create table categories(id integer primary key, q text, n integer)")
    db.execute("insert into categories values (1, '[0,0]', 2), (2, '[0,0]', 2)")
    return db


def rowids(db, sql):
    return [row[0] for row in db.execute(sql)]


@pytest.mark.parametrize("tables", ["w, v", "v, w"])
def test_the_query_vector_can_come_from_another_table(fill, tables):
    knn = f"select v.rowid from {tables} where v.embedding match w.q and v.k = 2"
    assert rowids(fill, knn) == [1, 2]


@pytest.mark.parametrize("tables", ["w, v", "v, w"])
def test_k_can_come_from_another_table(fill, tables):
    knn = f"select v.rowid from {tables} where v.embedding match '[0,0]' and v.k = w.n"
    assert rowids(fill, knn) == [1, 2]


# A table that supplies the query vector or k is read before v, so vec0 runs one
# KNN per row of it, and a filter whose value comes from that table applies
# before the k: the 2 nearest in each category.
@pytest.mark.parametrize(
    "arguments",
    [
        "v.embedding match c.q and v.k = 2",
        "v.embedding match '[0,0]' and v.k = c.n",
    ],
)
@pytest.mark.parametrize("column", ["tag", "part"])
def test_a_table_that_supplies_an_argument_filters_before_the_k(fill, column, arguments):
    knn = (
        f"select c.id, v.rowid from v join categories c on c.id = v.{column} "
        f"where {arguments}"
    )
    assert [tuple(row) for row in fill.execute(knn)] == [(1, 1), (1, 2), (2, 4), (2, 5)]


# A filter whose value comes from a table that supplies no KNN argument runs on
# the k rows the KNN returns, in either FROM order, so a lookup join returns k
# rows. Running one KNN per categories row instead would return k rows per
# category; ARCHITECTURE.md's idxStr section says why vec0 leaves the filter to
# the plan.
@pytest.mark.parametrize(
    "tables",
    [
        "v join categories c on c.id = v.{column}",
        "categories c join v on c.id = v.{column}",
    ],
)
@pytest.mark.parametrize("column", ["tag", "part"])
def test_a_lookup_join_on_a_filter_column_returns_k_rows(fill, column, tables):
    knn = (
        f"select c.id, v.rowid from {tables.format(column=column)} "
        "where v.embedding match '[0,0]' and v.k = 2"
    )
    assert [tuple(row) for row in fill.execute(knn)] == [(1, 1), (1, 2)]
