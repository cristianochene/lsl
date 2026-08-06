-- config.lua (lsl 2.0 - Fixed Alignment & No Wrapping)

-- Default Behavior
show_hidden = false
dirs_first = true -- Strict alphabetical order
columns = 4
column_width = 32 -- Fixed column width
show_size = false
human_readable_size = true
sort_by = "name"
sort_order = "asc"

-- Visuals & Colors
enable_colors = true
enable_icons = false -- Disabled for clean text output
dir_icon = ""
file_icon = ""
binary_icon = ""
symlink_icon = ""

-- Standard ANSI Color Palette
ext_colors = {
	dir = "\27[1;34m", -- Bold Blue
	binary = "\27[1;32m", -- Bold Green
	symlink = "\27[36m", -- Cyan
	lua = "\27[33m", -- Yellow
	c = "\27[35m", -- Magenta
	h = "\27[35m", -- Magenta
	pdf = "\27[31m", -- Red
	png = "\27[32m", -- Green
	jpg = "\27[32m", -- Green
	jpeg = "\27[32m", -- Green
}

-- Ignored Patterns
ignore_patterns = { "*.o", "*.tmp", ".git" }

-- Formatting Settings
layout_mode = "grid" -- "grid", "list", "tree"
trailing_slash = true
show_permissions = false
show_modified_time = false
date_format = "%b %d %H:%M"

local RESET = "\27[0m"
local GRAY = "\27[90m"

function format_size(bytes)
	if not human_readable_size then
		return (bytes or 0) .. " B"
	end
	local size = bytes or 0
	if size < 1024 then
		return size .. " B"
	elseif size < 1024 * 1024 then
		return string.format("%.1f KB", size / 1024)
	elseif size < 1024 * 1024 * 1024 then
		return string.format("%.1f MB", size / (1024 * 1024))
	else
		return string.format("%.1f GB", size / (1024 * 1024 * 1024))
	end
end

function get_color(file)
	if not enable_colors then
		return "", ""
	end
	if file.is_symlink and ext_colors.symlink then
		return ext_colors.symlink, RESET
	elseif file.is_dir and ext_colors.dir then
		return ext_colors.dir, RESET
	elseif file.is_executable and ext_colors.binary then
		return ext_colors.binary, RESET
	elseif file.extension and ext_colors[file.extension] then
		return ext_colors[file.extension], RESET
	end
	return "", ""
end

function format_entry(file)
	local color_start, color_end = get_color(file)

	local display_name = file.name
	if file.is_dir and trailing_slash then
		display_name = display_name .. "/"
	end

	local raw_size = ""
	local colored_size = ""

	if show_size and not file.is_dir then
		local formatted = format_size(file.size)
		raw_size = " (" .. formatted .. ")"
		colored_size = " " .. GRAY .. "(" .. formatted .. ")" .. RESET
	end

	-- Truncate filename if total entry length exceeds column_width - 2 spaces
	local max_name_len = column_width - string.len(raw_size) - 2
	if max_name_len > 5 and string.len(display_name) > max_name_len then
		display_name = string.sub(display_name, 1, max_name_len - 3) .. "..."
	end

	-- Exact visible length calculation
	local visible_len = string.len(display_name) + string.len(raw_size)
	local pad_len = column_width - visible_len
	if pad_len < 1 then
		pad_len = 1
	end
	local padding = string.rep(" ", pad_len)

	return color_start .. display_name .. color_end .. colored_size .. padding
end
