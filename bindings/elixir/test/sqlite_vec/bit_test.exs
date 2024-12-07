defmodule SqliteVec.Bit.Test do
  use ExUnit.Case
  use ExUnitProperties

  doctest SqliteVec.Bit

  defp shape_generator do
    {StreamData.positive_integer()}
  end

  defp type_generator do
    [
      # {:u, 2},
      # {:u, 4},
      {:u, 8},
      {:u, 16},
      {:u, 32},
      {:u, 64},
      # {:s, 2},
      # {:s, 4},
      {:s, 8},
      {:s, 16},
      {:s, 32},
      {:s, 64},
      {:f, 8},
      {:f, 16},
      {:f, 32},
      {:f, 64},
      {:bf, 16},
      {:c, 64},
      {:c, 128}
    ]
    |> Enum.map(&StreamData.constant(&1))
    |> StreamData.one_of()
  end

  defp tensor_generator do
    gen all(seed <- StreamData.integer(), shape <- shape_generator(), type <- type_generator()) do
      key = Nx.Random.key(seed)

      {tensor, _key} =
        case type do
          {:s, _} -> random_integer(type, shape, key)
          {:u, _} -> random_integer(type, shape, key)
          {:f, _} -> random_float(type, shape, key)
          {:bf, _} -> random_float(type, shape, key)
          {:c, _} -> random_complex(type, shape, key)
        end

      tensor
    end
    |> StreamData.filter(&finite?(&1))
  end

  defp random_integer(type, shape, key) do
    min = Nx.Constants.min_finite(type) |> Nx.to_number()
    max = Nx.Constants.max_finite(type) |> Nx.to_number()
    Nx.Random.randint(key, min, max, shape: shape, type: type)
  end

  defp random_float(type, shape, key) do
    min = (Nx.Constants.min_finite(type) |> Nx.to_number()) / 2
    max = (Nx.Constants.max_finite(type) |> Nx.to_number()) / 2
    Nx.Random.uniform(key, min, max, shape: shape, type: type)
  end

  defp random_complex(type, shape, key) do
    Nx.Random.uniform(key, shape: shape, type: type)
  end

  defp finite?(tensor) do
    tensor |> Nx.is_infinity() |> Nx.any() |> Nx.to_number() == 0
  end

  test "creating vector from vector works" do
    vector = SqliteVec.Bit.new([1, 0, 1, 0, 1, 1, 1, 1])
    assert vector == vector |> SqliteVec.Bit.new()
  end

  test "creating vector from list of bits works" do
    list = [1, 0, 1, 0, 1, 1, 1, 1]
    assert list == list |> SqliteVec.Bit.new() |> SqliteVec.Bit.to_list()
  end

  test "creating vector from empty list errors" do
    assert_raise ArgumentError, fn -> SqliteVec.Bit.new([]) end
  end

  test "list length must be divisible by 8" do
    list = [1, 0, 0, 0]
    assert_raise ArgumentError, fn -> SqliteVec.Bit.new(list) end
  end

  test "list elements are expected to be 0 or 1" do
    list = [2, 1, 1, 1, 1, 1, 1, 1]
    assert_raise ArgumentError, fn -> SqliteVec.Bit.new(list) end
  end

  property "creating vector from list of bits and calling to_list/1 returns original list" do
    check all(
            bytes <- StreamData.positive_integer(),
            bitlist <- StreamData.list_of(StreamData.integer(0..1), length: 8 * bytes)
          ) do
      assert bitlist == bitlist |> SqliteVec.Bit.new() |> SqliteVec.Bit.to_list()
    end
  end

  test "creating vector from tensor works" do
    tensor = Nx.tensor([1, 2, 3], type: :u8)

    assert tensor == tensor |> SqliteVec.Bit.new() |> SqliteVec.Bit.to_tensor()
  end

  test "creating vector from tensor of a type with size that's not divisible by 8 errors" do
    types = [:u2, :u4, :s2, :s4]

    for type <- types do
      assert_raise ArgumentError, fn -> SqliteVec.Bit.new(Nx.tensor([1], type: type)) end
    end
  end

  property "creating vector from tensor and calling to_tensor/1 returns u8 tensor with original binary value" do
    check all(tensor <- tensor_generator()) do
      assert Nx.to_binary(tensor) ==
               tensor
               |> SqliteVec.Bit.new()
               |> SqliteVec.Bit.to_tensor()
               |> Nx.to_binary()
    end
  end

  test "inspect" do
    vector = SqliteVec.Bit.new([1, 0, 1, 0, 1, 1, 1, 1])
    assert "vec_bit('[1, 0, 1, 0, 1, 1, 1, 1]')" == inspect(vector)
  end

  test "equals" do
    assert SqliteVec.Bit.new([0, 0, 0, 0, 1, 1, 1, 1]) ==
             SqliteVec.Bit.new([0, 0, 0, 0, 1, 1, 1, 1])

    refute SqliteVec.Bit.new([1, 0, 0, 0, 1, 1, 1, 1]) ==
             SqliteVec.Bit.new([0, 0, 0, 0, 1, 1, 1, 1])
  end
end
