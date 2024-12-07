defmodule SqliteVec.Int8 do
  @moduledoc """
  A vector struct for int8 vectors.
  Vectors are stored as binaries.
  """

  @type t :: %__MODULE__{data: binary()}

  defstruct [:data]

  @doc """
  Creates a new vector from a vector, list, or tensor

  The vector must be a `SqliteVec.Int8` vector.
  The list must only contain valid int8 values, i.e. values between and including -128 and 127.
  The tensor must have a rank of 1 and must be of type :s8.

  ## Examples
      iex> SqliteVec.Int8.new([1, 2, 3])
      %SqliteVec.Int8{data: <<1::integer-8, 2::integer-8, 3::integer-8>>}

      iex> v1 = SqliteVec.Int8.new([1, 2, 3])
      ...> SqliteVec.Int8.new(v1)
      %SqliteVec.Int8{data: <<1::integer-8, 2::integer-8, 3::integer-8>>}

      iex> SqliteVec.Int8.new(Nx.tensor([1, 2, 3], type: :s8))
      %SqliteVec.Int8{data: <<1::integer-8, 2::integer-8, 3::integer-8>>}
  """
  def new(vector_or_list_or_tensor)

  def new(%SqliteVec.Int8{} = vector) do
    vector
  end

  def new(list) when is_list(list) do
    if list == [] do
      raise ArgumentError, "list must not be empty"
    end

    if Enum.any?(list, &(not valid_int8?(&1))) do
      raise ArgumentError, "expected list elements to be valid int8 values"
    end

    bin = for v <- list, into: <<>>, do: <<v::signed-integer-8>>
    from_binary(<<bin::binary>>)
  end

  if Code.ensure_loaded?(Nx) do
    def new(tensor) when is_struct(tensor, Nx.Tensor) do
      if Nx.rank(tensor) != 1 do
        raise ArgumentError, "expected rank to be 1"
      end

      if Nx.type(tensor) != {:s, 8} do
        raise ArgumentError, "expected type to be :s8"
      end

      bin = Nx.to_binary(tensor)
      from_binary(<<bin::binary>>)
    end
  end

  defp valid_int8?(value) do
    is_integer(value) and -128 <= value and value <= 127
  end

  @doc """
  Creates a new vector from its binary representation
  """
  def from_binary(binary) when is_binary(binary) do
    %SqliteVec.Int8{data: binary}
  end

  @doc """
  Converts the vector to its binary representation
  """
  def to_binary(vector) when is_struct(vector, SqliteVec.Int8) do
    vector.data
  end

  @doc """
  Converts the vector to a list
  """
  def to_list(vector) when is_struct(vector, SqliteVec.Int8) do
    <<bin::binary>> = vector.data

    for <<v::signed-integer-8 <- bin>>, do: v
  end

  if Code.ensure_loaded?(Nx) do
    @doc """
    Converts the vector to a tensor
    """
    def to_tensor(vector) when is_struct(vector, SqliteVec.Int8) do
      <<bin::binary>> = vector.data
      Nx.from_binary(bin, :s8)
    end
  end
end

defimpl Inspect, for: SqliteVec.Int8 do
  import Inspect.Algebra

  def inspect(vector, opts) do
    concat(["vec_int8('", Inspect.List.inspect(SqliteVec.Int8.to_list(vector), opts), "')"])
  end
end
