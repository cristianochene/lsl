#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <inttypes.h>
#include <linux/limits.h>
#include <stdint.h>
#include <stdarg.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#ifdef LSL_WITH_LUA
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#if LUA_VERSION_NUM < 502
#define lua_rawlen lua_objlen
#endif
#endif

#define DENTS_BUFFER (256U * 1024U)
#define OUTPUT_BUFFER (256U * 1024U)
#define INITIAL_ENTRIES 1024U

struct linux_dirent64 { ino64_t d_ino; off64_t d_off; unsigned short d_reclen; unsigned char d_type; char d_name[]; };

typedef struct {
    const char *name;
    const char *path;
    uint64_t size;
    int64_t mtime;
    mode_t mode;
    unsigned char type;
    unsigned depth;
    unsigned stat_loaded : 1;
} Entry;

typedef struct Block { struct Block *next; size_t used, capacity; char data[]; } Block;
typedef struct { Block *head; } Arena;
typedef struct { Entry *data; size_t length, capacity; } Entries;
typedef struct { char *path; unsigned depth; } Work;
typedef struct { Work *data; size_t length, capacity; } Stack;
typedef struct { uint64_t files, dirs, links, bytes; } Totals;
typedef struct {
    int all, long_mode, tree, stats, dirs_first, reverse, no_sort;
    int use_lua; const char *config; const char *path;
} Options;

static Arena names;
static int sort_dirs_first, sort_reverse;

static void die(const char *what) { perror(what); exit(EXIT_FAILURE); }
static void *xrealloc(void *p, size_t n) { void *q = realloc(p, n); if (!q) die("realloc"); return q; }
static void *arena_alloc(Arena *a, size_t n) {
    n = (n + 7U) & ~7U;
    if (!a->head || a->head->capacity - a->head->used < n) {
        size_t cap = n > 65536U ? n : 65536U;
        Block *b = malloc(sizeof(*b) + cap); if (!b) die("malloc");
        b->next = a->head; b->used = 0; b->capacity = cap; a->head = b;
    }
    void *p = a->head->data + a->head->used; a->head->used += n; return p;
}
static char *arena_copy(Arena *a, const char *s) { size_t n = strlen(s)+1; char *p=arena_alloc(a,n); memcpy(p,s,n); return p; }
static void arena_destroy(Arena *a) { while (a->head) { Block *n=a->head->next; free(a->head); a->head=n; } }
static void entries_push(Entries *v, Entry e) { if(v->length==v->capacity){v->capacity=v->capacity?v->capacity*2:INITIAL_ENTRIES;v->data=xrealloc(v->data,v->capacity*sizeof(*v->data));}v->data[v->length++]=e; }
static void stack_push(Stack *s, Work w) { if(s->length==s->capacity){s->capacity=s->capacity?s->capacity*2:64;s->data=xrealloc(s->data,s->capacity*sizeof(*s->data));}s->data[s->length++]=w; }
static int type_is_dir(unsigned char t) { return t == DT_DIR; }
static int type_is_link(unsigned char t) { return t == DT_LNK; }
static const char *extension(const char *name) { const char *p=strrchr(name,'.'); return (!p||p==name)?"":p+1; }
static const char *join_path(const char *base, const char *name) {
    size_t a=strlen(base), b=strlen(name); char *p=arena_alloc(&names,a+b+2); memcpy(p,base,a); if(a&&base[a-1]!='/')p[a++]='/'; memcpy(p+a,name,b+1); return p;
}
static int load_stat(Entry *e) {
    if (e->stat_loaded) return 0;
    struct stat st; if (fstatat(AT_FDCWD,e->path,&st,AT_SYMLINK_NOFOLLOW)<0) return -1;
    e->size=(uint64_t)st.st_size; e->mtime=(int64_t)st.st_mtime; e->mode=st.st_mode; e->stat_loaded=1;
    if(e->type==DT_UNKNOWN) e->type=S_ISDIR(st.st_mode)?DT_DIR:S_ISLNK(st.st_mode)?DT_LNK:DT_REG;
    return 0;
}
static int compare_entries(const void *ap,const void *bp){const Entry*a=ap,*b=bp;if(sort_dirs_first&&type_is_dir(a->type)!=type_is_dir(b->type))return type_is_dir(a->type)?-1:1;int r=strverscmp(a->name,b->name);return sort_reverse?-r:r;}

