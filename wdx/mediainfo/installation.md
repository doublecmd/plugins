# Installation instructions for mediainfo plugin

1. Copy the entire `mediainfo` directory to `~/.config/doublecmd/plugins/wdx/` (or `/usr/lib/doublecmd/plugins/wdx/`, or anywhere else)

2. Install mediainfo CLI: https://mediaarea.net/

3. Locate the filename of your Lua library (should be similar to `liblua5.2.so.0`) inside `/usr`. If you don't have it, install it: https://www.lua.org/

4.
    A: Enter it in Double Commander: Configuration > Options > Plugins > Lua library file to use
    
    B: If you don't have that option (i.e. older version of Double Commander): Locate your `doublecmd.xml` (see https://doublecmd.github.io/doc/en/configxml.html#luapathtolibrary) and enter the Lua library at `<PathToLibrary>`; then restart Double Commander.

5. Activate the plugin inside Double Commander: Configuration > Options > Plugins WDX > Add > ~/.config/doublecmd/plugins/wdx/mediainfo/mediainfo-video-cli.lua or mediainfo-video-cli2.lua

6. Use it at Configuration > Options > Columns > Custom columns
