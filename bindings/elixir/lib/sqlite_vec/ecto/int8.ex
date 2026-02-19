if Code.ensure_loaded?(Ecto) do
  defmodule SqliteVec.Ecto.Int8 do
    @moduledoc """
    `Ecto.Type` for `SqliteVec.Int8`
    """
    use Ecto.Type

    def type, do: :binary

    def cast(value) do
      {:ok, SqliteVec.Int8.new(value)}
    end

    def load(data) do
      {:ok, SqliteVec.Int8.from_binary(data)}
    end

    def dump(%SqliteVec.Int8{} = vector) do
      {:ok, SqliteVec.Int8.to_binary(vector)}
    end

    def dump(_), do: :error
  end
end
