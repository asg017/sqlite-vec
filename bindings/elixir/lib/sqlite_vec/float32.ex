defmodule SqliteVec.Float32 do
  @moduledoc """
  A vector struct for float32 vectors.
  Vectors are stored as binaries in the endianness of the system.

  > ### Consider endianness {: .warning}
  >
  > `SqliteVec.Float32.Vector` holds data in system endianness.
  > Therefore, the same vector data will be interpreted differently on another system with different endianness.
  > Moreover, you must consider endianness when converting the binary data directly to a list of numbers.

      iex> v = SqliteVec.Float32.new([-1.0, 2.0])
      ...> b = SqliteVec.Float32.to_binary(v)
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
  Creates a new vector from a vector, list, or tensor

  The vector must be a `SqliteVec.Float32` vector.
  The list may contain any number but the values will be converted to f32 format.
  The tensor must have a rank of 1 and must be of type :f32.

  ## Examples
      iex> SqliteVec.Float32.new([1.0, 2.0])
      %SqliteVec.Float32{data: <<1.0::float-32-native, 2.0::float-32-native>>}

      iex> v1 = SqliteVec.Float32.new([1, 2])
      ...> SqliteVec.Float32.new(v1)
      %SqliteVec.Float32{data: <<1.0::float-32-native, 2.0::float-32-native>>}

      iex> SqliteVec.Float32.new(Nx.tensor([1, 2], type: :f32))
      %SqliteVec.Float32{data: <<1.0::float-32-native, 2.0::float-32-native>>}
  """
  def new(vector_or_list_or_tensor)

  def new(%SqliteVec.Float32{} = vector) do
    vector
  end

  def new(list) when is_list(list) do
    if list == [] do
      raise ArgumentError, "list must not be empty"
    end

    bin = for v <- list, into: <<>>, do: <<v::float-32-native>>
    from_binary(<<bin::binary>>)
  end

  if Code.ensure_loaded?(Nx) do
    def new(tensor) when is_struct(tensor, Nx.Tensor) do
      if Nx.rank(tensor) != 1 do
        raise ArgumentError, "expected rank to be 1"
      end

      if Nx.type(tensor) != {:f, 32} do
        raise ArgumentError, "expected type to be :f32"
      end

      bin = tensor |> Nx.to_binary()
      from_binary(<<bin::binary>>)
    end
  end

  @doc """
  Creates a new vector from its binary representation
  """
  def from_binary(binary) when is_binary(binary) do
    %SqliteVec.Float32{data: binary}
  end

  @doc """
  Converts the vector to its binary representation
  """
  def to_binary(vector) when is_struct(vector, SqliteVec.Float32) do
    vector.data
  end

  @doc """
  Converts the vector to a list
  """
  def to_list(vector) when is_struct(vector, SqliteVec.Float32) do
    <<bin::binary>> = vector.data

    for <<v::float-32-native <- bin>>, do: v
  end

  if Code.ensure_loaded?(Nx) do
    @doc """
    Converts the vector to a tensor
    """
    def to_tensor(vector) when is_struct(vector, SqliteVec.Float32) do
      <<bin::binary>> = vector.data
      Nx.from_binary(bin, :f32)
    end
  end
end

defimpl Inspect, for: SqliteVec.Float32 do
  import Inspect.Algebra

  def inspect(vector, opts) do
    concat(["vec_f32('", Inspect.List.inspect(SqliteVec.Float32.to_list(vector), opts), "')"])
  end
end
