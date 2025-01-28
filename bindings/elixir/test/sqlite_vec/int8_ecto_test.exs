defmodule Int8Item do
  use Ecto.Schema

  schema "int8_ecto_items" do
    field(:embedding, SqliteVec.Ecto.Int8)
  end
end

defmodule Int8EctoTest do
  use ExUnit.Case, async: false

  import Ecto.Query
  import SqliteVec.Ecto.Query

  setup_all do
    Ecto.Adapters.SQL.query!(Repo, "DROP TABLE IF EXISTS test", [])

    Ecto.Adapters.SQL.query!(Repo, "CREATE TABLE test (some_column)", [])

    Ecto.Adapters.SQL.query!(Repo, "INSERT INTO test (some_column) VALUES ($1)", ["test dummy"])

    Ecto.Adapters.SQL.query!(Repo, "DROP TABLE IF EXISTS int8_ecto_items", [])

    Ecto.Adapters.SQL.query!(
      Repo,
      "CREATE VIRTUAL TABLE int8_ecto_items USING vec0(id INTEGER PRIMARY KEY, embedding int8[2])",
      []
    )

    create_items()
    :ok
  end

  defp create_items do
    Ecto.Adapters.SQL.query!(
      Repo,
      "insert into int8_ecto_items(id, embedding) values(1, vec_int8('[1, 2]')), (2, vec_int8('[52, 43]')), (3, vec_int8('[3, 4]'))",
      []
    )

    # Repo.insert(%Int8Item{
    #   embedding: SqliteVec.Int8.new([1, 2])
    # })

    # Repo.insert(%Int8Item{
    #   embedding: SqliteVec.Int8.new([52, 43])
    # })

    # Repo.insert(%Int8Item{
    #   embedding: Nx.tensor([3, 4], type: :s8)
    # })
  end

  test "vector l2 distance" do
    items =
      Repo.all(
        from(i in Int8Item,
          order_by: vec_distance_L2(i.embedding, vec_int8(SqliteVec.Int8.new([2, 2]))),
          limit: 5
        )
      )

    assert Enum.map(items, fn v ->
             v.id
           end) == [1, 3, 2]

    assert Enum.map(items, fn v -> v.embedding |> SqliteVec.Int8.to_list() end) == [
             [1, 2],
             [3, 4],
             [52, 43]
           ]
  end

  test "match performs a KNN query" do
    items =
      Repo.all(
        from(i in Int8Item,
          where: match(i.embedding, vec_int8(SqliteVec.Int8.new([2, 2]))),
          limit: 3
        )
      )

    assert Enum.map(items, fn v ->
             v.id
           end) == [1, 3, 2]

    assert Enum.map(items, fn v -> v.embedding |> SqliteVec.Int8.to_list() end) == [
             [1, 2],
             [3, 4],
             [52, 43]
           ]
  end

  test "vector cosine distance" do
    items =
      Repo.all(
        from(i in Int8Item,
          order_by: vec_distance_cosine(i.embedding, vec_int8(SqliteVec.Int8.new([1, 1]))),
          limit: 5
        )
      )

    assert Enum.map(items, fn v -> v.id end) == [2, 3, 1]
  end

  test "vector cosine similarity" do
    items =
      Repo.all(
        from(i in Int8Item,
          order_by: 1 - vec_distance_cosine(i.embedding, vec_int8(SqliteVec.Int8.new([1, 1]))),
          limit: 5
        )
      )

    assert Enum.map(items, fn v -> v.id end) == [1, 3, 2]
  end

  test "vec_length returns number of elements of vector" do
    vector = SqliteVec.Int8.new([1, 2, 3])
    assert Repo.one(from("test", select: vec_length(vec_int8(vector)))) == 3
  end

  test "vec_type returns vector type as string" do
    vector = SqliteVec.Int8.new([1, 2, 3])
    assert Repo.one(from("test", select: vec_type(vec_int8(vector)))) == "int8"
  end

  test "vec_add adds two vectors element wise" do
    v1 = SqliteVec.Int8.new([1, 2, 3])
    v2 = SqliteVec.Int8.new([4, 5, 6])

    binary = Repo.one(from("test", select: vec_add(vec_int8(v1), vec_int8(v2))))

    assert SqliteVec.Int8.from_binary(binary) ==
             SqliteVec.Int8.new([5, 7, 9])
  end

  test "vec_sub subtracts two vectors element wise" do
    v1 = SqliteVec.Int8.new([1, 22, 3])
    v2 = SqliteVec.Int8.new([4, 15, 26])

    binary = Repo.one(from("test", select: vec_sub(vec_int8(v1), vec_int8(v2))))

    assert SqliteVec.Int8.from_binary(binary) ==
             SqliteVec.Int8.new([-3, 7, -23])
  end

  test "vec_normalize errors" do
    vector = SqliteVec.Int8.new([3, 4])

    assert_raise Exqlite.Error, fn ->
      Repo.one(from("test", select: vec_normalize(vec_int8(vector))))
    end
  end

  test "vec_slice extracts subset of vector" do
    vector = SqliteVec.Int8.new([1, 2, 3, 4])

    binary = Repo.one(from("test", select: vec_slice(vec_int8(vector), ^1, ^3)))

    assert SqliteVec.Int8.from_binary(binary) ==
             SqliteVec.Int8.new([2, 3])
  end

  test "vec_to_json returns vector as json" do
    vector = SqliteVec.Int8.new([1, 2, 3])

    assert Repo.one(from("test", select: vec_to_json(vec_int8(vector)))) ==
             "[1,2,3]"
  end

  test "vec_quantize_binary quantizes vector into bitvector" do
    vector = SqliteVec.Int8.new([1, -2, 3, -4, -5, 6, -7, -8])
    binary = Repo.one(from("test", select: vec_quantize_binary(vec_int8(vector))))

    assert binary == <<0b00100101>>
  end

  @tag :skip
  test "cast" do
    embedding = [1, 2]
    items = Repo.all(from(i in Int8Item, where: i.embedding == ^embedding))
    assert Enum.map(items, fn v -> v.id end) == [1]
  end
end
