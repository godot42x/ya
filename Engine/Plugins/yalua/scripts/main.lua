Module = {}

function printtable(t)
    for k, v in pairs(t) do
        print(k)
        if (type(v) == "table") then
            printtable(v)
        else
            print(v)
        end
    end
end

local GuideModule = {}


function GuideModule:print_hello()
    print("Module.Guide.print_hello")
end

Module.Guide = GuideModule

function print_hello()
    print("global hello")
end

printtable(Module)
