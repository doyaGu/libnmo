-- script analyze: a failing analysis names the line
local count = #models

error("analysis failure over " .. count .. " file(s)")