static int scan(const Options *o, Entries *out, Totals *totals) {
    Stack todo={0}; stack_push(&todo,(Work){arena_copy(&names,o->path),0}); char *buf=malloc(DENTS_BUFFER); if(!buf)die("malloc");
    while(todo.length){Work w=todo.data[--todo.length];int fd=open(w.path,O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(fd<0){fprintf(stderr,"lsl: %s: %s\n",w.path,strerror(errno));continue;}
        for(;;){long n=syscall(SYS_getdents64,fd,buf,DENTS_BUFFER);if(n<0){fprintf(stderr,"lsl: %s: %s\n",w.path,strerror(errno));break;}if(!n)break;
            for(long pos=0;pos<n;){struct linux_dirent64*d=(void*)(buf+pos);pos+=d->d_reclen;if(!strcmp(d->d_name,".")||!strcmp(d->d_name,"..")||(!o->all&&d->d_name[0]=='.'))continue;
                const char *path=join_path(w.path,d->d_name);Entry e={arena_copy(&names,d->d_name),path,0,0,0,d->d_type,w.depth,0};
                if((o->long_mode||o->stats||d->d_type==DT_UNKNOWN)&&load_stat(&e)<0)continue;
                if(type_is_dir(e.type))totals->dirs++;else if(type_is_link(e.type))totals->links++;else totals->files++;if(e.stat_loaded)totals->bytes+=e.size;
                entries_push(out,e);if(o->tree&&type_is_dir(e.type))stack_push(&todo,(Work){(char*)path,w.depth+1});
            }
        }close(fd);
    }free(buf);free(todo.data);return 0;
}

static void mode_string(mode_t m,char p[11]){const char chars[]="rwxrwxrwx";p[0]=S_ISDIR(m)?'d':S_ISLNK(m)?'l':'-';for(int i=0;i<9;i++)p[i+1]=(m&(1U<<(8-i)))?chars[i]:'-';p[10]=0;}
static const char *icon_for(const Entry *e){if(type_is_dir(e->type))return "";if(type_is_link(e->type))return "";const char*x=extension(e->name);if(!strcmp(x,"c")||!strcmp(x,"h"))return "";if(!strcmp(x,"lua"))return "";if(!strcmp(x,"md"))return "";return "";}
static const char *color_for(const Entry *e){if(type_is_dir(e->type))return "\033[1;34m";if(type_is_link(e->type))return "\033[36m";if(e->stat_loaded&&(e->mode&0111))return "\033[1;32m";return "";}
static void append(char **buf,size_t *used,size_t *cap,const char *s,size_t n){if(*used+n>*cap){while(*used+n>*cap)*cap*=2;*buf=xrealloc(*buf,*cap);}memcpy(*buf+*used,s,n);*used+=n;}
static void appendf(char **buf,size_t*u,size_t*c,const char *fmt,...){va_list ap;va_start(ap,fmt);char tmp[512];int n=vsnprintf(tmp,sizeof tmp,fmt,ap);va_end(ap);if(n>0)append(buf,u,c,tmp,(size_t)n<sizeof tmp?(size_t)n:sizeof(tmp)-1);}

#ifdef LSL_WITH_LUA
static Entry *lua_entry(lua_State *L){return *(Entry**)luaL_checkudata(L,1,"lsl.entry");}
static int entry_index(lua_State *L){Entry*e=lua_entry(L);const char*k=luaL_checkstring(L,2);if(!strcmp(k,"name"))lua_pushstring(L,e->name);else if(!strcmp(k,"path"))lua_pushstring(L,e->path);else if(!strcmp(k,"extension"))lua_pushstring(L,extension(e->name));else if(!strcmp(k,"is_dir"))lua_pushboolean(L,type_is_dir(e->type));else if(!strcmp(k,"is_symlink"))lua_pushboolean(L,type_is_link(e->type));else if(!strcmp(k,"depth"))lua_pushinteger(L,e->depth);else {if(load_stat(e)<0)lua_pushnil(L);else if(!strcmp(k,"size"))lua_pushinteger(L,(lua_Integer)e->size);else if(!strcmp(k,"mtime"))lua_pushinteger(L,e->mtime);else if(!strcmp(k,"mode"))lua_pushinteger(L,e->mode);else if(!strcmp(k,"is_executable"))lua_pushboolean(L,(e->mode&0111)!=0);else lua_pushnil(L);}return 1;}
static int push_entry(lua_State*L,Entry*e){Entry**u=lua_newuserdata(L,sizeof(*u));*u=e;luaL_getmetatable(L,"lsl.entry");lua_setmetatable(L,-2);return 1;}
static lua_State *start_lua(const char *path){lua_State*L=luaL_newstate();if(!L)return NULL;luaL_openlibs(L);luaL_newmetatable(L,"lsl.entry");lua_pushcfunction(L,entry_index);lua_setfield(L,-2,"__index");lua_pop(L,1);if(luaL_dofile(L,path)!=LUA_OK){fprintf(stderr,"lsl: %s\n",lua_tostring(L,-1));lua_close(L);return NULL;}return L;}
static int lua_accept(lua_State*L,Entry*e){lua_getglobal(L,"filter_entry");if(!lua_isfunction(L,-1)){lua_pop(L,1);return 1;}push_entry(L,e);if(lua_pcall(L,1,1,0)!=LUA_OK){fprintf(stderr,"lsl: filter_entry: %s\n",lua_tostring(L,-1));lua_pop(L,1);return 0;}int ok=lua_toboolean(L,-1);lua_pop(L,1);return ok;}
static const char *lua_format(lua_State *L, Entry *e, size_t *length) {
    lua_getglobal(L, "format_entry");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return NULL;
    }

    push_entry(L, e);
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        fprintf(stderr, "lsl: format_entry: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return NULL;
    }

    const char *formatted = lua_tolstring(L, -1, length);
    if (!formatted) {
        fprintf(stderr, "lsl: format_entry must return a string\n");
        lua_pop(L, 1);
    }
    return formatted;
}
#endif

