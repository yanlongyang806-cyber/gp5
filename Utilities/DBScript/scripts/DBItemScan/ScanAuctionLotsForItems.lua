require("csv");
require("ss2000");

infn = "itemnamelist.txt"
local outfile = nil;
local errorfile = nil;
local accountidlistfile = nil;
local itemnamelist = {};
local mytype = "AuctionLot";

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
	--	Walk ppItemsV2 to check for items
	if xcount(".ppItemsV2") > 0 then
		for i in xindices(".ppItemsV2") do
			local inventoryitempath = ".ppItemsV2["..i.."].pItem.hItem";
			local inventorycountpath = ".ppItemsV2["..i.."].pItem.Count";
			local itemname = xvalue(inventoryitempath);
			local itemcount = xvalue(inventorycountpath);
			if itemnamelist[itemname] then
				itemnamelist[itemname] = itemnamelist[itemname] + itemcount;
			end
		end
	end
	
	--	Walk ppItemsV2 to check for items
	if xcount(".ppItemsV1_Deprecated") then
		for i in xindices(".ppItemsV1_Deprecated") do
			local inventoryitempath = ".ppItemsV1_Deprecated["..i.."].pItem.hItem";
			local inventorycountpath = ".ppItemsV1_Deprecated["..i.."].Count";
			local itemname = xvalue(inventoryitempath);
			local itemcount = xvalue(inventorycountpath);
			if itemnamelist[itemname] then
				itemnamelist[itemname] = itemnamelist[itemname] + itemcount;
			end
		end
	end
end
