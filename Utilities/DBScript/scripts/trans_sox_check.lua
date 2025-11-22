require('csv');
require('ss2000');

local categories = {
	["AF"] = "Action Figure",
	["AP"] = "Adventure Pack",
	["AT"] = "Archetype",
	["BRG"] = "Bridge Pack",
	["BUN"] = "Bundle",
	["CP"] = "Costume Pack",
	["EM"] = "Emote Pack",
	["EP"] = "Emblem Pack",
	["FI"] = "Functional Item",
	["IT"] = "Item",
	["PO"] = "Power",
	["PR"] = "Promo",
	["PS"] = "Playable Species",
	["PT"] = "Pet",
	["S"] = "Ship",
	["SC"] = "Ship Costume",
	["SV"] = "Service",
	["TI"] = "Title",
	["TK"] = "Token",
	
	-- Neverwinter categories
	["BAG"] = "Bag",
	["BST"] = "Booster",
	["BUF"] = "Buff",
	["CBF"] = "Companion Buff",
	["DYE"] = "Dye",
	["ENC"] = "Enchanting",
	["IDN"] = "Identification",
	["INS"] = "Inscription",
	["MNT"] = "Mount",
	["RES"] = "Resurrection Scroll",
	["SKC"] = "Skillcraft",
	["SKN"] = "Skin",
};

local oddballs = {
	["PRD-STO-M-FI-FedFerengi"] = "PS",
	["PRD-STO-M-FI-FedKlingon"] = "PS",
	["PRD-STO-M-CP-BUY-NXPrefix"] = "SC",
	["PRD-STO-M-FI-BUY-UssEnterprise"] = "S",
	["PRD-STO-M-SC-Buy-PlusOneRhodeIsland"] = "S",
	["PRD-STO-M-SC-PlusOneRhodeIsland"] = "S",
	["PRD-STO-M-SC-BUY-PlusOneVorcha"] = "S",
	["PRD-STO-M-SC-PlusOneVorcha"] = "S",
	["PRD-STO-M-SC-BUY-PlusOnegalaxy"] = "S",
	["PRD-STO-M-SC-PlusOneGalaxy"] = "S",
	["PRD-STO-M-IT-BUY-TosPack"] = "BUN",
	["PRD-STO-M-IT-TosPack"] = "BUN",
	["PRD-STO-M-F2P-Buy-1hr-XP"] = "IT",
	["PRD-STO-M-F2P-Buy-8hr-XP"] = "IT",
	["PRD-STO-M-S-BUY-EnterpriseBundle"] = "BUN",
	["PRD-STO-M-S-EnterpriseBundle"] = "BUN",
	["PRD-STO-M-SC-BUY-PlusOneAkira"] = "S",
	["PRD-STO-M-SC-BUY-PlusOneKtinga"] = "S",
	["PRD-STO-M-SC-BUY-PlusOnesaber"] = "S",
	["PRD-STO-M-SC-BUY-PlusOneConnie"] = "S",
	["PRD-STO-M-SC-BUY-PlusOneintrepid"] = "S",
};

local games = {
	["livefc"] = "CO",
	["holodeckst"] = "STO",
	["ptsfc"] = "CO",
	["tribblest"] = "STO",
	["mimicnw"] = "NW",
};

-- Add new currencies to both this table and the following table
-- In the "currencies" table, use a shorthand key that follows a consistent naming scheme
-- e.g. It should end in "_beta" if it's a beta currency
local currencies = {
	["cryptic"] = "crypticpoints",
	["atari"] = "ataritokens",
	["atari_beta"] = "betaataritokens",
	["co_beta"] = "champsf2pbetapoints",
	["co_promo"] = "championspoints",
	["sto_promo"] = "startrekpoints",
	["atari_promo"] = "promoataritokens",
	["cryptic_paid"] = "paidcrypticpoints",
	["atari_paid"] = "paidatarionlytokens",
	["atari_converted"] = "convertedataritokens",
	["sto_pts"] = "startrekptspoints",
	["sto_paid"] = "paidstartrekpoints",
	["co_paid"] = "paidchampionspoints",
	["zen_paid"] = "paidsharedzen",
	["zen_promo"] = "promosharedzen",
	["sto_zen_paid"] = "paidstartrekzen",
	["sto_zen_promo"] = "promostartrekzen",
	["co_zen_paid"] = "paidchampionszen",
	["co_zen_promo"] = "promochampionszen",
	["sto_zen_steam"] = "paidsteamstartrekzen",
	["co_zen_steam"] = "paidsteamchampionszen",
	["cryptic_promo"] = "promocrypticpoints",
	["nw_zen_paid"] = "paidneverwinterzen",
	["nw_zen_promo"] = "promoneverwinterzen",
	["nw_beta"] = "neverwinterbetapoints",
	["sto_escrow"] = "startrekforsaleescrow",
	["sto_escrow_steam"] = "paidsteamstartrekescrow",
	["sto_escrow_paid"] = "paidstartrekescrow",
	["sto_escrow_promo"] = "promostartrekescrow",
	["sto_claim"] = "startrekzenclaim",
	["sto_claim_steam"] = "paidsteamstartrekclaim",
	["sto_claim_paid"] = "paidstartrekclaim",
	["sto_claim_promo"] = "promostartrekclaim",
	["co_escrow"] = "champsforsaleescrow",
	["co_escrow_steam"] = "paidsteamchampionsescrow",
	["co_escrow_paid"] = "paidchampionsescrow",
	["co_escrow_promo"] = "promochampionsescrow",
	["co_claim"] = "championszenclaim",
	["co_claim_steam"] = "paidsteamchampionsclaim",
	["co_claim_paid"] = "paidchampionsclaim",
	["co_claim_promo"] = "promochampionsclaim",
};

