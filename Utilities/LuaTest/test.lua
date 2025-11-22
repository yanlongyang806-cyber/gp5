
function showTest()
	obj1 = get_obj1();
	obj2 = get_obj2();

	print("Object 1 Name: " .. obj1.name .. "\n");
	print("Object 2 Name: " .. obj2.name .. "\n");

	global_print(string.format("Printed From a Global Func: %d %d %d\n", 1, 2, 3));
	test.test_print("Printed From a Module\n");
end

