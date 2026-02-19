defmodule SqliteVec.Int8.Test do
  use ExUnit.Case
  use ExUnitProperties

  doctest SqliteVec.Int8

  defp int8_generator do
    gen all(integer <- StreamData.integer()) do
      <<int8::signed-integer-8>> = <<integer>>

      int8
    end
  end

  defp shape_generator do
    {StreamData.positive_integer()}
  end

  defp s8_tensor_generator do
    gen all(seed <- StreamData.integer(), shape <- shape_generator()) do
      type = {:s, 8}
      key = Nx.Random.key(seed)

      min = Nx.Constants.min_finite(type) |> Nx.to_number()
      max = Nx.Constants.max_finite(type) |> Nx.to_number()

      {tensor, _key} = Nx.Random.randint(key, min, max, shape: shape, type: type)
      tensor
    end
  end

  test "creating vector from vector works" do
    vector = SqliteVec.Int8.new([1, 2, 3])
    assert vector == vector |> SqliteVec.Int8.new()
  end

  test "creating vector from list works" do
    list = [1, 2, 3]
    assert list == list |> SqliteVec.Int8.new() |> SqliteVec.Int8.to_list()
  end

  test "creating vector from empty list errors" do
    assert_raise ArgumentError, fn -> SqliteVec.Int8.new([]) end
  end

  test "list elements are expected to be valid int8 values" do
    assert_raise ArgumentError, fn -> SqliteVec.Int8.new([128]) end
    assert_raise ArgumentError, fn -> SqliteVec.Int8.new([-129]) end

    assert SqliteVec.Int8.new([127])
    assert SqliteVec.Int8.new([-128])
  end

  property "creating vector from list of int8 and calling to_list/1 returns original list" do
    check all(list <- StreamData.list_of(int8_generator(), min_length: 1)) do
      assert list == list |> SqliteVec.Int8.new() |> SqliteVec.Int8.to_list()
    end
  end

  test "creating vector from tensor of type s8 works" do
    tensor = Nx.tensor([1, 2, 3], type: :s8)
    assert tensor == tensor |> SqliteVec.Int8.new() |> SqliteVec.Int8.to_tensor()
  end

  test "creating vector from tensor that's not of type s8 errors" do
    types =
      [
        :u2,
        :u4,
        :u8,
        :u16,
        :u32,
        :u64,
        :s2,
        :s4,
        # :s8,
        :s16,
        :s32,
        :s64,
        :f8,
        :f16,
        :f32,
        :f64,
        :bf16,
        :c64,
        :c128
      ]

    for type <- types do
      assert_raise ArgumentError, fn -> SqliteVec.Int8.new(Nx.tensor([1], type: type)) end
    end
  end

  property "creating vector from tensor of type :s8 and calling to_tensor/1 returns original tensor" do
    check all(tensor <- s8_tensor_generator()) do
      assert tensor ==
               tensor
               |> SqliteVec.Int8.new()
               |> SqliteVec.Int8.to_tensor()
               |> Nx.as_type(Nx.type(tensor))
    end
  end

  test "inspect" do
    vector = SqliteVec.Int8.new([1, 2, 3])
    assert "vec_int8('[1, 2, 3]')" == inspect(vector)
  end

  test "equals" do
    assert SqliteVec.Int8.new([1, 2, 3]) == SqliteVec.Int8.new([1, 2, 3])
    refute SqliteVec.Int8.new([1, 2, 3]) == SqliteVec.Int8.new([1, 2, 4])
  end
end
