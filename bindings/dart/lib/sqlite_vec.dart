import 'dart:ffi';
import 'dart:io';

const String _libName = 'sqlite_vec';

/// The dynamic library in which the symbols for [SqliteVecBindings] can be found.
final DynamicLibrary vec0 = () {
  if (Platform.isMacOS || Platform.isIOS) {
    return DynamicLibrary.open('$_libName.framework/$_libName');
  }
  if (Platform.isAndroid || Platform.isLinux) {
    return DynamicLibrary.open('lib$_libName.so');
  }
  if (Platform.isWindows) {
    return DynamicLibrary.open('$_libName.dll');
  }
  throw UnsupportedError('Unknown platform: ${Platform.operatingSystem}');
}();
