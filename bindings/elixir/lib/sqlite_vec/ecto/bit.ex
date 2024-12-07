if Code.ensure_loaded?(Ecto) do
  defmodule SqliteVec.Ecto.Bit do
    @moduledoc """
    `Ecto.Type` for `SqliteVec.Bit`
    """
    use Ecto.Type

    def type, do: :binary

    def cast(value) do
      {:ok, SqliteVec.Bit.new(value)}
    end

    def load(data) do
      {:ok, SqliteVec.Bit.from_binary(data)}
    end

    def dump(%SqliteVec.Bit{} = vector) do
      {:ok, SqliteVec.Bit.to_binary(vector)}
    end

    def dump(_), do: :error
  end
end
