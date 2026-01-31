# frozen_string_literal: true

Gem::Specification.new do |spec|
  spec.name          = "sqlite-vec"
  spec.version       = "0.2.4.alpha"
  spec.authors       = ["Alex Garcia", "Vlad Lasky"]
  spec.email         = ["alex@alex.garcia"]

  spec.summary       = "A vector search SQLite extension that runs anywhere"
  spec.description   = "sqlite-vec is a SQLite extension for vector search, supporting float, int8, and binary vectors with minimal dependencies"
  spec.homepage      = "https://github.com/vlasky/sqlite-vec"
  spec.licenses      = ["MIT", "Apache-2.0"]
  spec.required_ruby_version = ">= 2.6.0"

  spec.metadata["homepage_uri"] = spec.homepage
  spec.metadata["source_code_uri"] = "https://github.com/vlasky/sqlite-vec"
  spec.metadata["changelog_uri"] = "https://github.com/vlasky/sqlite-vec/blob/main/CHANGELOG.md"

  # Specify which files should be added to the gem when it is released.
  spec.files = Dir[
    "lib/**/*",
    "sqlite-vec.c",
    "sqlite-vec.h",
    "sqlite-vec.h.tmpl",
    "VERSION",
    "vendor/**/*",
    "scripts/vendor.sh",
    "Makefile",
    "extconf.rb",
    "LICENSE*",
    "README.md"
  ]

  spec.require_paths = ["lib"]

  # Configure native extension build
  spec.extensions = ["extconf.rb"]

  # Build the extension during gem install
  spec.post_install_message = <<~MSG
    sqlite-vec installed successfully!

    Load the extension in Ruby with:
      db.enable_load_extension(true)
      db.load_extension('vec0')

    See https://github.com/vlasky/sqlite-vec for documentation.
  MSG
end
