const { lua, lauxlib, lualib, to_luastring, to_jsstring } = require('fengari');
const path = require('node:path');
process.chdir(path.join(__dirname, '..'));
const L = lauxlib.luaL_newstate();
lualib.luaL_openlibs(L);
let result = lauxlib.luaL_loadfile(L, to_luastring('tests/run.lua'));
if (result === lua.LUA_OK) result = lua.lua_pcall(L, 0, lua.LUA_MULTRET, 0);
if (result !== lua.LUA_OK) {
  console.error(to_jsstring(lua.lua_tostring(L, -1)));
  process.exitCode = 1;
}
