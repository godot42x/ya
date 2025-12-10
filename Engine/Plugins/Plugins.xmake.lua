local function include_xmake(path)
    includes(path .. "/xmake.lua")
end

include_xmake("./log.cc")
include_xmake("./utility.cc")
include_xmake("./reflect.cc")
include_xmake("./yalua")
include_xmake("./reflects")
-- include_xmake("./test.cc")
