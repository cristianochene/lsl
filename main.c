#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <time.h>
#include <fnmatch.h>
#include <unistd.h>
#include <regex.h>

// Lua Headers
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

typedef struct {
    char name[256];
    char rel_path[4096];
    char extension[32];
    int is_dir;
    int is_symlink;
    int is_executable;
    long size;
    time_t modified_time;
    char permissions[10];
    int depth;
} FileInfo;

// Global sorting state variables
int g_dirs_first = 0;
char g_sort_by[32] = "name";
char g_sort_order[32] = "asc";

void get_permissions(mode_t mode, char *str) {
    str[0] = (mode & S_IRUSR) ? 'r' : '-';
    str[1] = (mode & S_IWUSR) ? 'w' : '-';
    str[2] = (mode & S_IXUSR) ? 'x' : '-';
    str[3] = (mode & S_IRGRP) ? 'r' : '-';
    str[4] = (mode & S_IWGRP) ? 'w' : '-';
    str[5] = (mode & S_IXGRP) ? 'x' : '-';
    str[6] = (mode & S_IROTH) ? 'r' : '-';
    str[7] = (mode & S_IWOTH) ? 'w' : '-';
    str[8] = (mode & S_IXOTH) ? 'x' : '-';
    str[9] = '\0';
}

void extract_extension(const char *filename, char *ext, size_t max_len) {
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) {
        ext[0] = '\0';
    } else {
        snprintf(ext, max_len, "%s", dot + 1);
    }
}

int compare_files(const void *a, const void *b) {
    const FileInfo *fileA = (const FileInfo *)a;
    const FileInfo *fileB = (const FileInfo *)b;

    if (g_dirs_first) {
        if (fileA->is_dir && !fileB->is_dir) return -1;
        if (!fileA->is_dir && fileB->is_dir) return 1;
    }

    int result = 0;
    if (strcmp(g_sort_by, "size") == 0) {
        if (fileA->size < fileB->size) result = -1;
        else if (fileA->size > fileB->size) result = 1;
        else result = strcasecmp(fileA->name, fileB->name);
    } else if (strcmp(g_sort_by, "time") == 0) {
        if (fileA->modified_time < fileB->modified_time) result = -1;
        else if (fileA->modified_time > fileB->modified_time) result = 1;
        else result = strcasecmp(fileA->name, fileB->name);
    } else if (strcmp(g_sort_by, "type") == 0) {
        result = strcasecmp(fileA->extension, fileB->extension);
        if (result == 0) result = strcasecmp(fileA->name, fileB->name);
    } else {
        result = strcasecmp(fileA->rel_path, fileB->rel_path);
    }

    if (strcmp(g_sort_order, "desc") == 0) {
        result = -result;
    }

    return result;
}

int should_ignore(lua_State *L, const char *filename) {
    lua_getglobal(L, "ignore_patterns");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }

    int ignore = 0;
    size_t len = lua_rawlen(L, -1);
    for (size_t i = 1; i <= len; i++) {
        lua_rawgeti(L, -1, i);
        if (lua_isstring(L, -1)) {
            const char *pattern = lua_tostring(L, -1);
            if (fnmatch(pattern, filename, 0) == 0) {
                ignore = 1;
                lua_pop(L, 1);
                break;
            }
        }
        lua_pop(L, 1);
    }

    lua_pop(L, 1);
    return ignore;
}

int load_config(lua_State *L) {
    if (access("config.lua", F_OK) == 0) {
        if (luaL_dofile(L, "config.lua") == LUA_OK) return 1;
    }

    const char *home = getenv("HOME");
    if (home != NULL) {
        char global_path[1024];
        snprintf(global_path, sizeof(global_path), "%s/.config/lsl/config.lua", home);
        if (access(global_path, F_OK) == 0) {
            if (luaL_dofile(L, global_path) == LUA_OK) return 1;
        }
    }

    return 0;
}

