csv = {}
csv.__index = csv

function csv.new(fn)
    local f = {};
    setmetatable(f, csv);
    f.h = io.open(fn, "w");
    if not f.h then
    	print("\nYou are most likely about to get a runtime error. It is because you probably have "..fn.." open in Excel.");
		return nil, "Failed to open "..fn;
    end
	return f;
end

function csv.write(f, ...)
	local arg = {...};
	local c = #arg;

	if f.b then
		f.h:write("\n");
	else
		f.b = true;
	end

	for i, v in ipairs(arg) do
		if type(v) == "string" and v:match("[\",]") then
			v = v:gsub("\"", "\"\"");
			v = "\""..v.."\"";
		end

		f.h:write(tostring(v));

		if i < c then
			f.h:write(",");
		end
	end
end

function csv.close(f)
	f.h:close();
end
