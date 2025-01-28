defmodule BitItem do
  use Ecto.Schema

  schema "bit_ecto_items" do
    field(:embedding, SqliteVec.Ecto.Bit)
  end
end

defmodule BitEctoTest do
  use ExUnit.Case, async: false

  import Ecto.Query
  import SqliteVec.Ecto.Query

  setup_all do
    Ecto.Adapters.SQL.query!(Repo, "DROP TABLE IF EXISTS test", [])

    Ecto.Adapters.SQL.query!(Repo, "CREATE TABLE test (some_column)", [])

    Ecto.Adapters.SQL.query!(Repo, "INSERT INTO test (some_column) VALUES ($1)", ["test dummy"])

    Ecto.Adapters.SQL.query!(Repo, "DROP TABLE IF EXISTS bit_ecto_items", [])

    Ecto.Adapters.SQL.query!(
      Repo,
      "CREATE VIRTUAL TABLE bit_ecto_items USING vec0(id INTEGER PRIMARY KEY, embedding bit[8])",
      []
    )

    create_items()
    :ok
  end

  defp create_items do
    Ecto.Adapters.SQL.query!(
      Repo,
      "insert into bit_ecto_items(id, embedding) values(1, vec_bit(X'FF')), (2, vec_bit(X'00')), (3, vec_bit(X'0A'))",
      []
    )
  end

  test "match performs a KNN query" do
    items =
      Repo.all(
        from(i in BitItem,
          where:
            match(
              i.embedding,
              vec_bit(SqliteVec.Bit.new([0, 0, 0, 0, 0, 0, 0, 1]))
            ),
          limit: 3
        )
      )

    assert Enum.map(items, fn v ->
             v.id
           end) == [2, 3, 1]

    assert Enum.map(items, fn v -> v.embedding |> SqliteVec.Bit.to_list() end) == [
             [0, 0, 0, 0, 0, 0, 0, 0],
             [0, 0, 0, 0, 1, 0, 1, 0],
             [1, 1, 1, 1, 1, 1, 1, 1]
           ]
  end

  test "vector hamming distance" do
    items =
      Repo.all(
        from(i in BitItem,
          order_by:
            vec_distance_hamming(
              i.embedding,
              vec_bit(SqliteVec.Bit.new([0, 0, 0, 0, 0, 0, 0, 1]))
            ),
          limit: 5
        )
      )

    assert Enum.map(items, fn v ->
             v.id
           end) == [2, 3, 1]

    assert Enum.map(items, fn v -> v.embedding |> SqliteVec.Bit.to_list() end) == [
             [0, 0, 0, 0, 0, 0, 0, 0],
             [0, 0, 0, 0, 1, 0, 1, 0],
             [1, 1, 1, 1, 1, 1, 1, 1]
           ]
  end

  test "vec_length returns number of elements of vector" do
    vector = SqliteVec.Bit.new([0, 0, 0, 0, 0, 0, 0, 0] ++ [0, 0, 0, 0, 1, 0, 1, 0])
    assert Repo.one(from("test", select: vec_length(vec_bit(vector)))) == 16
  end

  test "vec_type returns vector type as string" do
    vector = SqliteVec.Bit.new([0, 0, 0, 0, 0, 0, 0, 0])
    assert Repo.one(from("test", select: vec_type(vec_bit(vector)))) == "bit"
  end

  test "vec_add errors" do
    v1 = SqliteVec.Bit.new([0, 0, 0, 0, 0, 0, 0, 0])
    v2 = SqliteVec.Bit.new([0, 0, 0, 0, 0, 0, 0, 1])

    assert_raise Exqlite.Error, fn ->
      Repo.one(from("test", select: vec_add(vec_bit(v1), vec_bit(v2))))
    end
  end

  test "vec_sub errros" do
    v1 = SqliteVec.Bit.new([0, 0, 0, 0, 0, 0, 0, 0])
    v2 = SqliteVec.Bit.new([0, 0, 0, 0, 0, 0, 0, 1])

    assert_raise Exqlite.Error, fn ->
      Repo.one(from("test", select: vec_sub(vec_bit(v1), vec_bit(v2))))
    end
  end

  test "vec_normalize errors" do
    vector = SqliteVec.Bit.new([0, 0, 0, 0, 0, 0, 0, 0])

    assert_raise Exqlite.Error, fn ->
      Repo.one(from("test", select: vec_normalize(vec_bit(vector))))
    end
  end

  test "vec_slice extracts subset of vector" do
    vector =
      SqliteVec.Bit.new(
        [0, 0, 0, 0, 0, 0, 0, 1] ++
          [0, 0, 0, 0, 0, 0, 1, 0] ++
          [0, 0, 0, 0, 0, 0, 1, 1] ++
          [0, 0, 0, 0, 0, 1, 0, 0]
      )

    binary = Repo.one(from("test", select: vec_slice(vec_bit(vector), ^8, ^24)))

    assert SqliteVec.Bit.from_binary(binary) ==
             SqliteVec.Bit.new([0, 0, 0, 0, 0, 0, 1, 0] ++ [0, 0, 0, 0, 0, 0, 1, 1])
  end

  test "vec_to_json returns vector as json" do
    vector =
      SqliteVec.Bit.new(
        [0, 0, 0, 0, 0, 0, 0, 1] ++
          [0, 0, 0, 0, 0, 0, 1, 0] ++
          [0, 0, 0, 0, 0, 0, 1, 1]
      )

    assert Repo.one(from("test", select: vec_to_json(vec_bit(vector)))) ==
             "[1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,1,1,0,0,0,0,0,0]"
  end

  test "vec_quantize_binary errors" do
    vector = SqliteVec.Bit.new([0, 0, 0, 0, 0, 0, 0, 1])

    assert_raise Exqlite.Error, fn ->
      Repo.one(from("test", select: vec_quantize_binary(vec_bit(vector))))
    end
  end

  @tag :skip
  test "cast" do
    embedding = [1, 2]
    items = Repo.all(from(i in BitItem, where: i.embedding == ^embedding))
    assert Enum.map(items, fn v -> v.id end) == [1]
  end
end
