const os = require('os');

const platform = os.platform();

const arch = os.arch();

let binary;

if (platform === 'linux' && arch === 'x64') {

  binary = require('sqlite-vec-linux-x64');

} else if (platform === 'linux' && arch === 'arm64') {

  binary = require('sqlite-vec-linux-arm64');

} else if (platform === 'darwin' && arch === 'x64') {

  binary = require('sqlite-vec-macos-x64');

} else if (platform === 'darwin' && arch === 'arm64') {

  binary = require('sqlite-vec-macos-arm64');

} else if (platform === 'win32' && arch === 'x64') {

  binary = require('sqlite-vec-windows-x64');

} else if (platform === 'android' && arch === 'arm64') {

  binary = require('sqlite-vec-android-arm64');

} else {

  throw new Error(`Unsupported platform: ${platform} ${arch}. Please install the appropriate sqlite-vec binary package.`);

}

module.exports = {

  load: function(db) {

    db.loadExtension(binary);

  }

};