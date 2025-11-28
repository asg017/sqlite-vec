
module SqliteVec
  class Error < StandardError; end

  # Read version from VERSION file
  VERSION = File.read(File.expand_path('../VERSION', __dir__)).strip

  def self.loadable_path
    File.expand_path('../dist/vec0', __dir__)
  end

  def self.load(db)
    db.load_extension(self.loadable_path)
  end
end
