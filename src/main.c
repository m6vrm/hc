#define _DEFAULT_SOURCE

#include <assert.h>         // for assert
#include <dirent.h>         // for closedir, opendir, readdir, DIR
#include <stdbool.h>        // for true, bool, false
#include <stddef.h>         // for size_t, ptrdiff_t
#include <stdio.h>          // for NULL, fprintf, stderr, size_t, fclose
#include <stdlib.h>         // for free, EXIT_FAILURE, EXIT_SUCCESS
#include <string.h>         // for strerror, strcmp, strlen, strchr
#include <sys/dirent.h>     // for dirent, DT_DIR, DT_REG
#include <sys/errno.h>      // for errno, EEXIST
#include <sys/stat.h>       // for mkdir
#include <sys/syslimits.h>  // for PATH_MAX, NAME_MAX
#include <sys/types.h>      // for S_IRWXU, SEEK_END, SEEK_SET
#include <unistd.h>         // for optarg, getopt

#define VERSION 1.0

#define QUOTE(...) #__VA_ARGS__
#define STR(x) QUOTE(x)
#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))
#define PERROR(FMT, ...)                                                       \
    fprintf(stderr, FMT ": %s\n", __VA_ARGS__, strerror(errno))

#ifndef PATH_MAX
#define PATH_MAX 1023
#endif

#ifndef NAME_MAX
#define NAME_MAX 255
#endif

/// Memory

static void *realloc_safe(void *ptr, size_t size) {
    void *tmp = realloc(ptr, size);
    if (tmp == NULL) {
        free(ptr);
    }

    return tmp;
}

struct buf {
    size_t len;
    size_t cap;
    char *buf; // null-terminated
};

static void buf_realloc(struct buf *buf, size_t len) {
    assert(len > 0);

    buf->len = len;
    if (buf->len < buf->cap) {
        return;
    }

    char *old_buf = buf->buf;

    buf->cap = buf->len * 2 + 1;
    buf->buf = realloc_safe(buf->buf, buf->cap);

    // ensure empty string on first alloc
    if (old_buf == NULL) {
        *buf->buf = '\0';
    }
}

static void buf_free(struct buf buf) { free(buf.buf); }

/// Strings

static void strcpy_safe(char *dst, char *src, size_t size) {
    assert(dst != NULL);
    assert(src != NULL);
    assert(size > 0);

    snprintf(dst, size, "%s", src);
}

static void strcat_safe(char *dst, char *src, size_t size) {
    assert(dst != NULL);
    assert(src != NULL);
    assert(size > 0);

    size_t dst_len = strnlen(dst, size - 1);
    size_t src_len = strlen(src);

    dst += dst_len;
    size -= dst_len;
    if (src_len >= size) {
        src_len = size - 1;
    }

    memcpy(dst, src, src_len);
    dst[src_len] = '\0';
}

// dst must have enough space
static void strsub(char *dst, char *src, char *find, char *rep) {
    assert(dst != NULL);
    assert(src != NULL);
    assert(find != NULL);
    assert(rep != NULL);

    size_t find_len = strlen(find);
    size_t rep_len = strlen(rep);

    char *match;
    while ((match = strstr(src, find)) != NULL) {
        ptrdiff_t offset = match - src;

        // copy part before match
        memcpy(dst, src, offset);
        // replace match
        memcpy(dst + offset, rep, rep_len);

        dst += offset + rep_len;
        src += offset + find_len;
    }

    // copy remaining string
    strcpy(dst, src);
}

struct strsub_pair {
    char *find;
    char *rep;
};

static char *strsub_alloc(char *src, struct strsub_pair *pairs,
                          size_t pair_count) {

    assert(src != NULL);
    assert(pairs != NULL);

    // initial fat buffer
    size_t src_len = strlen(src);
    struct buf buf = {0};
    // +1, since we need two null-terminators (first one handled internally)
    buf_realloc(&buf, src_len + 1);
    strcpy(buf.buf, src);

    for (size_t i = 0; i < pair_count; ++i) {
        struct strsub_pair *pair = &pairs[i];
        assert(pair != NULL);
        assert(pair->find != NULL);

        // treat NULL as empty string
        char *rep = pair->rep != NULL ? pair->rep : "";

        size_t find_len = strlen(pair->find);
        size_t rep_len = strlen(rep);

        // calculate result buffer size
        size_t new_len = src_len;
        char *match = buf.buf;
        while ((match = strstr(match, pair->find)) != NULL) {
            new_len += rep_len - find_len;
            match += find_len;
        }

        // realloc if need more memory
        buf_realloc(&buf, src_len + new_len + 1); // additional null-terminator

        // substitute into second half of the buffer
        strsub(buf.buf + src_len + 1, buf.buf, pair->find, rep);
        // copy second half of the buffer to the first one
        memcpy(buf.buf, buf.buf + src_len + 1, new_len);
        buf.buf[new_len] = '\0'; // ensure C-string
        src_len = new_len;
    }

    return buf.buf;
}

