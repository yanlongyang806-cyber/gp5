require("os");

_G["__SS2000date"] = _G["__SS2000date"] or {
	month = 1,
	day = 1,
	year = 2000,
	hour = 0,
	min = 0,
	sec = 0,
};

_G["__SS2000time"] = _G["__SS2000time"] or os.time(_G["__SS2000date"]);

function copy(t)
	local t_copy = {};
	for k, v in pairs(t) do t_copy[k] = v; end
	return t_copy;
end

function ToSS2000(date_t)
	return (os.time(date_t) or _G["__SS2000time"]) - _G["__SS2000time"];
end

function FromSS2000(timess2000)
	return os.date("!*t", timess2000 + _G["__SS2000time"]);
end

function TimestampFromSS2000(timess2000)
	return os.date("%c", timess2000 + _G["__SS2000time"]);
end

function SQLTimestampFromSS2000(timess2000, override)
	if timess2000 == 0 and not override then
		return "";
	end

	return os.date("%Y-%m-%d %H:%M:%S", timess2000 + _G["__SS2000time"]);
end

function DatestampFromSS2000(timess2000)
	return os.date("%m/%d/%Y", timess2000 + _G["__SS2000time"]);
end

function ToSS2000FromDatestamp(date)
	local date_t = {};
	date_t.month, date_t.day, date_t.year = date:match("(%d+)[-/](%d+)[-/](%d+)");
	date_t.hour, date_t.min, date_t.sec = 0, 0, 0;
	return ToSS2000(date_t);
end

function ToSS2000FromTimestamp(time)
	local date_t = {};
	date_t.month, date_t.day, date_t.year, date_t.hour, date_t.min, date_t.sec = time:match("(%d+)[-/](%d+)[-/](%d+) (%d+):(%d+):(%d+)");
	if tonumber(date_t.year) < 2000 then date_t.year = date_t.year + 2000; end
	return ToSS2000(date_t);
end

function ToSS2000FromLogTimestamp(time)
	local date_t = {};
	date_t.year, date_t.month, date_t.day, date_t.hour, date_t.min, date_t.sec = time:match("(%d%d)(%d%d)(%d%d) (%d+):(%d+):(%d+)");
	if tonumber(date_t.year) < 2000 then date_t.year = date_t.year + 2000; end
	return ToSS2000(date_t);
end

function ToSS2000FromSQLTimestamp(time)
	local date_t = {};
	date_t.year, date_t.month, date_t.day, date_t.hour, date_t.min, date_t.sec = time:match("(%d+)-(%d+)-(%d+) (%d+):(%d+):(%d+)");
	return ToSS2000(date_t);
end
