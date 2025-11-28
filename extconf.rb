require 'mkmf'

# Run vendor script to get dependencies
system('./scripts/vendor.sh') or abort "Failed to run vendor.sh"

# Build the loadable extension
system('make loadable') or abort "Failed to build extension"

# Create a dummy Makefile since we already built with make loadable
File.open("Makefile", "w") do |f|
  f.puts "install:"
  f.puts "\tmkdir -p $(DESTDIR)$(sitearchdir)"
  f.puts "\tcp dist/vec0.so $(DESTDIR)$(sitearchdir)/vec0.so 2>/dev/null || cp dist/vec0.dylib $(DESTDIR)$(sitearchdir)/vec0.dylib 2>/dev/null || cp dist/vec0.dll $(DESTDIR)$(sitearchdir)/vec0.dll 2>/dev/null || true"
  f.puts "clean:"
  f.puts "\t@true"
end