/// FS

static char *file_alloc(char *path) {
    assert(path != NULL);

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        PERROR("can't open file: %s", path);
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        PERROR("fseek failed: %s", path);
        goto close;
    }

    // todo: better to read by chunks and realloc
    long buf_size = ftell(file);
    if (buf_size == -1) {
        PERROR("ftell failed: %s", path);
        goto close;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        PERROR("fseek failed: %s", path);
        goto close;
    }

    char *buf = malloc(buf_size + 1);
    buf[buf_size] = '\0';

    if (buf_size > 0 && fread(buf, buf_size, 1, file) != 1) {
        PERROR("fread failed: %s", path);
        goto free;
    }

    if (fclose(file) == EOF) {
        PERROR("fclose failed: %s", path);
        goto free;
    }

    return buf;

    // cleanup
free:
    free(buf);

close:
    fclose(file);
    return NULL;
}

static void file_write(char *path, char *str) {
    assert(path != NULL);
    assert(str != NULL);

    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        PERROR("can't open file: %s", path);
        return;
    }

    if (fputs(str, file) == EOF) {
        PERROR("fputs failed: %s", path);
        goto close;
    }

    // cleanup
close:
    fclose(file);
}

static void mkdir_p(char *path) {
    assert(path != NULL);

    char buf[PATH_MAX];
    strcpy_safe(buf, path, sizeof(buf));

    char *match = buf;
    while ((match = strchr(match, '/')) != NULL) {
        *match = '\0'; // treat slash as end of the string

        if (mkdir(buf, S_IRWXU) == -1 && errno != EEXIST) {
            PERROR("can't create dir: %s", buf);
            return;
        }

        *match = '/'; // then return it for nested directory
        ++match;
    }
}

/// Configuration

#define CONF_MAX 256
#define CONF_FM_DELIM "---\n"
#define CONF_FM_DELIM_LEN (sizeof(CONF_FM_DELIM) - 1)
#define CONF_KV_DELIM " = "
#define CONF_KV_DELIM_LEN (sizeof(CONF_KV_DELIM) - 1)
#define CONF_NULL_PH '\177' // DEL control character (ASCII 127)

struct conf_pair {
    char *key;
    char *val;
};

struct conf {
    struct conf_pair pairs[CONF_MAX];
    size_t pair_count;
    char *content;
    char *buf;
};

static void conf_read(struct conf *conf, char *str) {
    assert(conf != NULL);
    assert(str != NULL);

    conf->pair_count = 0;
    conf->content = NULL;
    conf->buf = str;

    // no front matter, assume everything is a content
    if (strncmp(conf->buf, CONF_FM_DELIM, CONF_FM_DELIM_LEN) != 0) {
        conf->content = conf->buf;
        return;
    }

    // read line by line
    char *match = conf->buf;
    while ((match = strchr(match, '\n')) != NULL) {
        *match = CONF_NULL_PH; // replace NL with null-terminator
        ++match;

        // end of key-value pairs, everything else is a content
        if (strncmp(match, CONF_FM_DELIM, CONF_FM_DELIM_LEN) == 0) {
            conf->content = match + CONF_FM_DELIM_LEN;
            break;
        }

        // find key-value delimiter
        char *val = strstr(match, CONF_KV_DELIM);
        if (val == NULL) {
            break;
        }

        // skip lines without delimiter
        if (val > strchr(match, '\n')) {
            continue;
        }

        *val = CONF_NULL_PH; // replace KV delimiter with null-terminator

        // key-value pair
        struct conf_pair pair = {match, val + CONF_KV_DELIM_LEN};
        if (conf->pair_count < CONF_MAX) {
            conf->pairs[conf->pair_count] = pair;
            ++conf->pair_count;
        } else {
            PERROR("too many key-value pairs: key = %s", pair.key);
        }
    }

    // replace placeholders with null-terminators to make actual C-strings
    match = conf->buf;
    while ((match = strchr(match, CONF_NULL_PH)) != NULL) {
        *match = '\0';
        ++match;
    }
}

static struct conf conf_alloc(char *path) {
    assert(path != NULL);

    struct conf conf = {0};
    char *str = file_alloc(path);
    if (str == NULL) {
        return conf;
    }

    conf_read(&conf, str);
    return conf;
}

static void conf_free(struct conf conf) { free(conf.buf); }

static char *conf_find(struct conf conf, size_t offset, char *key, char *val) {
    assert(conf.buf != NULL);
    assert(key != NULL);

    for (size_t i = offset; i < conf.pair_count; ++i) {
        struct conf_pair *pair = &conf.pairs[i];
        if (strcmp(pair->key, key) == 0) {
            return pair->val;
        }
    }

    return val;
}

