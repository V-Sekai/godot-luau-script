local Module = require("test_scripts.reload.Module")
local Base = gdclass(nil, "Node")

--@1

Base:RegisterProperty("baseProperty", Enum.VariantType.STRING)
    :Default(Module.kBasePropertyDefault)

return Base