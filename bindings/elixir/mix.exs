defmodule SqliteVec.MixProject do
  use Mix.Project

  @source_url "https://github.com/joelpaulkoch/sqlite_vec"
  @version "0.1.0"

  def project do
    [
      app: :sqlite_vec,
      version: @version,
      elixir: "~> 1.17",
      start_permanent: Mix.env() == :prod,
      deps: deps(),
      compilers: Mix.compilers() ++ [:download_sqlite_vec],
      aliases: [
        "compile.download_sqlite_vec": &download_sqlite_vec/1
      ],
      preferred_cli_env: [
        "test.watch": :test
      ],
      name: "SqliteVec",
      package: package(),
      docs: docs(),
      description: "A wrapper around sqlite-vec",
      source_url: @source_url,
      homepage_url: @source_url
    ]
  end

  defp download_sqlite_vec(_) do
    version = Application.get_env(:sqlite_vec, :version, SqliteVec.Downloader.default_version())

    output_dir = Path.join(__DIR__, "priv/#{version}")
    File.mkdir_p!(output_dir)

    case SqliteVec.download(output_dir, version) do
      :skip ->
        :ok

      {:ok, _successful_files, []} ->
        :ok

      {:ok, _successful_files, failed_files} ->
        message = "failed to download: " <> Enum.join(failed_files, ", ")
        raise(message)

      {:error, message} ->
        raise(message)
    end
  end

  defp deps do
    [
      {:octo_fetch, "~> 0.4.0"},
      {:ecto, "~> 3.0", optional: true},
      {:nx, "~> 0.9", optional: true},
      {:ecto_sql, "~> 3.0", only: :test},
      {:ecto_sqlite3, "~> 0.17", only: :test},
      {:stream_data, "~> 1.0", only: :test},
      {:ex_doc, "~> 0.34", only: :dev, runtime: false},
      {:mix_test_watch, "~> 1.0", only: [:dev, :test], runtime: false},
      {:credo, "~> 1.7", only: [:dev, :test], runtime: false},
      {:doctest_formatter, "~> 0.3.1", only: [:dev, :test], runtime: false}
    ]
  end

  defp package do
    [
      maintainers: ["Joel Koch"],
      licenses: ["MIT"],
      links: %{
        GitHub: "https://github.com/joelpaulkoch/sqlite_vec"
      },
      files: ~w(lib priv/.gitkeep .formatter.exs mix.exs README.md LICENSE)
    ]
  end

  defp docs do
    [
      main: "readme",
      source_ref: "v#{@version}",
      source_url: @source_url,
      extras: [
        {"README.md", title: "README"},
        "notebooks/getting_started.livemd",
        "notebooks/usage_with_ecto.livemd"
      ],
      groups_for_extras: [
        Notebooks: [
          "notebooks/getting_started.livemd",
          "notebooks/usage_with_ecto.livemd"
        ]
      ]
    ]
  end
end