// Recursive file collection function
void collect_files(lua_State *L, const char *base_path, const char *sub_path, 
                   FileInfo **files, size_t *count, int depth, int max_depth, 
                   int recursive, int show_hidden, regex_t *regex_filter, int use_regex) {

    char current_dir_path[4096];
    if (sub_path[0] != '\0') {
        if (snprintf(current_dir_path, sizeof(current_dir_path), "%s/%s", base_path, sub_path) >= (int)sizeof(current_dir_path)) {
            fprintf(stderr, "Warning: Path length exceeded limit: %s/%s\n", base_path, sub_path);
            return;
        }
    } else {
        if (snprintf(current_dir_path, sizeof(current_dir_path), "%s", base_path) >= (int)sizeof(current_dir_path)) {
            fprintf(stderr, "Warning: Base path length exceeded limit: %s\n", base_path);
            return;
        }
    }

    DIR *dir = opendir(current_dir_path);
    if (dir == NULL) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (!show_hidden && entry->d_name[0] == '.') continue;
        if (should_ignore(L, entry->d_name)) continue;

        // Regular Expression filter
        if (use_regex) {
            if (regexec(regex_filter, entry->d_name, 0, NULL, 0) != 0) {
                continue;
            }
        }

        char full_item_path[4096];
        char rel_item_path[4096];
        
        int full_len = snprintf(full_item_path, sizeof(full_item_path), "%s/%s", current_dir_path, entry->d_name);
        if (full_len < 0 || full_len >= (int)sizeof(full_item_path)) {
            fprintf(stderr, "Warning: File path truncated, skipping: %s/%s\n", current_dir_path, entry->d_name);
            continue;
        }

        if (sub_path[0] != '\0') {
            int rel_len = snprintf(rel_item_path, sizeof(rel_item_path), "%s/%s", sub_path, entry->d_name);
            if (rel_len < 0 || rel_len >= (int)sizeof(rel_item_path)) {
                continue;
            }
        } else {
            snprintf(rel_item_path, sizeof(rel_item_path), "%s", entry->d_name);
        }

        struct stat file_stat;
        struct stat lstat_buf;
        int is_symlink = 0;

        if (lstat(full_item_path, &lstat_buf) == 0 && S_ISLNK(lstat_buf.st_mode)) {
            is_symlink = 1;
        }

        int stat_ok = (stat(full_item_path, &file_stat) == 0);
        int is_dir = stat_ok ? S_ISDIR(file_stat.st_mode) : 0;

        *files = realloc(*files, sizeof(FileInfo) * (*count + 1));
        
        snprintf((*files)[*count].name, sizeof((*files)[*count].name), "%s", entry->d_name);
        snprintf((*files)[*count].rel_path, sizeof((*files)[*count].rel_path), "%s", rel_item_path);
        extract_extension(entry->d_name, (*files)[*count].extension, sizeof((*files)[*count].extension));

        (*files)[*count].is_dir = is_dir;
        (*files)[*count].is_symlink = is_symlink;
        (*files)[*count].depth = depth;

        if (stat_ok) {
            (*files)[*count].size = file_stat.st_size;
            (*files)[*count].modified_time = file_stat.st_mtime;
            get_permissions(file_stat.st_mode, (*files)[*count].permissions);

            (*files)[*count].is_executable = !is_dir && ((file_stat.st_mode & S_IXUSR) ||
                                                          (file_stat.st_mode & S_IXGRP) ||
                                                          (file_stat.st_mode & S_IXOTH));
        } else {
            (*files)[*count].size = 0;
            (*files)[*count].modified_time = 0;
            (*files)[*count].is_executable = 0;
            strcpy((*files)[*count].permissions, "---------");
        }

        (*count)++;

        // Recurse into subdirectories if enabled and within max_depth limits
        if (is_dir && recursive && (max_depth == 0 || depth < max_depth)) {
            collect_files(L, base_path, rel_item_path, files, count, depth + 1, max_depth, recursive, show_hidden, regex_filter, use_regex);
        }
    }

    closedir(dir);
}

// Formats a file entry using the Lua format_entry function
void print_formatted_entry(lua_State *L, FileInfo *file, int recursive) {
    lua_getglobal(L, "format_entry");

    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        printf("%s", recursive ? file->rel_path : file->name);
        return;
    }

    lua_newtable(L);

    lua_pushstring(L, recursive ? file->rel_path : file->name);
    lua_setfield(L, -2, "name");

    lua_pushstring(L, file->extension);
    lua_setfield(L, -2, "extension");

    lua_pushboolean(L, file->is_dir);
    lua_setfield(L, -2, "is_dir");

    lua_pushboolean(L, file->is_symlink);
    lua_setfield(L, -2, "is_symlink");

    lua_pushboolean(L, file->is_executable);
    lua_setfield(L, -2, "is_executable");

    lua_pushinteger(L, file->size);
    lua_setfield(L, -2, "size");

    lua_pushinteger(L, file->modified_time);
    lua_setfield(L, -2, "modified_time");

    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        fprintf(stderr, "Error executing format_entry: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
    }

    const char *formatted_name = lua_tostring(L, -1);
    printf("%s", formatted_name);
    lua_pop(L, 1);
}