/// Pages

#define PAGE_CHILDREN_MAX 4096 // todo: better to be dynamic
#define PAGE_SPECIAL_MAX 64
#define PAGE_SPECIAL_PREFIX '.'
#define PAGE_INDEX "index.html"

struct page {
    char name[NAME_MAX];
    bool is_parent; // parent can have no children
    struct conf conf;
    struct page *parent;
    struct page *children[PAGE_CHILDREN_MAX];
    size_t child_count;
    struct page *special[PAGE_SPECIAL_MAX];
    size_t special_count;
};

static struct page *page_alloc(char *name) {
    assert(name != NULL);

    struct page *page = calloc(1, sizeof(*page));
    strcpy_safe(page->name, name, sizeof(page->name));
    return page;
}

static void page_free(struct page *page) {
    if (page == NULL) {
        return;
    }

    // free child pages
    for (size_t i = 0; i < page->child_count; ++i) {
        struct page **child = &page->children[i];
        page_free(*child);
    }

    // free special pages
    for (size_t i = 0; i < page->special_count; ++i) {
        struct page **special = &page->special[i];
        page_free(*special);
    }

    conf_free(page->conf);
    free(page);
}

static bool page_add_special(struct page *page, struct page *special) {
    assert(page != NULL);

    if (page->special_count >= PAGE_SPECIAL_MAX) {
        PERROR("too many special pages: %s", page->name);
        return false;
    }

    special->parent = page;

    page->is_parent = true;
    page->special[page->special_count] = special;
    ++page->special_count;

    return true;
}

static bool page_add(struct page *page, struct page *child) {
    assert(page != NULL);

    if (child == NULL) {
        return false;
    }

    if (*child->name == PAGE_SPECIAL_PREFIX) {
        return page_add_special(page, child);
    }

    if (page->child_count >= PAGE_CHILDREN_MAX) {
        PERROR("too many children: %s", page->name);
        return false;
    }

    child->parent = page;

    page->is_parent = true;
    page->children[page->child_count] = child;
    ++page->child_count;

    return true;
}

// todo: not portable, whatever
static struct page *page_tree_alloc(char *path, char *name) {
    assert(path != NULL);
    assert(name != NULL);

    DIR *dir = opendir(path);
    if (dir == NULL) {
        PERROR("can't open dir: %s", path);
        return NULL;
    }

    // allocate root page
    struct page *page = page_alloc(name);
    page->is_parent = true;

    // allocate root conf
    char conf_path[PATH_MAX];
    snprintf(conf_path, sizeof(conf_path), "%s/" PAGE_INDEX, path);
    page->conf = conf_alloc(conf_path);

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            // skip special directories
            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0) {

                continue;
            }

            // recursively read child directory
            char dir_path[PATH_MAX];
            snprintf(dir_path, sizeof(dir_path), "%s/%s", path, entry->d_name);
            struct page *child = page_tree_alloc(dir_path, entry->d_name);
            if (!page_add(page, child)) {
                page_free(child);
            }
        } else if (entry->d_type == DT_REG) {
            // skip page index
            if (strcmp(entry->d_name, PAGE_INDEX) == 0) {
                continue;
            }

            // allocate child page
            struct page *child = page_alloc(entry->d_name);
            if (page_add(page, child)) {
                // allocate child conf
                snprintf(conf_path, sizeof(conf_path), "%s/%s", path,
                         entry->d_name);
                child->conf = conf_alloc(conf_path);
            } else {
                page_free(child);
            }
        }
    }

    closedir(dir);
    return page;
}

static char *page_conf(struct page *page, char *key, char *val) {
    assert(page != NULL);
    assert(key != NULL);

    while (page != NULL) {
        char *found = conf_find(page->conf, 0, key, NULL);
        if (found != NULL) {
            return found;
        }

        page = page->parent;
    }

    return val;
}

static char *page_content(struct page *page, char *val) {
    assert(page != NULL);

    struct conf conf = page->conf;
    if (conf.content != NULL) {
        return conf.content;
    }

    return val;
}

static struct page *page_root(struct page *page) {
    assert(page != NULL);

    if (page->parent == NULL) {
        return page;
    }

    return page_root(page->parent);
}

static struct page *page_find(struct page *tree, char *path) {
    assert(tree != NULL);
    assert(path != NULL);

    // traverse from root if needed
    if (*path == '/') {
        struct page *root = page_root(tree);
        return page_find(root, path + 1);
    }

    // split path
    char name[NAME_MAX + 1] = "";
    char child_path[PATH_MAX + 1] = "";
    sscanf(path, "%" STR(NAME_MAX) "[^/]/%" STR(PATH_MAX) "s", name,
           child_path);

