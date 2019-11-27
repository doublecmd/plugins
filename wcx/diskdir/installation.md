# Compilation and installation instructions for DiskDir plugin

## What you need

Free Pascal Compiler (FPC) and Lazarus: install packages `fpc`, `fpc-src` and
`lazarus` from the repository of your distribution, or you can get packages
from the Lazarus IDE website https://www.lazarus-ide.org/index.php?page=downloads
(DEB and RPM packages are available).
For ubuntu, just do `sudo apt-get install lazarus`, this will install everything.
Current version of Double Commander requires at least FPC 3.0.4 and
Lazarus 1.8.2, you can use this versions for DiskDir plugin too.

## Compiling

### Using the Lazarus IDE to build:

1. Download https://github.com/doublecmd/plugins/archive/master.zip and unpack
1. launch Lazarus IDE (skip the wizard)
1. Project > Open Project, open plugins-master/wcx/diskdir/src/diskdir.lpi
1. Run > Compile

The compiled file `diskdir.wcx` will be saved in `plugins-master/wcx/diskdir`

### OR Building from command line:

```bash
git clone https://github.com/doublecmd/plugins.git
cd plugins/wcx/diskdir
lazbuild src/diskdir.lpi
```

Compiled file `diskdir.wcx` will be saved in current directory.

## Installation

1. move `diskdir.wcx` to any convenient directory. Suggestion: `%commander_path%/plugins/wcx/diskdir/diskdir.wcx`. On ubuntu, `%commander_path%` is `/usr/lib/doublecmd`
1. launch Double Commander
1. Configuration > Options > Plugins > Packer plugins, Add > find diskdir.wcx, Enter extension > lst > Apply > Ok
