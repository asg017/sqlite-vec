defmodule SqliteVec.Bit do
  @moduledoc """
  A vector struct for bit vectors.
  Vectors are stored as binaries.

  > ### Consider endianness {: .warning}
  >
  > When returned from `sqlite-vec` or created from `Nx.Tensor`, `SqliteVec.Bit.Vector` holds data in system endianness.
  > You must consider endianness when converting the binary data to a list of numbers.

      iex> v = SqliteVec.Bit.new(Nx.tensor([-1.0, 2.0], type: :f32))
      ...> b = SqliteVec.Bit.to_binary(v)
      ...> <<f1::float-32, f2::float-32>> = b
      ...> [f1, f2]
      case System.endianness() do
        :big -> [-1.0, 2.0]
        :little -> [4.618539608568165e-41, 8.96831017167883e-44]
      end
  """

  @type t :: %__MODULE__{data: binary()}

  defstruct [:data]

  @doc """
  Creates a new vector from a vector, list, or tensor.

  The vector must be a `SqliteVec.Bit` vector.
  The list must only contain values of 0 or 1 and must have a length that's divisible by 8.
  The tensor must have a rank of 1 and a type size that's divisible by 8.

  ## Examples
      iex> SqliteVec.Bit.new([0, 0, 0, 0, 0, 0, 0, 1])
      %SqliteVec.Bit{data: <<0b00000001>>}

      iex> v1 = SqliteVec.Bit.new([0, 0, 0, 0, 0, 0, 0, 1])
      ...> SqliteVec.Bit.new(v1)
      %SqliteVec.Bit{data: <<0b00000001>>}

      iex> SqliteVec.Bit.new(Nx.tensor([1, 2, 3], type: :u8))
      %SqliteVec.Bit{data: <<1::signed-integer-8, 2::signed-integer-8, 3::signed-integer-8>>}
  """
  def new(vector_or_list_or_tensor)

  def new(%SqliteVec.Bit{} = vector) do
    vector
  end

  def new(list) when is_list(list) do
    if list == [] do
      raise ArgumentError, "list must not be empty"
    end

    if not length_divisible_by_8?(list) do
      raise ArgumentError, "expected list length to be divisible by 8"
    end

    if Enum.any?(list, &(not bit?(&1))) do
      raise ArgumentError, "expected list elements to be 0 or 1"
    end

    bin = for v <- list, into: <<>>, do: <<v::1>>

    from_binary(<<bin::binary>>)
  end

  if Code.ensure_loaded?(Nx) do
    def new(tensor) when is_struct(tensor, Nx.Tensor) do
      if Nx.rank(tensor) != 1 do
        raise ArgumentError, "expected rank to be 1"
      end

      if not binary_type_size?(Nx.type(tensor)) do
        raise ArgumentError, "expected type size to be divisible by 8"
      end

      bin = Nx.to_binary(tensor)
      from_binary(<<bin::binary>>)
    end

    defp binary_type_size?({_type, size}), do: rem(size, 8) == 0
  end

  defp length_divisible_by_8?(list) do
    rem(length(list), 8) == 0
  end

  defp bit?(0), do: true
  defp bit?(1), do: true
  defp bit?(_), do: false

  @doc """
  Creates a new vector from its binary representation
  """
  def from_binary(binary) when is_binary(binary) do
    %SqliteVec.Bit{data: binary}
  end

  @doc """
  Converts the vector to its binary representation
  """
  def to_binary(vector) when is_struct(vector, SqliteVec.Bit) do
    vector.data
  end

  @doc """
  Converts the vector to a list of bits
  """
  def to_list(vector) when is_struct(vector, SqliteVec.Bit) do
    <<bin::binary>> = vector.data

    for <<v::1 <- bin>>, do: v
  end

  if Code.ensure_loaded?(Nx) do
    @doc """
    Converts the vector to a tensor
    """
    def to_tensor(vector) when is_struct(vector, SqliteVec.Bit) do
      <<bin::binary>> = vector.data
      Nx.from_binary(bin, :u8)
    end
  end
end

defimpl Inspect, for: SqliteVec.Bit do
  import Inspect.Algebra

  def inspect(vector, opts) do
    concat(["vec_bit('", Inspect.List.inspect(SqliteVec.Bit.to_list(vector), opts), "')"])
  end
end
