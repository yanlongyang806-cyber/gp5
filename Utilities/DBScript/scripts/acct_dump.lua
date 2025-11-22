require("csv");

fn = "acct_dump.csv";
local f = nil;

function Begin()
	f = csv.new(fn);
end

function Process()
	local a_id = xvalue(".UID");
	local a_name = xvalue(".AccountName");
	local d_name = xvalue(".DisplayName");
	local email = xvalue(".Personalinfo.Email");
	local created = xvalue(".Ucreatedtime");
	local pw_change = xvalue(".Upasswordchangetime");
	local pw_name = xvalue(".Ppwaccountname");
	local shadow = xvalue(".Bpwautocreated");

	f:write(a_id, a_name, d_name, email, created, pw_change, pw_name, shadow);
end

function End()
	f:close();
end
