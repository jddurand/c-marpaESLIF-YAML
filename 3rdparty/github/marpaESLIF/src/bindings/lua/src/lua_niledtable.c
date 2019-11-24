static const char *MARPAESLIFLUA_NILEDTABLE =
  "\n"
  "-- NiledTable.lua\n"
  "local M = {}\n"
  "\n"
  "-- weak table for representing proxied storage tables.\n"
  "local data = setmetatable({}, {__mode = 'k'})\n"
  "\n"
  "-- nil placeholder.\n"
  "-- Note: this value is not exposed outside this module, so\n"
  "-- there's typically no possibility that a user could attempt\n"
  "-- to store a 'nil placeholder' in a table, leading to the\n"
  "-- same problem as storing nils in tables.\n"
  "local NIL = {\n"
  "   __tostring = function() return tostring(nil) end,\n"
  "   __type = function() return type(nil) end\n"
  "}\n"
  "setmetatable(NIL, NIL)\n"
  "\n"
  "-- metatable for NiledTable's.\n"
  "local mt = {}\n"
  "function mt.__index(t,k)\n"
  "  -- print('__index('..tostring(t)..', '..tostring(k)..')')\n"
  "  local d = data[t]\n"
  "  local v = d and d[k]\n"
  "  if v == NIL then v = nil end\n"
  "  return v\n"
  "end\n"
  "function mt.__newindex(t,k,v)\n"
  "  if v == nil then v = NIL end\n"
  "  -- print('__newindex('..tostring(t)..', '..tostring(k)..', '..tostring(v)..')')\n"
  "  local d = data[t]\n"
  "  if not d then\n"
  "    d = {}\n"
  "    data[t] = d\n"
  "  end\n"
  "  d[k] = v\n"
  "end\n"
  "function mt.__len(t)  -- note: ignored by Lua but used by exlen below\n"
  "  -- print('__len('..tostring(t)..')')\n"
  "  local d = data[t]\n"
  "  return d and #d or 0\n"
  "end\n"
  "\n"
  "-- constructor\n"
  "setmetatable(M, {__call = function(class, t)\n"
  "  return setmetatable(t, mt)\n"
  "end})\n"
  "\n"
  "function M.exists(t, k)\n"
  "  -- print('exists('..tostring(t)..', '..tostring(k)..')')\n"
  "  local d = data[t]\n"
  "  return (d and d[k]) ~= nil\n"
  "end\n"
  "local exists = M.exists\n"
  "\n"
  "function M.exlen(t)\n"
  "  -- print('exlen('..tostring(t)..')')\n"
  "  local mt = getmetatable(t)\n"
  "  local len = mt.__len\n"
  "  return len and len(t) or #t\n"
  "end\n"
  "\n"
  "local function exipairs_iter(t, i)\n"
  "  i = i + 1\n"
  "  if exists(t, i) then\n"
  "    local v = t[i]\n"
  "    return i, v\n"
  "  end\n"
  "end\n"
  "\n"
  "-- ipairs replacement that handles nil values in tables.\n"
  "function M.exipairs(t, i)\n"
  "  -- print('exipairs('..tostring(t)..', '..tostring(i)..')')\n"
  "  return exipairs_iter, t, 0\n"
  "end\n"
  "\n"
  "-- next replacement that handles nil values in tables\n"
  "function M.exnext(t, k)\n"
  "  -- print('exnext('..tostring(t)..', '..tostring(k)..')')\n"
  "  local d = data[t]\n"
  "  if not d then return end\n"
  "  k, v = next(d, k)\n"
  "  -- print('... => '..tostring(k)..', '..tostring(v))\n"
  "  return k, v\n"
  "end\n"
  "local exnext = M.exnext\n"
  "\n"
  "-- pairs replacement that handles nil values in tables.\n"
  "function M.expairs(t, i)\n"
  "  -- print('expairs('..tostring(t)..', '..tostring(i)..')')\n"
  "  return exnext, t, nil\n"
  "end\n"
  "\n"
  "-- Remove key in table.  This is used since there is no\n"
  "-- value v such that t[k] = v will remove k from the table.\n"
  "function M.delete(t, k)\n"
  "  -- print('delete('..tostring(t)..', '..tostring(k)..')')\n"
  "  local d = data[t]\n"
  "  if d then d[k] = nil end\n"
  "end\n"
  "\n"
  "-- array constructor replacement.  used since {...} discards nils.\n"
  "function M.niledarray(...)\n"
  "  -- print('niledarray(...)')\n"
  "  local n = select('#', ...)\n"
  "  local d = {...}\n"
  "  local _mt = { __index    = mt.__index,\n"
  "                __newindex = mt.__newindex, \n"
  "                __len      = mt.__len, \n"
  "                __pairs    = mt.__pairs, \n"
  "                __ipairs   = mt.__ipairs, \n"
  "                __next     = mt.__next, \n"
  "                __exists   = mt.__exists, \n"
  "                __delete   = mt.__delete,\n"
  "                canarray   = true }\n"
  "  local t = setmetatable({}, _mt)\n"
  "  for i=1,n do\n"
  "    if d[i] == nil then d[i] = NIL end\n"
  "  end\n"
  "  data[t] = d\n"
  "  return t\n"
  "end\n"
  "\n"
  "-- table constructor replacement.  used since {...} discards nils.\n"
  "function M.niledtablekv(...)\n"
  "  -- print('niledtablekv(...)')\n"
  "  -- possibly more optimally implemented in C.\n"
  "  local n = select('#', ...)\n"
  "  local tmp = {...} -- it would be nice to avoid this\n"
  "  local _mt = { __index    = mt.__index,\n"
  "                __newindex = mt.__newindex, \n"
  "                __len      = mt.__len, \n"
  "                __pairs    = mt.__pairs, \n"
  "                __ipairs   = mt.__ipairs, \n"
  "                __next     = mt.__next, \n"
  "                __exists   = mt.__exists, \n"
  "                __delete   = mt.__delete,\n"
  "                canarray   = false }\n"
  "  local t = setmetatable({}, _mt)\n"
  "  for i=1,n,2 do t[tmp[i]] = tmp[i+1] end\n"
  "  return t\n"
  "end\n"
  "\n"
  "mt.__pairs  = M.expairs\n"
  "mt.__ipairs = M.exipairs\n"
  "mt.__next   = M.exnext\n"
  "mt.__exists = M.exists\n"
  "mt.__delete = M.delete\n"
  "\n"
  "return M\n"
  "\n";