local currency_codes = {
	[currencies.cryptic] = 1,
	[currencies.atari] = 3,
	[currencies.atari_beta] = 4,
	[currencies.co_beta] = 5,
	[currencies.co_promo] = 6,
	[currencies.sto_promo] = 7,
	[currencies.atari_promo] = 8,
	[currencies.cryptic_paid] = 9,
	[currencies.atari_paid] = 10,
	[currencies.atari_converted] = 11,
	[currencies.sto_pts] = 12,
	[currencies.sto_paid] = 13,
	[currencies.co_paid] = 14,
	[currencies.zen_paid] = 15,
	[currencies.zen_promo] = 16,
	[currencies.sto_zen_paid] = 17,
	[currencies.sto_zen_promo] = 18,
	[currencies.co_zen_paid] = 19,
	[currencies.co_zen_promo] = 20,
	[currencies.sto_zen_steam] = 21,
	[currencies.co_zen_steam] = 22,
	[currencies.cryptic_promo] = 23,
	[currencies.nw_zen_paid] = 24,
	[currencies.nw_zen_promo] = 25,
	[currencies.nw_beta] = 26,
	[currencies.sto_escrow] = 27,
	[currencies.sto_escrow_steam] = 28,
	[currencies.sto_escrow_paid] = 29,
	[currencies.sto_escrow_promo] = 30,
	[currencies.sto_claim] = 32,
	[currencies.sto_claim_steam] = 33,
	[currencies.sto_claim_paid] = 34,
	[currencies.sto_claim_promo] = 35,
	[currencies.co_escrow] = 36,
	[currencies.co_escrow_steam] = 37,
	[currencies.co_escrow_paid] = 38,
	[currencies.co_escrow_promo] = 39,
	[currencies.co_claim] = 41,
	[currencies.co_claim_steam] = 42,
	[currencies.co_claim_paid] = 43,
	[currencies.co_claim_promo] = 44,
};

local output_currencies = {
	[currency_codes[currencies.zen_paid]] = true,
	[currency_codes[currencies.sto_zen_paid]] = true,
	[currency_codes[currencies.sto_zen_steam]] = true,
	[currency_codes[currencies.co_zen_paid]] = true,
	[currency_codes[currencies.co_zen_steam]] = true,
	[currency_codes[currencies.nw_zen_paid]] = true,
};

local trans = {
	["credit"] = "CR",
	["debit"] = "DR",
};

local prods = {};

local f = nil;
local min_timestamp = 0;
local max_timestamp = 4294967296;
fn = fn or "trans_sox_breakdown.csv";
total_fn = total_fn or "trans_sox_total.csv";
start_date = start_date or nil;
end_date = end_date or nil;
hours = hours or nil;
local utc_offset = 8;

local totals = {};

function Begin()
	f = csv.new(fn);
	f:write("AccountID", "TransID", "UniqueTransID", "Provider", "MerchantTransID", "MerchantOrderID", "Action", "Currency", "Timestamp", "ProductName", "Game", "Category", "Type", "Reason", "Amount", "Balance");

	date = os.date('!*t');
	date.min = 0;
	date.sec = 0;
	date.isdst = false;

	if hours and not start_date and not end_date then
		date.hour = date.hour - hours;
	else
		date.hour = utc_offset;
		date.day = date.day - 1;
	end

	if start_date then
		min_timestamp = ToSS2000FromTimestamp(start_date);
		min_timestamp = min_timestamp + (3600 * utc_offset);
	else
		min_timestamp = ToSS2000(date);
	end

	if end_date then
		max_timestamp = ToSS2000FromTimestamp(end_date);
		max_timestamp = max_timestamp + (3600 * utc_offset);
	end

	print("Capturing logs with timestamps between "..min_timestamp.." and "..max_timestamp..".");
end

local a_id = nil;

function GetProductName(p_id)
	if not p_id then
		return "";
	end

	if not prods[p_id] then
		local con = loadcon("Accountserver_Product", p_id);
		prods[p_id] = con:xvalue(".Pname");
		con:unload();
	end

	return prods[p_id] or "";
end

function GetGameAndCategory(name, source)
	local not_app = "N/A";
	local uncat = "Uncategorized";
	local game, cat = nil, nil;

	if name ~= "" then
		local cat_code = oddballs[kv_prod] or name:match("^[^%-]*%-[^%-]*%-[^%-]*%-([^%-]*)");
		if cat_code then
			cat = categories[cat_code] or uncat;
		else
			cat = not_app;
		end

		game = name:match("^PRD%-([^%-]*)%-") or not_app;
	else
		cat = not_app;
		game = games[source:lower()] or not_app;
	end

	return game, cat;
