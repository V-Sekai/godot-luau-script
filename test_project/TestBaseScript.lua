local TestBaseScript = gdclass({ extends = "Node3D" })

function TestBaseScript:TestMethod()
	print("TestBaseScript: TestMethod")
end

TestBaseScript:RegisterMethod("TestMethod", {})

TestBaseScript:RegisterProperty("baseProperty", {
	property = gdproperty({ type = Enum.VariantType.STRING }),
	usage = Enum.PropertyUsageFlags.STORAGE,
	default = "hi"
})

return TestBaseScript
