if Code.ensure_loaded?(Ecto) do
  defmodule SqliteVec.Ecto.Float32 do
    @moduledoc """
    `Ecto.Type` for `SqliteVec.Float32`
    """
    use Ecto.Type

    def type, do: :binary

    def cast(value) do
      {:ok, SqliteVec.Float32.new(value)}
    end

    def load(data) do
      {:ok, SqliteVec.Float32.from_binary(data)}
    end

    def dump(%SqliteVec.Float32{} = vector) do
      {:ok, SqliteVec.Float32.to_binary(vector)}
    end

    def dump(_), do: :error
  end
end
