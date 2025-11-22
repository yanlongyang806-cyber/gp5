require("csv");
require("ss2000");

infn = "itemnamelist.txt"
local outfile = nil;
local errorfile = nil;
local accountidlistfile = nil;
local itemnamelist = {};
local mytype = 0;

function Begin()
	itemnamelistfile = io.open(infn);

	for line in itemnamelistfile:lines() do
		itemname = string.match(line, "(.+)");
		if itemname then
			itemnamelist[itemname] = 0;
		end
	end
end

function End()
--	generate a file with the counts and a name that reflects the type passed in if I can know it
--	mytype is the numerical value of the type
	local outFileName = mytype.."ItemCounts.csv";
	local outFile = csv.new(outFileName, "w");
	for k, v in next, itemnamelist do
		outFile:write(k,v);
	end
	outFile:close();
end

function Process()
	-- 	Get the type so we can customize file names at the end.
	if mytype == 0 then
		mytype = xvalue(".Myentitytype");
	end
	
	--	Don't count the buy back bag?
	--	Walk pInventoryV2 to check for items
	if xcount(".pInventoryV2.ppInventoryBags") > 0 then
		for i in xindices(".pInventoryV2.ppInventoryBags") do
			local inventoryslotspath = ".pInventoryV2.ppInventoryBags["..i.."].ppIndexedInventorySlots";
			if xcount(inventoryslotspath) > 0 then
				for j in xindices(inventoryslotspath) do
					local inventoryitempath = inventoryslotspath.."["..j.."].pItem.hItem";
					local inventorycountpath = inventoryslotspath.."["..j.."].pItem.Count";
					local itemname = xvalue(inventoryitempath);
					local itemcount = xvalue(inventorycountpath);
					if itemnamelist[itemname] then
						itemnamelist[itemname] = itemnamelist[itemname] + itemcount;
					end
				end
			end
		end
	end
		
	--	Walk pEmailV2 to check for items
	if xcount(".pPlayer.pEmailV2.mail") > 0 then
		for i in xindices(".pPlayer.pEmailV2.mail") do
			local inventoryslotspath = ".pPlayer.pEmailV2.mail["..i.."].ppItemSlot";
			if xcount(inventoryslotspath) > 0 then
				for j in xindices(inventoryslotspath) do
					local inventoryitempath = inventoryslotspath.."["..j.."].pItem.hItem";
					local inventorycountpath = inventoryslotspath.."["..j.."].pItem.Count";
					local itemname = xvalue(inventoryitempath);
					local itemcount = xvalue(inventorycountpath);
					if itemnamelist[itemname] then
						itemnamelist[itemname] = itemnamelist[itemname] + itemcount;
					end
				end
			end
		end
	end
	
	--	Walk pEmailV3 to check for items
	if xcount(".pEmailV3.eaMessages") > 0 then
		for i in xindices(".pEmailV3.eaMessages") do
			local inventoryslotspath = ".pEmailV3.eaMessages["..i.."].ppItems";
			if xcount(inventoryslotspath) > 0 then
				for j in xindices(inventoryslotspath) do
					local inventoryitempath = inventoryslotspath.."["..j.."].hItem";
					local inventorycountpath = inventoryslotspath.."["..j.."].Count";
					local itemname = xvalue(inventoryitempath);
					local itemcount = xvalue(inventorycountpath);
					if itemnamelist[itemname] then
						itemnamelist[itemname] = itemnamelist[itemname] + itemcount;
					end
				end
			end
		end
	end
	
	-- 	Walk pInventoryV1_Deprecated to check for items in case the Entity has not yet been fixed up
	
	if xcount(".pInventoryV1_Deprecated.ppInventoryBags") > 0 then
		for i in xindices(".pInventoryV1_Deprecated.ppInventoryBags") do
			local inventoryslotspath = ".pInventoryV1_Deprecated.ppInventoryBags["..i.."].ppIndexedInventorySlots";
			if xcount(inventoryslotspath) > 0 then
				for j in xindices(inventoryslotspath) do
					local inventoryitempath = inventoryslotspath.."["..j.."].pItem.hItem";
					local inventorycountpath = inventoryslotspath.."["..j.."].Count";
					local itemname = xvalue(inventoryitempath);
					local itemcount = xvalue(inventorycountpath);
					if itemnamelist[itemname] then
						itemnamelist[itemname] = itemnamelist[itemname] + itemcount;
					end
				end
			end
		end
	end
		
	--	Walk pEmailV1_Deprecated to check for items
	if xcount(".pPlayer.pEmailV1_Deprecated.mail") > 0 then
		for i in xindices(".pPlayer.pEmailV1_Deprecated.mail") do
			local inventoryslotspath = ".pPlayer.pEmailV1_Deprecated.mail["..i.."].ppItemSlot";
			if xcount(inventoryslotspath) > 0 then
				for j in xindices(inventoryslotspath) do
					local inventoryitempath = inventoryslotspath.."["..j.."].pItem.hItem";
					local inventorycountpath = inventoryslotspath.."["..j.."].Count";
					local itemname = xvalue(inventoryitempath);
					local itemcount = xvalue(inventorycountpath);
					if itemnamelist[itemname] then
						itemnamelist[itemname] = itemnamelist[itemname] + itemcount;
					end
				end
			end
		end
	end
	
end
