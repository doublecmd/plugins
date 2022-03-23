# Installation instructions for mediainfo plugin

1. Copy the entire `mediainfo` directory to `/usr/lib/doublecmd/plugins/wdx/`
2. Install mediainfo CLI: https://mediaarea.net/
3. Locate the filename of your Lua library (should be similar to `liblua5.2.so.0`) inside `/usr`. If you don't have it, install it: https://www.lua.org/
4. Locate your `doublecmd.xml` (see https://doublecmd.github.io/doc/en/configxml.html#luapathtolibrary) and enter the Lua library at `<PathToLibrary>`
5. Restart Double Commander?
6. Activate the plugin inside Double Commander: Configuration > Options > Plugins WDX > Add > /usr/lib/doublecmd/plugins/wdx/mediainfo/mediainfo-video-cli.lua or mediainfo-video-cli2.lua
7. Use it at Configuration > Options > Columns > Custom columns
