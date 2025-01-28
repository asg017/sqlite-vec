defmodule SqliteVec.Downloader do
  @moduledoc """
  The downloader module for `SqliteVec`.
  If the configured version already exists, the download is skipped.
  Otherwise, the corresponding GitHub release artifact will be downloaded.
  Afterwards, all other previously downloaded versions will be deleted.
  """

  use OctoFetch,
    latest_version: "0.1.6",
    github_repo: "asg017/sqlite-vec",
    download_versions: %{
      "0.1.6" => [
        {:darwin, :amd64, "35d014e5f7bcac52645a97f1f1ca34fdb51dcd61d81ac6e6ba1c712393fbf8fd"},
        {:darwin, :arm64, "142e195b654092632fecfadbad2825f3140026257a70842778637597f6b8c827"},
        {:linux, :amd64, "438e0df29f3f8db3525b3aa0dcc0a199869c0bcec9d7abc5b51850469caf867f"},
        {:linux, :arm64, "d6e4ba12c5c0186eaab42fb4449b311008d86ffd943e6377d7d88018cffab3aa"},
        {:windows, :amd64, "f1c615577ad2e692d1e2fe046fe65994dafd8a8cae43e9e864f5f682dc295964"}
      ],
      "0.1.5" => [
        {:darwin, :amd64, "1daa90b7cdda7e873af4636a20a2b6daf0ebd4d664f2bbbcc2ffeae219bf34b6"},
        {:darwin, :arm64, "348ea4ce39b4b4749b19ee93e5e9674d6ed7616e3e313cb20f6354cdecbebc75"},
        {:linux, :amd64, "626bb9b66896269facdf7f87d94c308bf0523cb1e584ff7ff5b3f51936f21d24"},
        {:linux, :arm64, "8ce460c1f2adcbbc709f5ca1d1a3578c34c62c131d1a044bd3ff7c0729be2137"},
        {:windows, :amd64, "cfd31e96d2edf27749c4c2063134737fc98ac87b6e113acf204db57563b078bc"}
      ],
      "0.1.4" => [
        {:darwin, :amd64, "1be7676e9e63c427fe0ce84b738c1c9012f2bbb4b81ecc63719b5552f07e1b26"},
        {:darwin, :arm64, "e7962da8acd394ad95cfc4822d573d5b10ac9f93d2dd28b73e76841eb5da45ee"},
        {:linux, :amd64, "2d3855b9953f05aba033536efed3cd2a9cc4518ee009301b0c03b17f9d698819"},
        {:linux, :arm64, "b0b8d2b7b4beb9641417874689e737fe872d79e208c0c306565bd5fbfacb7124"},
        {:windows, :amd64, "39a5575c565af7c135b9f62db9d92aebd7af096cc2b952c8a31b40f674ccf2cf"}
      ],
      "0.1.3" => [
        {:darwin, :amd64, "8ef228a8935883f8b5c52f191a8123909ea48ab58f6eceb5d4c12ada654556cf"},
        {:darwin, :arm64, "c57a552c8a8df823a8deb937f81d8a9ec5c81377e66e86cd5db8508b74ef4068"},
        {:linux, :amd64, "5fa404f6d61de7b462d1f1504332a522a64331103603ca079714f078cdb28606"}
      ],
      "0.1.2" => [
        {:darwin, :amd64, "d2d4d312fac1d609723b75cc777df42f3ff0770903cd89d53ca201c6e10c25f9"},
        {:darwin, :arm64, "a449cb190366ee0080bcab132d788b0f792600bfa8dd7c0aba539444c6e126ba"},
        {:linux, :amd64, "539e6bb92612665e1fd1870df1b2c5db66e327bf5a98aee1666c57fb3c6e128d"}
      ]
    }

  @impl true
  def download_name(version, :darwin, arch), do: download_name(version, :macos, arch)
  def download_name(version, os, :amd64), do: download_name(version, os, :x86_64)
  def download_name(version, os, :arm64), do: download_name(version, os, :aarch64)

  def download_name(version, os, arch), do: "sqlite-vec-#{version}-loadable-#{os}-#{arch}.tar.gz"

  def pre_download_hook(_file, output_dir) do
    if library_exists?(output_dir) do
      :skip
    else
      :cont
    end
  end

  defp library_exists?(output_dir) do
    matches =
      output_dir
      |> Path.join("vec0.*")
      |> Path.wildcard()

    matches != []
  end

  def post_write_hook(file) do
    output_dir = file |> Path.dirname() |> Path.join("..") |> Path.expand()
    current_version = file |> Path.dirname() |> Path.basename()

    remove_other_versions(output_dir, current_version)

    :ok
  end

  defp remove_other_versions(output_dir, current_version) do
    output_dir
    |> Path.join("*")
    |> Path.wildcard()
    |> Enum.filter(fn path -> Path.basename(path) != current_version end)
    |> Enum.map(&File.rm_rf(&1))
  end
end
