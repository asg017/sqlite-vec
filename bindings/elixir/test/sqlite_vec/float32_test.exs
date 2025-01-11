defmodule SqliteVec.Float32.Test do
  use ExUnit.Case
  use ExUnitProperties

  doctest SqliteVec.Float32

  defp float32_generator do
    gen all(float <- StreamData.float()) do
      <<float32::float-32>> = <<float::float-32>>

      float32
    end
  end

  defp shape_generator do
    {StreamData.positive_integer()}
  end

  defp f32_tensor_generator do
    gen all(seed <- StreamData.integer(), shape <- shape_generator()) do
      type = {:f, 32}
      key = Nx.Random.key(seed)

      min = (Nx.Constants.min_finite(type) |> Nx.to_number()) / 2
      max = (Nx.Constants.max_finite(type) |> Nx.to_number()) / 2

      {tensor, _key} = Nx.Random.uniform(key, min, max, shape: shape, type: type)

      tensor
    end
    |> StreamData.filter(&finite?(&1))
  end

  defp finite?(tensor) do
    tensor |> Nx.is_infinity() |> Nx.any() |> Nx.to_number() == 0
  end

  test "creating vector from vector works" do
    vector = SqliteVec.Float32.new([1, 2, 3])
    assert vector == vector |> SqliteVec.Float32.new()
  end

  test "creating vector from list works" do
    list = [1.0, 2.0, 3.0]
    assert list == list |> SqliteVec.Float32.new() |> SqliteVec.Float32.to_list()
  end

  test "creating vector from empty list errors" do
    assert_raise ArgumentError, fn -> SqliteVec.Float32.new([]) end
  end

  property "creating vector from list of float32 and calling to_list/1 returns original list" do
    check all(list <- StreamData.list_of(float32_generator(), min_length: 1)) do
      assert list == list |> SqliteVec.Float32.new() |> SqliteVec.Float32.to_list()
    end
  end

  test "creating vector from tensor of type f32 works" do
    tensor = Nx.tensor([1.0, 2.0, 3.0], type: :f32)
    assert tensor == tensor |> SqliteVec.Float32.new() |> SqliteVec.Float32.to_tensor()
  end

  test "creating vector from tensor that's not of type f32 errors" do
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
        :s8,
        :s16,
        :s32,
        :s64,
        :f8,
        :f16,
        # :f32,
        :f64,
        :bf16,
        :c64,
        :c128
      ]

    for type <- types do
      assert_raise ArgumentError, fn -> SqliteVec.Float32.new(Nx.tensor([1], type: type)) end
    end
  end

  property "creating vector from tensor of type :f32 and calling to_tensor/1 returns original tensor" do
    check all(tensor <- f32_tensor_generator()) do
      assert tensor ==
               tensor
               |> SqliteVec.Float32.new()
               |> SqliteVec.Float32.to_tensor()
               |> Nx.as_type(Nx.type(tensor))
    end
  end

  test "inspect" do
    vector = SqliteVec.Float32.new([1, 2, 3])
    assert "vec_f32('[1.0, 2.0, 3.0]')" == inspect(vector)
  end

  test "equals" do
    assert SqliteVec.Float32.new([1, 2, 3]) == SqliteVec.Float32.new([1, 2, 3])
    refute SqliteVec.Float32.new([1, 2, 3]) == SqliteVec.Float32.new([1, 2, 4])
  end

  test "vectors are stored as binaries in system endianness" do
    case System.endianness() do
      :little ->
        assert SqliteVec.Float32.new([2]).data == <<0x00, 0x00, 0x00, 0x40>>

      :big ->
        assert SqliteVec.Float32.new([2]).data == <<0x40, 0x00, 0x00, 0x00>>
    end
  end
end