    switch (*name) {
    case '\0':
        // empty name, found it
        return tree;
    case '.':
        if (name[1] != '\0') {
            // not a dot name
        } else if (!tree->is_parent && tree->parent != NULL) {
            // start from parent if current page is not parent
            return page_find(tree->parent, child_path);
        } else {
            // else just skip it
            return page_find(tree, child_path);
        }
    default:
        break;
    }

    // find child page
    for (size_t i = 0; i < tree->child_count; ++i) {
        struct page **child = &tree->children[i];
        if (strcmp((*child)->name, name) == 0) {
            return page_find(*child, child_path);
        }
    }

    // find special page
    for (size_t i = 0; i < tree->special_count; ++i) {
        struct page **special = &tree->special[i];
        if (strcmp((*special)->name, name) == 0) {
            return page_find(*special, child_path);
        }
    }

    // nothing found here, search in parent and so on
    if (tree->parent != NULL) {
        return page_find(tree->parent, path);
    }

    return NULL;
}

static void page_path_append(struct page *page, char *path, size_t size) {
    assert(page != NULL);
    assert(path != NULL);
    assert(size > 0);

    // don't include root name
    if (page->parent != NULL) {
        page_path_append(page->parent, path, size);
        strcat_safe(path, page->name, size);
    }

    if (page->is_parent) {
        strcat_safe(path, "/", size);
    }
}

static void page_url_append(struct page *page, char *url, size_t size) {
    assert(page != NULL);
    assert(url != NULL);
    assert(size > 0);

    page_path_append(page, url, size);
    if (page->is_parent) {
        strcat_safe(url, PAGE_INDEX, size);
    }
}

/// Templates

#define TPL_MAX 128

struct tpl {
    char path[PATH_MAX];
    char *str;
};

// templates are cached globally
char *s_tpl_path = "theme";
struct tpl s_tpls[TPL_MAX];
size_t s_tpl_count;

static char *tpl_cached(char *path) {
    assert(path != NULL);

    // find cached template
    for (size_t i = 0; i < s_tpl_count; ++i) {
        struct tpl *tpl = &s_tpls[i];
        if (strcmp(tpl->path, path) == 0) {
            return tpl->str;
        }
    }

    if (s_tpl_count >= TPL_MAX) {
        PERROR("too many templates: %s", path);
        return NULL;
    }

    // load new template and cache it (even if NULL)
    char full_path[PATH_MAX];
    snprintf(full_path, sizeof(full_path), "%s/%s", s_tpl_path, path);
    char *str = file_alloc(full_path);

    struct tpl tpl = {0};
    strcpy_safe(tpl.path, path, sizeof(tpl.path));
    tpl.str = str;

    s_tpls[s_tpl_count] = tpl;
    ++s_tpl_count;

    return str;
}

static void tpl_cache_free(void) {
    for (size_t i = 0; i < s_tpl_count; ++i) {
        struct tpl *tpl = &s_tpls[i];
        free(tpl->str);
    }
}

/// Plugins

char *s_root_url = "";

/// Blog plugin

#define PLUGIN_BLOG_DATE_FORMAT "YYYY-mm-dd"
#define PLUGIN_BLOG_DATE_LEN sizeof(PLUGIN_BLOG_DATE_FORMAT)
#define PLUGIN_BLOG_PAGE "blog"

static int compare_page_name(const void *a, const void *b) {
    assert(a != NULL);
    assert(b != NULL);

    struct page *page1 = *(struct page **)a;
    struct page *page2 = *(struct page **)b;
    assert(page1 != NULL);
    assert(page2 != NULL);

    return strcmp(page2->name, page1->name);
}

static char *plugin_blog_list_alloc(struct page *page) {
    assert(page != NULL);

    struct page *blog = page_find(page, PLUGIN_BLOG_PAGE);
    if (blog == NULL) {
        return NULL;
    }

    char *tpl = tpl_cached("blog/list.html");
    if (tpl == NULL) {
        return NULL;
    }

    // sort children by date (which is really just a name)
    qsort(blog->children, blog->child_count, sizeof(*blog->children),
          compare_page_name);

    struct buf buf = {0};
    for (size_t i = 0; i < blog->child_count; ++i) {
        struct page **post = &blog->children[i];

        char *title = page_conf(*post, "title", NULL);

        char date[PLUGIN_BLOG_DATE_LEN] = "";
        strcat_safe(date, (*post)->name, sizeof(date));

        char url[PATH_MAX] = "";
        strcat_safe(url, s_root_url, sizeof(url));
        page_url_append(*post, url, sizeof(url));

        struct strsub_pair pairs[] = {
            {"{{ title }}", title}, //
            {"{{ date }}", date},   //
            {"{{ url }}", url},     //
        };

        char *replaced = strsub_alloc(tpl, pairs, ARRAY_LEN(pairs));
        buf_realloc(&buf, buf.len + strlen(replaced));
        strcat(buf.buf, replaced);
        free(replaced);
    }

    return buf.buf;
}

