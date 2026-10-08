-- script analyze: the models of every file, and the script's arguments
local first, second = ...
print("files", #models, models[1].name, models[2].name)
print("args", first, second, arg[0] ~= nil)
assert(model == models[1])

local send = model:get(495)
print("send", send:where(), (send:value("Message")))
for _, m in ipairs(models) do
    local scripts = 0
    for _, root in ipairs(m.roots) do
        if root.kind == "Script" then
            scripts = scripts + 1
        end
    end
    print("scripts", m.name, scripts > 0)
end
