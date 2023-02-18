local TestBaseScript = gdclass(nil, Node3D)

function TestBaseScript:TestMethod()
	print("TestBaseScript: TestMethod")
end

TestBaseScript:RegisterMethod("TestMethod")

TestBaseScript:RegisterProperty("baseProperty", Enum.VariantType.STRING)
	:Default("hi")

return TestBaseScript
