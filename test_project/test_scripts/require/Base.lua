local Module = require("test_scripts.require.Module")
--@1

local Base = gdclass(nil, "Node")

function Base.TestFunc()
    return "what's up"
end

Base:RegisterProperty("baseProperty", Enum.VariantType.STRING)
    :Default(Module.kBasePropertyDefault)

return Base