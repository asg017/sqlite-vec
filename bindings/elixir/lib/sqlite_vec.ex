defmodule SqliteVec do
  @moduledoc false

  @target System.get_env("MIX_TARGET", "")

  @doc """
  Provides the path to the loadable library.
  """
  def path() do
    Path.join([:code.priv_dir(:sqlite_vec), @target, "vec0"])
  end
end
