defmodule Float32Item do
  use Ecto.Schema

  schema "float32_ecto_items" do
    field(:embedding, SqliteVec.Ecto.Float32)
  end
end

defmodule EctoTest do
  use ExUnit.Case, async: false

  import Ecto.Query
  import SqliteVec.Ecto.Query

  setup_all do
    Ecto.Adapters.SQL.query!(Repo, "DROP TABLE IF EXISTS test", [])

    Ecto.Adapters.SQL.query!(Repo, "CREATE TABLE test (some_column)", [])

    Ecto.Adapters.SQL.query!(Repo, "INSERT INTO test (some_column) VALUES ($1)", ["test dummy"])

    Ecto.Adapters.SQL.query!(Repo, "DROP TABLE IF EXISTS float32_ecto_items", [])

    Ecto.Adapters.SQL.query!(
      Repo,
      "CREATE VIRTUAL TABLE float32_ecto_items USING vec0(id INTEGER PRIMARY KEY, embedding float[2])",
      []
    )

    create_items()
    :ok
  end

  defp create_items do
    Repo.insert(%Float32Item{
      embedding: SqliteVec.Float32.new([1, 2])
    })

    Repo.insert(%Float32Item{
      embedding: SqliteVec.Float32.new([52.0, 43.0])
    })

    Repo.insert(%Float32Item{
      embedding: SqliteVec.Float32.new(Nx.tensor([3, 4], type: :f32))
    })
  end

  test "match performs a KNN query" do
    v = SqliteVec.Float32.new([2, 2])

    items =
      Repo.all(
        from(i in Float32Item,
          where: match(i.embedding, vec_f32(v)),
          limit: 3
        )
      )

    assert Enum.map(items, fn v ->
             v.id
           end) == [1, 3, 2]

    assert Enum.map(items, fn v -> v.embedding |> SqliteVec.Float32.to_list() end) == [
             [1.0, 2.0],
             [3.0, 4.0],
             [52.0, 43.0]
           ]
  end

  test "vector l2 distance" do
    v = SqliteVec.Float32.new([2, 2])

    items =
      Repo.all(
        from(i in Float32Item,
          order_by: vec_distance_L2(i.embedding, vec_f32(v)),
          limit: 5
        )
      )

    assert Enum.map(items, fn v ->
             v.id
           end) == [1, 3, 2]

    assert Enum.map(items, fn v -> v.embedding |> SqliteVec.Float32.to_list() end) == [
             [1.0, 2.0],
             [3.0, 4.0],
             [52.0, 43.0]
           ]
  end

  test "vector cosine distance" do
    items =
      Repo.all(
        from(i in Float32Item,
          order_by: vec_distance_cosine(i.embedding, vec_f32(SqliteVec.Float32.new([1, 1]))),
          limit: 5
        )
      )

    assert Enum.map(items, fn v -> v.id end) == [2, 3, 1]
  end

  test "vector cosine similarity" do
    items =
      Repo.all(
        from(i in Float32Item,
          order_by: 1 - vec_distance_cosine(i.embedding, vec_f32(SqliteVec.Float32.new([1, 1]))),
          limit: 5
        )
      )

    assert Enum.map(items, fn v -> v.id end) == [1, 3, 2]
  end

  test "vec_length returns number of elements of vector" do
    vector = SqliteVec.Float32.new([1, 2, 3])
    assert Repo.one(from("test", select: vec_length(vec_f32(vector)))) == 3
  end

  test "vec_type returns vector type as string" do
    vector = SqliteVec.Float32.new([1, 2, 3])
    assert Repo.one(from("test", select: vec_type(vec_f32(vector)))) == "float32"
  end

  test "vec_add adds two vectors element wise" do
    v1 = SqliteVec.Float32.new([1, 2, 3])
    v2 = SqliteVec.Float32.new([4, 5, 6])

    binary = Repo.one(from("test", select: vec_add(vec_f32(v1), vec_f32(v2))))

    assert SqliteVec.Float32.from_binary(binary) ==
             SqliteVec.Float32.new([5, 7, 9])
  end

  test "vec_sub subtracts two vectors element wise" do
    v1 = SqliteVec.Float32.new([1, 22, 3])
    v2 = SqliteVec.Float32.new([4, 15, 26])

    binary = Repo.one(from("test", select: vec_sub(vec_f32(v1), vec_f32(v2))))

    assert SqliteVec.Float32.from_binary(binary) ==
             SqliteVec.Float32.new([-3, 7, -23])
  end

  test "vec_normalize performs l2 normalization" do
    vector = SqliteVec.Float32.new([3, 4])
    l2_norm = :math.sqrt(3 * 3 + 4 * 4)
    binary = Repo.one(from("test", select: vec_normalize(vec_f32(vector))))

    assert SqliteVec.Float32.from_binary(binary) ==
             SqliteVec.Float32.new([3 / l2_norm, 4 / l2_norm])
  end

  test "vec_slice extracts subset of vector" do
    vector = SqliteVec.Float32.new([1, 2, 3, 4])

    binary = Repo.one(from("test", select: vec_slice(vec_f32(vector), ^1, ^3)))

    assert SqliteVec.Float32.from_binary(binary) ==
             SqliteVec.Float32.new([2, 3])
  end

  test "vec_to_json returns vector as json" do
    vector = SqliteVec.Float32.new([1, 2, 3])

    assert Repo.one(from("test", select: vec_to_json(vec_f32(vector)))) ==
             "[1.000000,2.000000,3.000000]"
  end

  test "vec_quantize_binary quantizes vector into bitvector" do
    vector = SqliteVec.Float32.new([1, -2, 3, -4, -5, 6, -7, -8])
    binary = Repo.one(from("test", select: vec_quantize_binary(vec_f32(vector))))

    assert binary == <<0b00100101>>
  end

  test "cast" do
    embedding = [1.0, 2.0]
    items = Repo.all(from(i in Float32Item, where: i.embedding == ^embedding))
    assert Enum.map(items, fn v -> v.id end) == [1]
  end
end
