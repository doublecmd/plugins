## Installation Instructions for Diskdir plugin

1. You must have FPC and Lazarus installed (Double Commander is compiled with FPC 3.0.4 and Lazarus 1.8.2 now): packages `fpc`, `fpc-src` and `lazarus`. For ubuntu, just do `sudo apt-get install lazarus`, this will install everything.
1. download https://github.com/doublecmd/plugins/archive/master.zip and unpack
1. launch Lazarus IDE (skip the wizard)
1. Project > Open Project, open plugins-master/wcx/diskdir/src/diskdir.lpi
1. Run > Compile
1. result plugins-master/wcx/diskdir/diskdir.wcx;
1. move diskdir.wcx to %commander_path%/plugins/wcx/diskdir/diskdir.wcx. On ubuntu, %commander_path% is /usr/lib/doublecmd
1. in Double Commander: Configuration > Options > Plugins > Packer plugins, Add > find diskdir.wcx, Enter extension > lst > Apply > Ok