static char *plugin_blog_post_alloc(struct page *page) {
    assert(page != NULL);

    char *tpl = tpl_cached("blog/post.html");
    if (tpl == NULL) {
        return NULL;
    }

    char *content = page_content(page, NULL);
    if (content == NULL) {
        return NULL;
    }

    char *title = page_conf(page, "title", NULL);

    char date[PLUGIN_BLOG_DATE_LEN] = "";
    strcat_safe(date, page->name, sizeof(date));

    struct strsub_pair pairs[] = {
        {"{{ content }}", content}, //
        {"{{ title }}", title},     //
        {"{{ date }}", date},       //
    };

    return strsub_alloc(tpl, pairs, ARRAY_LEN(pairs));
}

/// Page plugin

static char *plugin_page_alloc(struct page *page) {
    assert(page != NULL);

    char *tpl = tpl_cached("page.html");
    if (tpl == NULL) {
        return NULL;
    }

    char *content = page_content(page, NULL);
    if (content == NULL) {
        return NULL;
    }

    char *title = page_conf(page, "title", NULL);
    struct strsub_pair pairs[] = {
        {"{{ content }}", content}, //
        {"{{ title }}", title},     //
    };

    return strsub_alloc(tpl, pairs, ARRAY_LEN(pairs));
}

/// Menu plugin

static char *plugin_menu_alloc(struct page *page) {
    assert(page != NULL);

    struct page *menu = page_find(page, ".menu.html");
    if (menu == NULL) {
        return NULL;
    }

    char *tpl = tpl_cached("menu.html");
    if (tpl == NULL) {
        return NULL;
    }

    struct buf buf = {0};
    for (size_t i = 0; i < menu->conf.pair_count; i += 2) {
        char *title = conf_find(menu->conf, i, "title", NULL);
        char *page_url = conf_find(menu->conf, i, "url", NULL);
        char *page_path = conf_find(menu->conf, i, "path", NULL);

        char url[PATH_MAX] = "#";
        if (page_url != NULL) {
            strcpy_safe(url, page_url, sizeof(url));
        } else if (page_path != NULL) {
            struct page *target_page = page_find(menu, page_path);
            if (target_page != NULL) {
                strcpy_safe(url, s_root_url, sizeof(url));
                page_url_append(target_page, url, sizeof(url));
            }
        }

        struct strsub_pair pairs[] = {
            {"{{ title }}", title}, //
            {"{{ url }}", url},     //
        };

        char *replaced = strsub_alloc(tpl, pairs, ARRAY_LEN(pairs));
        buf_realloc(&buf, buf.len + strlen(replaced));
        strcat(buf.buf, replaced);
        free(replaced);
    }

    return buf.buf;
}

/// Home plugin

static char *plugin_home_alloc(struct page *page) {
    assert(page != NULL);

    char *tpl = tpl_cached("home.html");
    if (tpl == NULL) {
        return NULL;
    }

    char *content = page_content(page, NULL);
    struct strsub_pair pairs[] = {
        {"{{ content }}", content}, //
    };

    return strsub_alloc(tpl, pairs, ARRAY_LEN(pairs));
}

/// Base plugin

#define PLUGIN_BASE_TITLE_MAX 128

static char *plugin_base_alloc(struct page *page) {
    assert(page != NULL);

    char *tpl = tpl_cached("base.html");
    if (tpl == NULL) {
        return NULL;
    }

    char *content = NULL;
    if (page->parent == NULL) {
        // home page
        content = plugin_home_alloc(page);
    } else if (strcmp(page->parent->name, PLUGIN_BLOG_PAGE) == 0) {
        // blog page
        content = plugin_blog_post_alloc(page);
    } else {
        // simple page
        content = plugin_page_alloc(page);
    }

    if (content == NULL) {
        return NULL;
    }

    char *footer = page_conf(page, "footer", NULL);
    char *blog_list = plugin_blog_list_alloc(page);
    char *menu = plugin_menu_alloc(page);
    char *desc = page_conf(page, "meta.description", NULL);

    char title[PLUGIN_BASE_TITLE_MAX] = "";
    char *site_name = page_conf(page, "site.name", NULL);
    if (page->parent != NULL) {
        char *page_title = page_conf(page, "title", NULL);
        char *title_delim = page_conf(page, "site.title.delimiter", " | ");
        strcat_safe(title, page_title, sizeof(title));
        strcat_safe(title, title_delim, sizeof(title));
        strcat_safe(title, site_name, sizeof(title));
    } else {
        strcat_safe(title, site_name, sizeof(title));
    }

    struct strsub_pair pairs[] = {
        {"{{ content }}", content},  //
        {"{{ footer }}", footer},    //
        {"{{ blog }}", blog_list},   //
        {"{{ menu }}", menu},        //
        {"{{ description }}", desc}, //
        {"{{ title }}", title},      //
        {"{{ name }}", site_name},   //
        {"{{ root }}", s_root_url},  //
    };

    char *str = strsub_alloc(tpl, pairs, ARRAY_LEN(pairs));
    free(content);
    free(blog_list);
    free(menu);
    return str;
}