end

function WriteTransaction(a_id, id, t_id, t_prov, mt_id, mo_id, action, c_code, stamp, p_name, p_game, p_cat, t_type, reason, diff, new)
	if output_currencies[c_code] then
		if action == trans.debit and t_type == "Micropurchase" then
			f:write(a_id, id, t_id, t_prov, mt_id, mo_id, action, c_code, stamp, p_name, p_game, p_cat, t_type, reason, diff, new);
			totals[p_name] = (totals[p_name] or 0) + diff;
		end
	end
end

function ProcessTransaction(id, path)
	local stamp = tonumber(xvalue(path..".Utimestampss2000"));

	if stamp < min_timestamp or stamp >= max_timestamp then
		return;
	end

	local t_id = nil;
	local source = nil;
	local reason = nil;
	local mt_id = nil;
	local mo_id = nil;
	local p_id = nil;
	local p_name = nil;
	local p_game = nil;
	local p_cat = nil;
	local t_prov = nil;
	local t_type = nil;

	local changes = {};

	for j in xindices(path..".Eakeyvaluechanges") do
		if not t_id then
			t_id = xvalue(path..".Ptransactionid");
			reason = xvalue(path..".Pkeyvaluechangereason");
			mt_id = xvalue(path..".Pmerchanttransactionid");
			mo_id = xvalue(path..".Pmerchantorderid");
			t_prov = xvalue(path..".Eprovider");
			t_type = xvalue(path..".Etransactiontype");
			source = xvalue(path..".Psource");
		end

		local change_path = path..(".Eakeyvaluechanges[%d]"):format(j);
		local key = xvalue(change_path..".Pkey");
		local old = tonumber(xvalue(change_path..".Ioldvalue"));
		local new = tonumber(xvalue(change_path..".Inewvalue"));
		local action = nil;
		local diff = 0;

		if old < new then
			action = trans.credit;
			diff = new - old;
		else
			action = trans.debit;
			diff = old - new;
		end

		table.insert(changes, {key = key, action = action, diff = diff, old = old, new = new});
	end

	if not t_id then
		return;
	end

	if t_type ~= "Micropurchase" then
		p_id = tonumber(xvalue(path..".Eapurchaseditems[0].Uproductid"));
		p_name = GetProductName(p_id);
		p_game, p_cat = GetGameAndCategory(p_name, source);

		for _, p in ipairs(changes) do
			local c_code = currency_codes[p.key:lower()];
			WriteTransaction(a_id, id, t_id, t_prov, mt_id, mo_id, p.action, c_code, TimestampFromSS2000(stamp), p_name, p_game, p_cat, t_type, reason, p.diff, p.new);
		end
	else
		for j in xindices(path..".Eapurchaseditems") do
			p_path = path..(".Eapurchaseditems[%d]"):format(j);
			p_id = tonumber(xvalue(p_path..".Uproductid"));
			p_price = tonumber(xvalue(p_path..".Pprice._Internal_Subdividedamount"));
			p_currency = xvalue(p_path..".Pprice._Internal_Currency");
			local spend = {};

			if p_currency:match("^_") then
				for _, p in ipairs(changes) do
					if p.action == "DR" then
						local diff = math.min(p_price, p.diff);
						p.diff = p.diff - diff;
						p_price = p_price - diff;
						p.old = p.old - diff;
						table.insert(spend, {key = p.key, action = p.action, diff = diff, new = p.old});
					end
				end

				if p_price ~= 0 then
					print(("Account %d / transaction %d (%s) doesn't have enough expenditure to cover cost!"):format(a_id, id, t_id));
				end
			end

			p_name = GetProductName(p_id);
			p_game, p_cat = GetGameAndCategory(p_name, source);

			for _, p in ipairs(spend) do
				local c_code = currency_codes[p.key:lower()];
				WriteTransaction(a_id, id, t_id, t_prov, mt_id, mo_id, p.action, c_code, TimestampFromSS2000(stamp), p_name, p_game, p_cat, t_type, reason, p.diff, p.new);
			end
		end

		if xcount(path..".Eapurchaseditems") > 0 then
			for _, p in ipairs(changes) do
				if p.action == "DR" and p.diff ~= 0 then
					print(("Account %d / transaction %d (%s) has more expenditure than it should!"):format(a_id, id, t_id));
				end
			end
		end
	end
end

function Process()
	if modtime2000() < min_timestamp then
		return;
	end

	a_id = xvalue(".UID");

	for i in xindices(".Eatransactions") do
		ProcessTransaction(i, (".Eatransactions[%d]"):format(i));
	end

	for i in xindices(".Eapendingtransactions") do
		ProcessTransaction(i, (".Eapendingtransactions[%d]"):format(i));
	end
end

function End()
	f:close();

	f = csv.new(total_fn);
	f:write("Product", "Revenue");

	for k, v in pairs(totals) do
		f:write(k, v);
	end
end