int main(int argc, char *argv[]) {
    lua_State *L = luaL_newstate();
    if (L == NULL) {
        fprintf(stderr, "Error initializing Lua.\n");
        return 1;
    }

    luaL_openlibs(L);

    if (!load_config(L)) {
        fprintf(stderr, "Warning: Running with fallback configuration.\n");
    }

    // Read default configuration from Lua
    lua_getglobal(L, "show_hidden");
    int show_hidden = lua_toboolean(L, -1);
    lua_pop(L, 1);

    lua_getglobal(L, "dirs_first");
    if (!lua_isnil(L, -1)) g_dirs_first = lua_toboolean(L, -1);
    lua_pop(L, 1);

    lua_getglobal(L, "recursive");
    int recursive = lua_toboolean(L, -1);
    lua_pop(L, 1);

    int max_depth = 1;
    lua_getglobal(L, "max_depth");
    if (lua_isnumber(L, -1)) max_depth = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);

    int columns = 4;
    lua_getglobal(L, "columns");
    if (lua_isnumber(L, -1)) columns = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);

    int column_width = 32;
    lua_getglobal(L, "column_width");
    if (lua_isnumber(L, -1)) column_width = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);

    // Auto-detect terminal width and calculate max fitting columns
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) {
        int auto_cols = w.ws_col / column_width;
        if (auto_cols > 0) {
            columns = auto_cols;
        }
    }

    char regex_pattern[256] = "";
    int use_regex = 0;

    const char *target_dir = ".";

    // Parse CLI options
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--all") == 0) {
            show_hidden = 1;
        } else if (strcmp(argv[i], "-R") == 0 || strcmp(argv[i], "--recursive") == 0) {
            recursive = 1;
        } else if (strcmp(argv[i], "--dirs-first") == 0) {
            g_dirs_first = 1;
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--regex") == 0) {
            if (i + 1 < argc) {
                snprintf(regex_pattern, sizeof(regex_pattern), "%s", argv[i + 1]);
                use_regex = 1;
                i++;
            }
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--max-depth") == 0) {
            if (i + 1 < argc) {
                max_depth = atoi(argv[i + 1]);
                i++;
            }
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--columns") == 0) {
            if (i + 1 < argc) {
                columns = atoi(argv[i + 1]);
                i++;
            }
        } else if (argv[i][0] != '-') {
            target_dir = argv[i];
        }
    }

    // Compile regex pattern if provided
    regex_t regex_filter;
    if (use_regex) {
        if (regcomp(&regex_filter, regex_pattern, REG_EXTENDED | REG_NOSUB) != 0) {
            fprintf(stderr, "Error: Invalid regular expression '%s'\n", regex_pattern);
            lua_close(L);
            return 1;
        }
    }

    FileInfo *files = NULL;
    size_t count = 0;

    // Collect directory entries
    collect_files(L, target_dir, "", &files, &count, 1, max_depth, recursive, show_hidden, &regex_filter, use_regex);

    if (use_regex) {
        regfree(&regex_filter);
    }

    // Sort entries
    qsort(files, count, sizeof(FileInfo), compare_files);

    // Switch layout mode if recursive traversal is enabled
    if (recursive) {
        lua_pushstring(L, "tree");
        lua_setglobal(L, "layout_mode");
    }

    // Render output
    if (count > 0) {
        if (recursive) {
            for (size_t i = 0; i < count; i++) {
                print_formatted_entry(L, &files[i], recursive);
                printf("\n");
            }
        } else {
            // Vertical grid rendering (top-to-bottom, left-to-right)
            int rows = (count + columns - 1) / columns;
            for (int r = 0; r < rows; r++) {
                for (int c = 0; c < columns; c++) {
                    int idx = c * rows + r;
                    if (idx < (int)count) {
                        print_formatted_entry(L, &files[idx], recursive);
                    }
                }
                printf("\n");
            }
        }
    }

    free(files);
    lua_close(L);

    return 0;
}
