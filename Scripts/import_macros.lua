require "lfs"
require "os"

local macros_dir = "./Scripts/macros"

local function getpathes(rootpath, pathes)
    pathes = pathes or {}
    for entry in lfs.dir(rootpath) do
        -- print(entry)
        -- ~= !=
        if entry ~= '.' and entry ~= '..' then
            local path = rootpath .. '/' .. entry
            local attr = lfs.attributes(path)
            assert(type(attr) == 'table')

            -- if attr.mode == 'directory' then
            --     getpathes(path, pathes)
            -- else
            pathes[entry] = path
        end
    end
    return pathes
end

local function getPreWords(file)
    local ret = ""
    for i = 1, #file do
        -- print(i)
        -- print("sub," .. i .. "," .. i)
        -- sub(s ,i, i) get the string [i]
        local k = string.sub(file, i, i)
        -- print(k)

        -- print(k .. "--" .. file)

        if k == '.' then
            break
        end

        ret = ret .. k
    end
    return ret
end

local function main(argv)
    -- files = getpathes(argv[1])
    local files = getpathes(macros_dir)

    for n, f in pairs(files) do
        print(n .. ":" .. f)
        n = getPreWords(n)

        local cmd = "xmake m --import=" .. f .. " " .. n
        os.execute(cmd)
    end
end

main(arg)
