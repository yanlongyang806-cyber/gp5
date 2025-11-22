require("csv");

kv = nil;
fn = "kvs.csv";

local f = nil;

function Begin()
	f = csv.new(fn);
end

function Process()
	local a_name = xvalue(".AccountName");

	for k in xindices(".Ppkeyvaluepairs") do
		if not kv or k:match(kv) then
			local v = xvalue((".Ppkeyvaluepairs[%s].Value"):format(k));
			f:write(a_name, k, v);
		end
	end
end

function End()
	f:close();
end
