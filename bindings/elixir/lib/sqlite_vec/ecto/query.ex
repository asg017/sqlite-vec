if Code.ensure_loaded?(Ecto) do
  defmodule SqliteVec.Ecto.Query do
    @moduledoc """
    Macros for Ecto
    """

    @doc """
    Creates a bit vector
    """
    defmacro vec_bit(vector) do
      quote do
        fragment("vec_bit(?)", type(^unquote(vector).data, :binary))
      end
    end

    @doc """
    Creates an int8 vector
    """
    defmacro vec_int8(vector) do
      quote do
        fragment("vec_int8(?)", type(^unquote(vector).data, :binary))
      end
    end

    @doc """
    Creates a float32 vector
    """
    defmacro vec_f32(vector) do
      quote do
        fragment("vec_f32(?)", type(^unquote(vector).data, :binary))
      end
    end

    @doc """
    Performs a K-nearest-neighbors (KNN) query. You must specify a LIMIT or 'k = ?' constraint.
    """
    defmacro match(left, right) do
      quote do
        fragment("? match ?", unquote(left), unquote(right))
      end
    end

    @doc """
    Calculates the L2 euclidian distance between vectors a and b. Only valid for float32 or int8 vectors.

    Returns an error under the following conditions:
     -  a or b are invalid vectors
     -  a or b do not share the same vector element types (ex float32 or int8)
     -  a or b are bit vectors. Use vec_distance_hamming() for distance calculations between two bitvectors.
     -  a or b do not have the same length.
    """
    # credo:disable-for-next-line Credo.Check.Readability.FunctionNames
    defmacro vec_distance_L2(a, b) do
      quote do
        fragment("vec_distance_L2(?, ?)", unquote(a), unquote(b))
      end
    end

    @doc """
    Calculates the cosine distance between vectors a and b. Only valid for float32 or int8 vectors.

    Returns an error under the following conditions:
     - a or b are invalid vectors
     - a or b do not share the same vector element types (ex float32 or int8)
     - a or b are bit vectors. Use vec_distance_hamming() for distance calculations between two bitvectors.
     - a or b do not have the same length
    """
    defmacro vec_distance_cosine(a, b) do
      quote do
        fragment("vec_distance_cosine(?, ?)", unquote(a), unquote(b))
      end
    end

    @doc """
    Calculates the hamming distance between two bitvectors a and b. Only valid for bitvectors.

    Returns an error under the following conditions:
     - a or b are not bitvectors
     - a and b do not share the same length
     - Memory cannot be allocated    
    """
    defmacro vec_distance_hamming(a, b) do
      quote do
        fragment("vec_distance_hamming(?, ?)", unquote(a), unquote(b))
      end
    end

    @doc """
    Returns the number of elements in the given vector
    """
    defmacro vec_length(vector) do
      quote do
        fragment("vec_length(?)", unquote(vector))
      end
    end

    @doc """
    Returns the name of the type of `vector` as text
    """
    defmacro vec_type(vector) do
      quote do
        fragment("vec_type(?)", unquote(vector))
      end
    end

    @doc """
    Adds every element in vector a with vector b, returning a new vector c.
    Both vectors must be of the same type and same length.
    Only float32 and int8 vectors are supported.

    An error is raised if either a or b are invalid, or if they are not the same type or same length.
    """
    defmacro vec_add(a, b) do
      quote do
        fragment("vec_add(?, ?)", unquote(a), unquote(b))
      end
    end

    @doc """
    Subtracts every element in vector a with vector b, returning a new vector c.
    Both vectors must be of the same type and same length.
    Only float32 and int8 vectors are supported.

    An error is raised if either a or b are invalid, or if they are not the same type or same length.
    """
    defmacro vec_sub(a, b) do
      quote do
        fragment("vec_sub(?, ?)", unquote(a), unquote(b))
      end
    end

    @doc """
    Performs L2 normalization on the given vector.
    Only float32 vectors are currently supported.

    Returns an error if the input is an invalid vector or not a float32 vector.
    """
    defmacro vec_normalize(vector) do
      quote do
        fragment("vec_normalize(?)", unquote(vector))
      end
    end

    @doc """
    Extract a subset of vector from the start element (inclusive) to the end element (exclusive).

    This is especially useful for Matryoshka embeddings, also known as "adaptive length" embeddings.
    Use with vec_normalize() to get proper results.

    Returns an error in the following conditions:
     - If vector is not a valid vector
     - If start is less than zero or greater than or equal to end
     - If end is greater than the length of vector, or less than or equal to start.
     - If vector is a bitvector, start and end must be divisible by 8.
    """
    defmacro vec_slice(vector, start_index, end_index) do
      quote do
        fragment("vec_slice(?, ?, ?)", unquote(vector), unquote(start_index), unquote(end_index))
      end
    end

    @doc """
    Represents a vector as JSON text.
    The input vector can be a vector BLOB or JSON text.

    Returns an error if vector is an invalid vector, or when memory cannot be allocated.
    """
    defmacro vec_to_json(vector) do
      quote do
        fragment("vec_to_json(?)", unquote(vector))
      end
    end

    @doc """
    Quantize a float32 or int8 vector into a bitvector.
    For every element in the vector, a 1 is assigned to positive numbers and a 0 is assigned to negative numbers.
    These values are then packed into a bit vector.

    Returns an error if vector is invalid, or if vector is not a float32 or int8 vector.
    """
    defmacro vec_quantize_binary(vector) do
      quote do
        fragment("vec_quantize_binary(?)", unquote(vector))
      end
    end
  end
end