/// Generate

static void generate_pages(struct page *page, char *out_path) {
    assert(page != NULL);
    assert(out_path != NULL);

    // result path for page
    char path[PATH_MAX];
    strcpy_safe(path, out_path, sizeof(path));
    page_url_append(page, path, sizeof(path));

    // write generated page
    char *str = plugin_base_alloc(page);
    if (str != NULL) {
        mkdir_p(path);
        file_write(path, str);
        free(str);
    }

    // generate children
    for (size_t i = 0; i < page->child_count; ++i) {
        struct page **child = &page->children[i];
        generate_pages(*child, out_path);
    }
}

#ifndef TEST

/// EP

int main(int argc, char *argv[]) {
    char *in_path = "content";
    char *out_path = "public";

    int opt;
    while ((opt = getopt(argc, argv, "i:o:t:r:v")) != -1) {
        switch (opt) {
        case 'i':
            in_path = optarg;
            break;
        case 'o':
            out_path = optarg;
            break;
        case 't':
            s_tpl_path = optarg;
            break;
        case 'r':
            s_root_url = optarg;
            break;
        case 'v':
            puts("version " STR(VERSION));
            return EXIT_SUCCESS;
        default:
            fprintf(stderr,
                    "Usage: %s [-i input dir] [-o output dir] [-t theme dir] "
                    "[-r root url] [-v]\n",
                    argv[0]);
            return EXIT_FAILURE;
        }
    }

    struct page *tree = page_tree_alloc(in_path, "");
    if (tree == NULL) {
        return EXIT_FAILURE;
    }

    generate_pages(tree, out_path);

    // cleanup
    page_free(tree);
    tpl_cache_free();

    puts("done");

    return EXIT_SUCCESS;
}

#else

/// Tests

static void test_buf(void) {
    struct buf buf = {0};
    buf_realloc(&buf, 5);
    assert(buf.len == 5);
    assert(buf.cap == 11);

    buf_realloc(&buf, 20);
    assert(buf.len == 20);
    assert(buf.cap == 41);

    buf_free(buf);
}

static void test_strcpy_safe(void) {
    char buf[8] = "hello";
    strcpy_safe(buf, "hello, world", sizeof(buf));
    assert(strcmp(buf, "hello, ") == 0);
}

static void test_strcat_safe(void) {
    char buf[8] = "hello";
    strcat_safe(buf, ", world", sizeof(buf));
    assert(strcmp(buf, "hello, ") == 0);
}

static void test_strsub_alloc(void) {
    char *str = "original read-only string";

    struct strsub_pair pairs[] = {
        {"read", "write"},
        {"string", "character array"},
        {"original", NULL},
        // serial substitutions (e.g. string -> array -> sequence) are allowed,
        // so order matters
        {"array", "sequence"},
    };

    char *replaced = strsub_alloc(str, pairs, ARRAY_LEN(pairs));
    assert(strcmp(replaced, " write-only character sequence") == 0);
    assert(strcmp(str, "original read-only string") == 0);

    free(replaced);
}

static void test_conf_read(void) {
    struct conf conf = {0};
    char full_str[] = "---\n\
key 1 = value 1\n\
key 2 = value 2\n\
---\n\
multiline\n\
test = content";

    conf_read(&conf, full_str);
    assert(conf.pair_count == 2);
    assert(strcmp(conf.pairs[0].key, "key 1") == 0);
    assert(strcmp(conf.pairs[0].val, "value 1") == 0);
    assert(strcmp(conf.pairs[1].key, "key 2") == 0);
    assert(strcmp(conf.pairs[1].val, "value 2") == 0);
    assert(strcmp(conf.content, "multiline\ntest = content") == 0);

    char keys_str[] = "---\n\
key 1 = value 1\n\
key 2 = value 2\n\
---";

    conf_read(&conf, keys_str);
    assert(conf.pair_count == 2);
    assert(strcmp(conf.pairs[0].key, "key 1") == 0);
    assert(strcmp(conf.pairs[0].val, "value 1") == 0);
    assert(strcmp(conf.pairs[1].key, "key 2") == 0);
    assert(strcmp(conf.pairs[1].val, "value 2") == 0);
    assert(conf.content == NULL);

    char content_str[] = "---\n\
---\n\
multiline\n\
content";

    conf_read(&conf, content_str);
    assert(conf.pair_count == 0);
    assert(strcmp(conf.content, "multiline\ncontent") == 0);

    char invalid_str[] = "invalid";
    conf_read(&conf, invalid_str);
    assert(conf.pair_count == 0);
    assert(strcmp(conf.content, "invalid") == 0);
}