#ifdef LSL_WITH_LUA
static int default_config_path(char *path, size_t size) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    int length;

    if (xdg && xdg[0] == '/')
        length = snprintf(path, size, "%s/lsl/config.lua", xdg);
    else if (home && *home)
        length = snprintf(path, size, "%s/.config/lsl/config.lua", home);
    else {
        fprintf(stderr, "lsl: cannot locate config.lua: HOME and XDG_CONFIG_HOME are unset\n");
        return -1;
    }

    if (length < 0 || (size_t)length >= size) {
        fprintf(stderr, "lsl: config.lua path is too long\n");
        return -1;
    }
    return 0;
}
#endif

static void usage(FILE*f){fprintf(f,"Usage: lsl [OPTIONS] [DIR]\n  -a, --all       show hidden entries\n  -l, --long      load and print metadata\n  -h, --help      show this help and exit\n      --tree      iterative recursive tree\n      --stats     print directory totals\n      --lua       load config.lua from the user config directory\n      --config F  load Lua file F\n      --no-sort   preserve kernel order\n      --dirs-first, --reverse\n");}
int main(int argc,char**argv){Options o={.path="."};for(int i=1;i<argc;i++){char*a=argv[i];if(!strcmp(a,"-a")||!strcmp(a,"--all"))o.all=1;else if(!strcmp(a,"-l")||!strcmp(a,"--long"))o.long_mode=1;else if(!strcmp(a,"--tree"))o.tree=1;else if(!strcmp(a,"--stats"))o.stats=1;else if(!strcmp(a,"--dirs-first"))o.dirs_first=1;else if(!strcmp(a,"--reverse"))o.reverse=1;else if(!strcmp(a,"--no-sort"))o.no_sort=1;else if(!strcmp(a,"--lua"))o.use_lua=1;else if(!strcmp(a,"--config")&&i+1<argc)o.config=argv[++i],o.use_lua=1;else if(!strcmp(a,"-h")||!strcmp(a,"--help")){usage(stdout);return 0;}else if(a[0]=='-'){usage(stderr);return 2;}else o.path=a;}
#ifdef LSL_WITH_LUA
    char cfg[PATH_MAX];lua_State*L=NULL;if(o.use_lua){if(!o.config){if(default_config_path(cfg,sizeof cfg)<0)return 1;o.config=cfg;}L=start_lua(o.config);if(!L)return 1;}
#else
    if(o.use_lua){fprintf(stderr,"lsl: built without Lua support\n");return 1;}
#endif
    Entries es={0};Totals totals={0};scan(&o,&es,&totals);sort_dirs_first=o.dirs_first;sort_reverse=o.reverse;if (!o.no_sort && !o.tree) qsort(es.data, es.length, sizeof(*es.data), compare_entries);
    size_t cap=OUTPUT_BUFFER,used=0;char*out=malloc(cap);if(!out)die("malloc");for(size_t i=0;i<es.length;i++){Entry*e=&es.data[i];
#ifdef LSL_WITH_LUA
        if (L && !lua_accept(L, e)) {
            continue;
        }
        if (L) {
            size_t formatted_length;
            const char *formatted = lua_format(L, e, &formatted_length);
            if (formatted) {
                append(&out, &used, &cap, formatted, formatted_length);
                append(&out, &used, &cap, "\n", 1);
                lua_pop(L, 1);
                continue;
            }
        }
#endif
        if(o.tree){for(unsigned d=0;d<e->depth;d++)append(&out,&used,&cap,"│  ",strlen("│  ")); append(&out,&used,&cap,"├─ ",strlen("├─ "));}if(o.long_mode){if(load_stat(e)<0)continue;char m[11];mode_string(e->mode,m);appendf(&out,&used,&cap,"%s %10" PRIu64 " ",m,e->size);}const char*c=color_for(e);appendf(&out,&used,&cap,"%s%s %s%s\033[0m\n",c,icon_for(e),e->name,type_is_dir(e->type)?"/":"");if(used>OUTPUT_BUFFER){if(write(STDOUT_FILENO,out,used)<0)die("write");used=0;}}
    if (o.stats) appendf(&out, &used, &cap, "\n%" PRIu64 " files, %" PRIu64 " directories, %" PRIu64 " links, %" PRIu64 " bytes\n", totals.files, totals.dirs, totals.links, totals.bytes);
    if (used && write(STDOUT_FILENO, out, used) < 0) die("write");
#ifdef LSL_WITH_LUA
    if(L)lua_close(L);
#endif
    free(out);free(es.data);arena_destroy(&names);return 0;}
