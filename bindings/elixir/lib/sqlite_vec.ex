defmodule SqliteVec do
  @moduledoc """
  Downloads the precompiled loadable library of `sqlite-vec` from GitHub releases.
  """

  @doc """
  Provides the path to the downloaded loadable library.
  """
  def path() do
    version = Application.get_env(:sqlite_vec, :version, SqliteVec.Downloader.default_version())

    Application.app_dir(:sqlite_vec, "priv/#{version}/vec0")
  end

  @doc """
  Downloads the specified `version` to `output_dir`.
  """
  def download(output_dir, version) do
    SqliteVec.Downloader.download(output_dir, override_version: version)
  end
end
