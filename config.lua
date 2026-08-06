-- lsl default presentation layer. Loaded only with --lua or --config.
local reset = "\27[0m"
local colors = { dir="\27[1;34m", link="\27[36m", executable="\27[1;32m", lua="\27[33m", c="\27[35m", h="\27[35m", md="\27[36m" }
local icons = { dir="", link="", executable="", lua="", c="", h="", md="", default="" }
local ignored = { "*.o", "*.tmp" }

function filter_entry(file)
  for _, pattern in ipairs(ignored) do
    if file.name:match("^" .. pattern:gsub("%%", "%%%%"):gsub("%.", "%%."):gsub("%*", ".*") .. "$") then return false end
  end
  return file.name ~= ".git"
end

function format_entry(file)
  local kind = file.is_dir and "dir" or file.is_symlink and "link" or file.is_executable and "executable" or file.extension
  local icon = icons[kind] or icons.default
  local color = colors[kind] or ""
  local suffix = file.is_dir and "/" or ""
  return string.format("%s%s %s%s%s", color, icon, file.name, suffix, color ~= "" and reset or "")
end