static void test_conf_find(void) {
    struct conf conf = {0};
    char str[] = "---\n\
key 1 = value 1\n\
invalid line should be skipped\n\
key 2 = value 2\n\
\n\
key 1 = value 3\n\
---";

    conf_read(&conf, str);

    char *val = conf_find(conf, 0, "key 1", NULL);
    assert(strcmp(val, "value 1") == 0);

    val = conf_find(conf, 0, "key 2", NULL);
    assert(strcmp(val, "value 2") == 0);

    val = conf_find(conf, 0, "key 3", "default");
    assert(strcmp(val, "default") == 0);

    val = conf_find(conf, 0, "key 3", NULL);
    assert(val == NULL);

    val = conf_find(conf, 2, "key 1", NULL);
    assert(strcmp(val, "value 3") == 0);
}

static void test_page_alloc(void) {
    struct page *page = page_alloc("name");

    assert(strcmp(page->name, "name") == 0);
    assert(page->is_parent == false);
    assert(page->parent == NULL);
    assert(page->child_count == 0);
    assert(page->special_count == 0);

    free(page);
}

static void test_page_add(void) {
    struct page *root = page_alloc("root");
    struct page *child1 = page_alloc("child1");
    struct page *child2 = page_alloc("child2");
    struct page *child3 = page_alloc(".child3");

    assert(root->is_parent == false);
    assert(root->child_count == 0);
    assert(root->special_count == 0);
    assert(child1->parent == NULL);
    assert(child2->parent == NULL);

    page_add(root, child1);
    assert(root->is_parent == true);
    assert(root->child_count == 1);
    assert(root->special_count == 0);
    assert(child1->parent == root);

    page_add(root, child2);
    assert(root->child_count == 2);
    assert(root->special_count == 0);
    assert(child2->parent == root);

    page_add(root, child3);
    assert(root->child_count == 2);
    assert(root->special_count == 1);
    assert(child3->parent == root);

    page_free(root);
}

static void test_page_conf(void) {
    struct page *root = page_alloc("root");
    struct page *child1 = page_alloc("child1");
    struct page *child2 = page_alloc("child2");
    page_add(root, child1);
    page_add(root, child2);

    char root_str[] = "---\n\
key 1 = value 1\n\
key 2 = value 2\n\
---";
    conf_read(&root->conf, root_str);

    char child1_str[] = "---\n\
key 1 = value 1 child 1\n\
---";
    conf_read(&child1->conf, child1_str);

    char child2_str[] = "---\n\
key 2 = value 2 child 2\n\
---";
    conf_read(&child2->conf, child2_str);

    char *val = page_conf(root, "key 1", NULL);
    assert(strcmp(val, "value 1") == 0);
    val = page_conf(root, "key 2", NULL);
    assert(strcmp(val, "value 2") == 0);

    val = page_conf(child1, "key 1", NULL);
    assert(strcmp(val, "value 1 child 1") == 0);
    val = page_conf(child1, "key 2", NULL);
    assert(strcmp(val, "value 2") == 0);
    val = page_conf(child1, "key 3", NULL);
    assert(val == NULL);

    val = page_conf(child2, "key 1", NULL);
    assert(strcmp(val, "value 1") == 0);
    val = page_conf(child2, "key 2", NULL);
    assert(strcmp(val, "value 2 child 2") == 0);
    val = page_conf(child2, "key 3", "default");
    assert(strcmp(val, "default") == 0);

    free(root);
    free(child1);
    free(child2);
}

static void test_page_content(void) {
    struct page *root = page_alloc("root");
    struct page *child1 = page_alloc("child1");
    struct page *child2 = page_alloc("child2");
    page_add(root, child1);
    page_add(root, child2);

    char root_str[] = "root content";
    conf_read(&root->conf, root_str);

    char child1_str[] = "";
    conf_read(&child1->conf, child1_str);

    char child2_str[] = "---\n";
    conf_read(&child2->conf, child2_str);

    char *content = page_content(root, NULL);
    assert(strcmp(content, "root content") == 0);

    content = page_content(child1, NULL);
    assert(strcmp(content, "") == 0);

    content = page_content(child2, "default");
    assert(strcmp(content, "default") == 0);

    free(root);
    free(child1);
    free(child2);
}

static void test_page_find(void) {
    struct page *root = page_alloc("root");
    struct page *child1 = page_alloc("child1");
    struct page *child2 = page_alloc("child2");
    struct page *child3 = page_alloc(".child3");
    page_add(root, child1);
    page_add(root, child2);
    page_add(child2, child3);

    struct page *page = page_find(root, "");
    assert(page == root);

    page = page_find(root, "/child1");
    assert(page == child1);
    page = page_find(root, "child1");
    assert(page == child1);

    page = page_find(root, "/child2");
    assert(page == child2);
    page = page_find(root, "child2");
    assert(page == child2);

    page = page_find(root, "/child2/.child3");
    assert(page == child3);
    page = page_find(root, "child2/.child3");
    assert(page == child3);

    page = page_find(root, "/child2/child4");
    assert(page == NULL);
    page = page_find(root, "child2/child4");
    assert(page == NULL);

    page = page_find(child3, "child2");
    assert(page == child2);

    page = page_find(child3, "");
    assert(page == child3);

    page = page_find(child3, "/");
    assert(page == root);

    page = page_find(child3, "/child2");
    assert(page == child2);

    page = page_find(child1, ".");
    assert(page == root);

    page = page_find(child2, ".");
    assert(page == child2);

    page = page_find(child3, ".");
    assert(page == child2);

    page_free(root);
}

static void test_page_root(void) {
    struct page *root = page_alloc("root");
    struct page *child1 = page_alloc("child1");
    struct page *child2 = page_alloc("child2");
    struct page *child3 = page_alloc(".child3");
    page_add(root, child1);
    page_add(root, child2);

    assert(page_root(root) == root);
    assert(page_root(child1) == root);
    assert(page_root(child2) == root);
    assert(page_root(child3) == child3);
}

static void test_page_path_append(void) {
    struct page *root = page_alloc("root");
    struct page *child1 = page_alloc("child1");
    struct page *child2 = page_alloc("child2");
    struct page *child3 = page_alloc(".child3");
    page_add(root, child1);
    page_add(root, child2);
    page_add(child2, child3);

    char path[32] = "";

    page_path_append(root, path, sizeof(path));
    assert(strcmp(path, "/") == 0);
    path[0] = '\0';

    page_path_append(child1, path, sizeof(path));
    assert(strcmp(path, "/child1") == 0);
    path[0] = '\0';

    page_path_append(child2, path, sizeof(path));
    assert(strcmp(path, "/child2/") == 0);
    path[0] = '\0';

    page_path_append(child3, path, sizeof(path));
    assert(strcmp(path, "/child2/.child3") == 0);
    path[0] = '\0';

    page_free(root);
}

static void test_page_url_append(void) {
    struct page *root = page_alloc("root");
    struct page *child1 = page_alloc("child1");
    struct page *child2 = page_alloc("child2");
    struct page *child3 = page_alloc(".child3");
    page_add(root, child1);
    page_add(root, child2);
    page_add(child2, child3);

    char url[32] = "";

    page_url_append(root, url, sizeof(url));
    assert(strcmp(url, "/index.html") == 0);
    url[0] = '\0';

    page_url_append(child1, url, sizeof(url));
    assert(strcmp(url, "/child1") == 0);
    url[0] = '\0';

    page_url_append(child2, url, sizeof(url));
    assert(strcmp(url, "/child2/index.html") == 0);
    url[0] = '\0';

    page_url_append(child3, url, sizeof(url));
    assert(strcmp(url, "/child2/.child3") == 0);
    url[0] = '\0';

    page_free(root);
}

static void test_page_find_by_page_path(void) {
    struct page *root = page_alloc("root");
    struct page *child1 = page_alloc("child1");
    struct page *child2 = page_alloc("child2");
    struct page *child3 = page_alloc(".child3");
    page_add(root, child1);
    page_add(root, child2);
    page_add(child2, child3);

    char path[32] = "";

    page_path_append(root, path, sizeof(path));
    struct page *page = page_find(root, path);
    assert(page == root);
    path[0] = '\0';

    page_path_append(child1, path, sizeof(path));
    page = page_find(root, path);
    assert(page == child1);
    path[0] = '\0';

    page_path_append(child2, path, sizeof(path));
    page = page_find(root, path);
    assert(page == child2);
    path[0] = '\0';

    page_path_append(child3, path, sizeof(path));
    page = page_find(root, path);
    assert(page == child3);
    path[0] = '\0';

    page_free(root);
}

int main(void) {
    test_buf();
    test_strcpy_safe();
    test_strcat_safe();
    test_strsub_alloc();

    test_conf_read();
    test_conf_find();

    test_page_alloc();
    test_page_add();
    test_page_conf();
    test_page_content();
    test_page_root();
    test_page_find();
    test_page_path_append();
    test_page_url_append();
    test_page_find_by_page_path();

    puts("success");

    return EXIT_SUCCESS;
}

#endif
