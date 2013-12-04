#include <ruby.h>
#include <string.h>
#include <lxc/lxccontainer.h>

#define SYMBOL(s) ID2SYM(rb_intern(s))

extern int lxc_wait_for_pid_status(pid_t pid);

static VALUE Error;

struct container_data {
    struct lxc_container *container;
};

static char **
ruby_to_c_string_array(VALUE rb_arr)
{
    int i;
    size_t len;
    char **arr;

    len = RARRAY_LEN(rb_arr);
    arr = malloc(len);
    if (arr == NULL)
        rb_raise(rb_eNoMemError, "unable to allocate array");
    for (i = 0; i < len; i++) {
        VALUE s = rb_ary_entry(rb_arr, i);
        arr[i] = strdup(StringValuePtr(s));
    }

    return arr;
}

static void
free_char_pointer_array(char **arr, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++)
        free(arr[i]);
    free(arr);
}

static void
container_free(void *data)
{
    struct container_data *d = (struct container_data *)data;
    lxc_container_put(d->container);
    free(d);
}

static VALUE
container_alloc(VALUE klass)
{
    struct container_data *data;
    return Data_Make_Struct(klass, struct container_data, NULL,
                            container_free, data);
}

static VALUE
container_initialize(int argc, VALUE *argv, VALUE self)
{
    char *name, *config_path;
    struct lxc_container *container;
    struct container_data *data;
    VALUE rb_name, rb_config_path;

    rb_scan_args(argc, argv, "11", &rb_name, &rb_config_path);

    name = StringValuePtr(rb_name);
    config_path = NIL_P(rb_config_path) ? NULL : StringValuePtr(rb_config_path);

    container = lxc_container_new(name, config_path);
    if (container == NULL)
        rb_raise(Error, "error creating container %s", name);

    Data_Get_Struct(self, struct container_data, data);
    container = data->container;

    if (rb_block_given_p())
        rb_yield(self);

    return self;
}

static VALUE
container_config_file_name(VALUE self)
{
    char *config_file_name;
    struct container_data *data;

    Data_Get_Struct(self, struct container_data, data);
    config_file_name = data->container->config_file_name(data->container);

    return rb_str_new2(config_file_name);
}

static VALUE
container_controllable_p(VALUE self)
{
    int controllable;
    struct container_data *data;

    Data_Get_Struct(self, struct container_data, data);
    controllable = data->container->may_control(data->container);

    return controllable ? Qtrue : Qfalse;
}

static VALUE
container_defined_p(VALUE self)
{
    int defined;
    struct container_data *data;

    Data_Get_Struct(self, struct container_data, data);
    defined = data->container->is_defined(data->container);

    return defined ? Qtrue : Qfalse;
}

static VALUE
container_init_pid(VALUE self)
{
    pid_t pid;
    struct container_data *data;

    Data_Get_Struct(self, struct container_data, data);
    pid = data->container->init_pid(data->container);

    return INT2NUM(pid);
}

static VALUE
container_name(VALUE self)
{
    struct container_data *data;

    Data_Get_Struct(self, struct container_data, data);

    return rb_str_new2(data->container->name);
}

static VALUE
container_running_p(VALUE self)
{
    int running;
    struct container_data *data;

    Data_Get_Struct(self, struct container_data, data);
    running = data->container->is_running(data->container);

    return running ? Qtrue : Qfalse;
}

static VALUE
container_state(VALUE self)
{
    const char *state;
    struct container_data *data;

    Data_Get_Struct(self, struct container_data, data);
    state = data->container->state(data->container);

    return rb_str_new2(state);
}

static VALUE
container_add_device_node(int argc, VALUE *argv, VALUE self)
{
    int ret;
    char *src_path, *dst_path;
    struct container_data *data;
    VALUE rb_src_path, rb_dst_path;

    rb_scan_args(argc, argv, "11", &rb_src_path, &rb_dst_path);
    src_path = NIL_P(rb_src_path) ? NULL : StringValuePtr(rb_src_path);
    dst_path = NIL_P(rb_dst_path) ? NULL : StringValuePtr(rb_dst_path);

    Data_Get_Struct(self, struct container_data, data);

    ret = data->container->add_device_node(data->container, src_path, dst_path);
    if (!ret)
        rb_raise(Error, "unable to add device node");

    return self;
}

static VALUE
lxc_attach_exec_block_cb(VALUE block)
{
    rb_funcall3(block, rb_intern("call"), 0, NULL);
    return INT2FIX(0);
}

static VALUE
lxc_attach_exec_rescue_cb()
{
    return INT2FIX(1);
}

static int
lxc_attach_exec(void *payload)
{
    VALUE res = rb_rescue(lxc_attach_exec_block_cb, (VALUE)payload,
                          lxc_attach_exec_rescue_cb, (VALUE)NULL);
    return FIX2INT(res);
}

static int
io_fileno(VALUE io)
{
    return NUM2INT(rb_funcall(io, rb_intern("fileno"), 0));
}

static int
is_integer(VALUE v)
{
    return (TYPE(v) == T_FIXNUM || TYPE(v) == T_BIGNUM);
}

static int
is_string(VALUE v)
{
    return TYPE(v) == T_STRING;
}

static int
is_string_array(VALUE v)
{
    size_t i, len;
    if (TYPE(v) != T_ARRAY)
        return 0;
    len = RARRAY_LEN(v);
    for (i = 0; i < len; i++) {
        if (TYPE(rb_ary_entry(v, i)) != T_STRING)
            return 0;
    }
    return 1;
}

static int
is_io(VALUE v)
{
    return !strcmp(rb_obj_classname(v), "IO");
}

static void
lxc_attach_free_options(lxc_attach_options_t *opts)
{
    if (!opts)
        return;
    if (opts->initial_cwd)
        free(opts->initial_cwd);
    if (opts->extra_env_vars) {
        free_char_pointer_array(opts->extra_env_vars,
                                RARRAY_LEN(opts->extra_env_vars));
    }
    if (opts->extra_keep_env) {
        free_char_pointer_array(opts->extra_keep_env,
                                RARRAY_LEN(opts->extra_keep_env));
    }
    free(opts);
}

static lxc_attach_options_t *
lxc_attach_parse_options(VALUE rb_opts)
{
    lxc_attach_options_t default_opts = LXC_ATTACH_OPTIONS_DEFAULT;
    lxc_attach_options_t *opts;
    VALUE rb_attach_flags, rb_namespaces, rb_personality, rb_initial_cwd;
    VALUE rb_uid, rb_gid, rb_env_policy, rb_extra_env_vars, rb_extra_keep_env;
    VALUE rb_stdin, rb_stdout, rb_stderr;

    if (!NIL_P(rb_opts) && TYPE(rb_opts) != T_HASH)
        rb_raise(Error, "need options to be a hash");

    opts = malloc(sizeof(*opts));
    if (opts == NULL)
        rb_raise(rb_eNoMemError, "unable to allocate options");
    memcpy(opts, &default_opts, sizeof(*opts));

    if (NIL_P(rb_opts))
        return opts;

    rb_attach_flags = rb_hash_aref(rb_opts, SYMBOL("flags"));
    if (is_integer(rb_attach_flags))
        opts->attach_flags = NUM2INT(rb_attach_flags);
    else
        goto err;
    rb_namespaces = rb_hash_aref(rb_opts, SYMBOL("namespaces"));
    if (is_integer(rb_namespaces))
        opts->namespaces = NUM2INT(rb_namespaces);
    else
        goto err;
    rb_personality = rb_hash_aref(rb_opts, SYMBOL("personality"));
    if (is_integer(rb_personality))
        opts->personality = NUM2INT(rb_personality);
    else
        goto err;
    rb_initial_cwd = rb_hash_aref(rb_opts, SYMBOL("initial_cwd"));
    if (is_string(rb_initial_cwd))
        opts->initial_cwd = StringValuePtr(rb_initial_cwd);
    else
        goto err;
    rb_uid = rb_hash_aref(rb_opts, SYMBOL("uid"));
    if (is_integer(rb_uid))
        opts->uid = NUM2INT(rb_uid);
    else
        goto err;
    rb_gid = rb_hash_aref(rb_opts, SYMBOL("gid"));
    if (is_integer(rb_gid))
        opts->gid = NUM2INT(rb_gid);
    else
        goto err;
    rb_env_policy = rb_hash_aref(rb_opts, SYMBOL("env_policy"));
    if (is_integer(rb_env_policy))
        opts->env_policy = NUM2INT(rb_env_policy);
    else
        goto err;
    rb_extra_env_vars = rb_hash_aref(rb_opts, SYMBOL("extra_env_vars"));
    if (is_string_array(rb_extra_env_vars))
        opts->extra_env_vars = ruby_to_c_string_array(rb_extra_env_vars);
    else
        goto err;
    rb_extra_keep_env = rb_hash_aref(rb_opts, SYMBOL("extra_keep_env"));
    if (is_string_array(rb_extra_keep_env))
        opts->extra_keep_env = ruby_to_c_string_array(rb_extra_keep_env);
    else
        goto err;
    rb_stdin = rb_hash_aref(rb_opts, SYMBOL("stdin"));
    if (is_io(rb_stdin))
        opts->stdin_fd = io_fileno(rb_stdin);
    else
        goto err;
    rb_stdout = rb_hash_aref(rb_opts, SYMBOL("stdout"));
    if (is_io(rb_stdout))
        opts->stdout_fd = io_fileno(rb_stdout);
    else
        goto err;
    rb_stderr = rb_hash_aref(rb_opts, SYMBOL("stderr"));
    if (is_io(rb_stderr))
        opts->stderr_fd = io_fileno(rb_stderr);
    else
        goto err;

    return opts;

err:
    lxc_attach_free_options(opts);
    return NULL;
}

static VALUE
container_attach(int argc, VALUE *argv, VALUE self)
{
    int wait;
    long ret;
    pid_t pid;
    lxc_attach_options_t *opts;
    struct lxc_container *container;
    struct container_data *data;
    VALUE block, rb_opts, rb_wait;

    if (!rb_block_given_p())
        rb_raise(Error, "no block given");
    block = rb_block_proc();

    rb_scan_args(argc, argv, "01", &rb_opts);
    opts = lxc_attach_parse_options(rb_opts);
    if (opts == NULL)
        rb_raise(Error, "unable to parse attach options");
    rb_wait = rb_hash_aref(rb_opts, SYMBOL("wait"));
    wait = rb_wait == Qnil || rb_wait == Qfalse ? 0 : 1;

    Data_Get_Struct(self, struct container_data, data);
    container = data->container;

    ret = container->attach(container, lxc_attach_exec, &block, opts, &pid);
    if (ret < 0)
        goto out;

    if (wait) {
        ret = lxc_wait_for_pid_status(pid);
        /* handle case where attach fails */
        if (WIFEXITED(ret) && WEXITSTATUS(ret) == 255)
            ret = -1;
    } else {
        ret = pid;
    }

out:
    lxc_attach_free_options(opts);
    return LONG2NUM(ret);
}

static VALUE
container_clear_config(VALUE self)
{
    struct container_data *data;
    Data_Get_Struct(self, struct container_data, data);
    data->container->clear_config(data->container);
    return self;
}

static VALUE
container_clear_config_item(VALUE self, VALUE rb_key)
{
    int ret;
    char *key;
    struct container_data *data;

    key = StringValuePtr(rb_key);

    Data_Get_Struct(self, struct container_data, data);

    ret = data->container->clear_config_item(data->container, key);
    if (!ret)
        rb_raise(Error, "unable to clear config item %s", key);

    return self;
}

static VALUE
container_clone(int argc, VALUE *argv, VALUE self)
{
    int flags;
    unsigned long new_size;
    char *new_name, *config_path, *bdev_type, *bdev_data;
    char **hook_args;
    struct lxc_container *container, *new_container;
    struct container_data *data;
    VALUE rb_new_name, rb_config_path, rb_bdev_type, rb_bdev_data, rb_hook_args;
    VALUE rb_opts;

    rb_scan_args(argc, argv, "01", &rb_opts);

    if (TYPE(rb_opts) != T_HASH)
        rb_raise(rb_eArgError, "clone options must be a hash");

    rb_new_name = rb_hash_aref(rb_opts, SYMBOL("new_name"));
    new_name = StringValuePtr(rb_new_name);

    rb_config_path = rb_hash_aref(rb_opts, SYMBOL("config_path"));
    config_path = StringValuePtr(rb_config_path);

    flags = NUM2INT(rb_hash_aref(rb_opts, SYMBOL("flags")));

    rb_bdev_type = rb_hash_aref(rb_opts, SYMBOL("bdev_type"));
    bdev_type = StringValuePtr(rb_bdev_type);

    rb_bdev_data = rb_hash_aref(rb_opts, SYMBOL("bdev_data"));
    bdev_data = StringValuePtr(rb_bdev_data);

    new_size = NUM2ULONG(rb_hash_aref(rb_opts, SYMBOL("new_size")));

    rb_hook_args = rb_hash_aref(rb_opts, SYMBOL("hook_args"));
    hook_args = NIL_P(rb_hook_args)
              ? NULL
              : ruby_to_c_string_array(rb_hook_args);

    if (hook_args)
        free_char_pointer_array(hook_args, RARRAY_LEN(hook_args));

    Data_Get_Struct(self, struct container_data, data);
    container = data->container;

    new_container = container->clone(container, new_name, config_path,
                                     flags, bdev_type, bdev_data, new_size,
                                     hook_args);
    if (new_container == NULL)
        rb_raise(Error, "unable to clone container");

    lxc_container_put(new_container);

    return self;
}

static VALUE
container_console(int argc, VALUE *argv, VALUE self)
{
    int ret;
    int tty_num = -1, stdin_fd = 0, stdout_fd = 1, stderr_fd = 2, escape = 1;
    struct container_data *data;
    struct lxc_container *container;
    VALUE rb_opts;

    rb_scan_args(argc, argv, "01", &rb_opts);
    switch (TYPE(rb_opts)) {
    case T_HASH:
        tty_num   = NUM2INT(rb_hash_aref(rb_opts, SYMBOL("tty_num")));
        stdin_fd  = NUM2INT(rb_hash_aref(rb_opts, SYMBOL("stdin_fd")));
        stdout_fd = NUM2INT(rb_hash_aref(rb_opts, SYMBOL("stdout_fd")));
        stderr_fd = NUM2INT(rb_hash_aref(rb_opts, SYMBOL("stderr_fd")));
        escape    = NUM2INT(rb_hash_aref(rb_opts, SYMBOL("escape")));
        break;
    default:
        rb_raise(rb_eArgError, "options must be a hash");
    }

    Data_Get_Struct(self, struct container_data, data);
    container = data->container;

    ret = container->console(container, tty_num, stdin_fd, stdout_fd, stderr_fd,
                             escape);
    if (ret != 0)
        rb_raise(Error, "unable to access container console");

    return self;
}

static VALUE
container_console_fd(int argc, VALUE *argv, VALUE self)
{
    int ret, tty_num, master_fd;
    struct container_data *data;
    VALUE rb_tty_num;
    VALUE rb_io_args[1];

    rb_scan_args(argc, argv, "01", &rb_tty_num);
    tty_num = NIL_P(rb_tty_num) ? -1 : NUM2INT(rb_tty_num);

    Data_Get_Struct(self, struct container_data, data);

    ret = data->container->console_getfd(data->container, &tty_num, &master_fd);
    if (ret < 0)
        rb_raise(Error, "unable to allocate tty");

    rb_io_args[0] = INT2NUM(master_fd);
    return rb_class_new_instance(1, rb_io_args, rb_cIO);
}

static VALUE
container_create(int argc, VALUE *argv, VALUE self)
{
    int ret, flags;
    char *template;
    char **args = { NULL };
    struct container_data *data;
    struct lxc_container *container;
    VALUE rb_template, rb_flags, rb_args;

    rb_scan_args(argc, argv, "12", &rb_template, &rb_flags, &rb_args);

    template = StringValuePtr(rb_template);
    flags = NIL_P(rb_flags) ? 0 : NUM2INT(rb_flags);
    if (!NIL_P(rb_args))
        args = ruby_to_c_string_array(rb_args);

    Data_Get_Struct(self, struct container_data, data);
    container = data->container;
    ret = container->create(container, template, NULL, NULL, flags, args);

    if (!NIL_P(rb_args))
        free_char_pointer_array(args, RARRAY_LEN(rb_args));

    if (!ret)
        rb_raise(Error, "unable to create container");

    return self;
}

static VALUE
container_destroy(VALUE self)
{
    int ret;
    struct container_data *data;

    Data_Get_Struct(self, struct container_data, data);

    ret = data->container->destroy(data->container);
    if (!ret)
        rb_raise(Error, "unable to destroy container");
    return self;
}

static VALUE
container_freeze(VALUE self)
{
    int ret;
    struct container_data *data;

    Data_Get_Struct(self, struct container_data, data);

    ret = data->container->freeze(data->container);
    if (!ret)
        rb_raise(Error, "unable to freeze container");

    return self;
}

static VALUE
container_cgroup_item(VALUE self, VALUE rb_key)
{
    int len1, len2;
    char *key, *value;
    struct container_data *data;
    struct lxc_container *container;
    VALUE ret;

    Data_Get_Struct(self, struct container_data, data);
    container = data->container;

    key = StringValuePtr(rb_key);
    len1 = container->get_cgroup_item(container, key, NULL, 0);
    if (len1 < 0)
        rb_raise(Error, "invalid cgroup entry for %s", key);

    value = malloc(sizeof(char) * len1 + 1);
    if (value == NULL)
        rb_raise(rb_eNoMemError, "unable to allocate cgroup value");

    len2 = container->get_cgroup_item(container, key, value, len1 + 1);
    if (len1 != len2) {
        free(value);
        rb_raise(Error, "unable to read cgroup value");
    }
    ret = rb_str_new2(value);
    free(value);

    return ret;
}

static VALUE
container_config_item(VALUE self, VALUE rb_key)
{
    int len1, len2;
    char *key, *value;
    struct container_data *data;
    struct lxc_container *container;
    VALUE ret;

    Data_Get_Struct(self, struct container_data, data);
    container = data->container;

    key = StringValuePtr(rb_key);
    len1 = container->get_config_item(container, key, NULL, 0);
    if (len1 < 0)
        rb_raise(Error, "invalid configuration key: %s", key);

    value = malloc(sizeof(char) * len1 + 1);
    if (value == NULL)
        rb_raise(rb_eNoMemError, "unable to allocate configuration value");

    len2 = container->get_config_item(container, key, value, len1 + 1);
    if (len1 != len2) {
        free(value);
        rb_raise(Error, "unable to read configuration file");
    }
    ret = rb_str_new2(value);
    free(value);

    return ret;
}

static VALUE
container_config_path(VALUE self)
{
    struct container_data *data;
    Data_Get_Struct(self, struct container_data, data);
    return rb_str_new2(data->container->get_config_path(data->container));
}

static VALUE
container_keys(VALUE self, VALUE rb_key)
{
    int len1, len2;
    char *key, *value;
    struct container_data *data;
    struct lxc_container *container;
    VALUE ret;

    Data_Get_Struct(self, struct container_data, data);
    container = data->container;

    key = StringValuePtr(rb_key);
    len1 = container->get_keys(container, key, NULL, 0);
    if (len1 < 0)
        rb_raise(Error, "invalid configuration key: %s", key);

    value = malloc(sizeof(char) * len1 + 1);
    if (value == NULL)
        rb_raise(rb_eNoMemError, "unable to allocate configuration value");

    len2 = container->get_keys(container, key, value, len1 + 1);
    if (len1 != len2) {
        free(value);
        rb_raise(Error, "unable to read configuration keys");
    }
    ret = rb_str_new2(value);
    free(value);

    return ret;
}

static VALUE
container_interfaces(VALUE self)
{
    int i, num_interfaces;
    char **interfaces;
    struct container_data *data;
    VALUE rb_interfaces;

    Data_Get_Struct(self, struct container_data, data);

    interfaces = data->container->get_interfaces(data->container);
    if (!interfaces)
        return rb_ary_new();

    for (num_interfaces = 0; interfaces[num_interfaces]; num_interfaces++)
        ;

    rb_interfaces = rb_ary_new2(num_interfaces);
    for (i = 0; i < num_interfaces; i++)
        rb_ary_store(rb_interfaces, i, rb_str_new2(interfaces[i]));

    free_char_pointer_array(interfaces, num_interfaces);

    return rb_interfaces;
}

static VALUE
container_ips(int argc, VALUE *argv, VALUE self)
{
    int i, num_ips, scope;
    char *interface, *family;
    char **ips;
    struct container_data *data;
    VALUE rb_ips, rb_interface, rb_family, rb_scope;

    rb_scan_args(argc, argv, "03", &rb_interface, &rb_family, &rb_scope);
    interface = NIL_P(rb_interface) ? NULL : StringValuePtr(rb_interface);
    family    = NIL_P(rb_family)    ? NULL : StringValuePtr(rb_family);
    scope     = NIL_P(rb_scope)     ? 0    : NUM2INT(rb_scope);

    Data_Get_Struct(self, struct container_data, data);

    ips = data->container->get_ips(data->container, interface, family, scope);
    if (ips == NULL)
        return rb_ary_new();

    for (num_ips = 0; ips[num_ips]; num_ips++)
        ;

    rb_ips = rb_ary_new2(num_ips);
    for (i = 0; i < num_ips; i++)
        rb_ary_store(rb_ips, i, rb_str_new2(ips[i]));

    free_char_pointer_array(ips, num_ips);

    return rb_ips;
}

static VALUE
container_load_config(int argc, VALUE *argv, VALUE self)
{
    int ret;
    char *path;
    struct container_data *data;
    VALUE rb_path;

    rb_scan_args(argc, argv, "01", &rb_path);
    path = NIL_P(rb_path) ? NULL : StringValuePtr(rb_path);

    Data_Get_Struct(self, struct container_data, data);

    ret = data->container->load_config(data->container, path);
    if (!ret)
        rb_raise(Error, "unable to load configuration file");

    return self;
}

static VALUE
container_reboot(VALUE self)
{
    int ret;
    struct container_data *data;

    Data_Get_Struct(self, struct container_data, data);

    ret = data->container->reboot(data->container);
    if (!ret)
        rb_raise(Error, "unable to reboot container");

    return self;
}

static VALUE
container_remove_device_node(int argc, VALUE *argv, VALUE self)
{
    int ret;
    char *src_path, *dst_path;
    struct lxc_container *container;
    struct container_data *data;
    VALUE rb_src_path, rb_dst_path;

    rb_scan_args(argc, argv, "11", &rb_src_path, &rb_dst_path);
    src_path = StringValuePtr(rb_src_path);
    dst_path = NIL_P(rb_dst_path) ? NULL : StringValuePtr(rb_dst_path);

    Data_Get_Struct(self, struct container_data, data);
    container = data->container;

    ret = container->remove_device_node(container, src_path, dst_path);
    if (!ret)
        rb_raise(Error, "unable to remove device node");

    return self;
}

static VALUE
container_save_config(int argc, VALUE *argv, VALUE self)
{
    int ret;
    char *path;
    struct container_data *data;
    VALUE rb_path;

    rb_scan_args(argc, argv, "01", &rb_path);
    path = NIL_P(rb_path) ? NULL : StringValuePtr(rb_path);

    Data_Get_Struct(self, struct container_data, data);

    ret = data->container->save_config(data->container, path);
    if (!ret)
        rb_raise(Error, "unable to save configuration file");

    return self;
}

static VALUE
container_set_cgroup_item(VALUE self, VALUE rb_key, VALUE rb_value)
{
    int ret;
    char *key, *value;
    struct container_data *data;

    key = StringValuePtr(rb_key);
    value = StringValuePtr(rb_value);

    Data_Get_Struct(self, struct container_data, data);

    ret = data->container->set_cgroup_item(data->container, key, value);
    if (!ret)
        rb_raise(Error, "unable to set cgroup item %s to %s", key, value);

    return self;
}

static VALUE
container_set_config_item(VALUE self, VALUE rb_key, VALUE rb_value)
{
    int ret;
    char *key, *value;
    struct container_data *data;

    key = StringValuePtr(rb_key);
    value = StringValuePtr(rb_value);

    Data_Get_Struct(self, struct container_data, data);

    ret = data->container->set_config_item(data->container, key, value);
    if (!ret)
        rb_raise(Error, "unable to set configuration item %s to %s", key, value);

    return self;
}

static VALUE
container_set_config_path(VALUE self, VALUE rb_path)
{
    int ret;
    char *path;
    struct container_data *data;

    path = StringValuePtr(rb_path);
    Data_Get_Struct(self, struct container_data, data);

    ret = data->container->set_config_path(data->container, path);
    if (!ret)
        rb_raise(Error, "unable to set configuration path to %s", path);

    return self;
}

static VALUE
container_shutdown(int argc, VALUE *argv, VALUE self)
{
    int ret, timeout;
    struct container_data *data;
    VALUE rb_timeout;

    rb_scan_args(argc, argv, "01", &rb_timeout);
    timeout = NIL_P(rb_timeout) ? -1 : NUM2INT(rb_timeout);

    Data_Get_Struct(self, struct container_data, data);

    ret = data->container->shutdown(data->container, timeout);
    if (!ret)
        rb_raise(Error, "unable to shutdown container");

    return self;
}

static VALUE
container_snapshot(int argc, VALUE *argv, VALUE self)
{
    int ret;
    char *path;
    char new_name[20];
    struct container_data *data;
    VALUE rb_path;

    rb_scan_args(argc, argv, "01", &rb_path);
    path = NIL_P(rb_path) ? NULL : StringValuePtr(rb_path);

    Data_Get_Struct(self, struct container_data, data);

    ret = data->container->snapshot(data->container, path);
    if (ret < 0)
        rb_raise(Error, "unable to snapshot container");

    ret = snprintf(new_name, 20, "snap%d", ret);
    if (ret < 0 || ret >= 20)
        rb_raise(Error, "unable to snapshot container");

    return rb_str_new2(new_name);
}

static VALUE
container_snapshot_destroy(VALUE self, VALUE rb_name)
{
    int ret;
    char *name;
    struct container_data *data;

    name = StringValuePtr(rb_name);

    Data_Get_Struct(self, struct container_data, data);

    ret = data->container->snapshot_destroy(data->container, name);
    if (!ret)
        rb_raise(Error, "unable to destroy snapshot");

    return self;
}

static VALUE
container_snapshot_list(VALUE self)
{
    int i, num_snapshots;
    struct lxc_snapshot *snapshots;
    struct container_data *data;
    VALUE rb_snapshots;

    Data_Get_Struct(self, struct container_data, data);

    num_snapshots = data->container->snapshot_list(data->container, &snapshots);
    if (num_snapshots < 0)
        rb_raise(Error, "unable to list snapshots");

    rb_snapshots = rb_ary_new2(num_snapshots);
    for (i = 0; i < num_snapshots; i++) {
        VALUE attrs = rb_ary_new2(4);
        rb_ary_store(attrs, 0, rb_str_new2(snapshots[i].name));
        rb_ary_store(attrs, 1, rb_str_new2(snapshots[i].comment_pathname));
        rb_ary_store(attrs, 2, rb_str_new2(snapshots[i].timestamp));
        rb_ary_store(attrs, 3, rb_str_new2(snapshots[i].lxcpath));
        snapshots[i].free(&snapshots[i]);
        rb_ary_store(rb_snapshots, i, attrs);
    }

    return rb_snapshots;
}

static VALUE
container_snapshot_restore(int argc, VALUE *argv, VALUE self)
{
    int ret;
    char *name, *new_name;
    struct container_data *data;
    VALUE rb_name, rb_new_name;

    rb_scan_args(argc, argv, "11", &rb_name, &rb_new_name);
    name = StringValuePtr(rb_name);
    new_name = NIL_P(rb_new_name) ? NULL : StringValuePtr(rb_new_name);

    Data_Get_Struct(self, struct container_data, data);

    ret = data->container->snapshot_restore(data->container, name, new_name);
    if (!ret)
        rb_raise(Error, "unable to restore snapshot");

    return self;
}

static VALUE
container_start(int argc, VALUE *argv, VALUE self)
{
    int ret, use_init, daemonize, close_fds;
    char **args;
    struct container_data *data;
    VALUE rb_use_init, rb_daemonize, rb_close_fds, rb_args, rb_opts;

    rb_scan_args(argc, argv, "01", &rb_opts);
    if (!NIL_P(rb_opts) && TYPE(rb_opts) != T_HASH)
        rb_raise(Error, "unable to read start options");

    rb_use_init = rb_hash_aref(rb_opts, SYMBOL("use_init"));
    use_init = (rb_use_init != Qnil) && (rb_use_init != Qfalse);

    rb_daemonize = rb_hash_aref(rb_opts, SYMBOL("daemonize"));
    daemonize = (rb_daemonize != Qnil) && (rb_daemonize != Qfalse);

    rb_close_fds = rb_hash_aref(rb_opts, SYMBOL("close_fds"));
    close_fds = (rb_close_fds != Qnil) && (rb_close_fds != Qfalse);

    rb_args = rb_hash_aref(rb_opts, SYMBOL("args"));
    args = NIL_P(rb_args) ? NULL : ruby_to_c_string_array(rb_args);

    Data_Get_Struct(self, struct container_data, data);

    data->container->want_close_all_fds(data->container, close_fds);
    data->container->want_daemonize(data->container, daemonize);
    ret = data->container->start(data->container, use_init, args);

    if (!NIL_P(rb_args))
        free_char_pointer_array(args, RARRAY_LEN(rb_args));

    if (!ret)
        rb_raise(Error, "unable to start container");

    return self;
}

static VALUE
container_stop(VALUE self)
{
    int ret;
    struct container_data *data;

    Data_Get_Struct(self, struct container_data, data);

    ret = data->container->stop(data->container);
    if (!ret)
        rb_raise(Error, "unable to stop container");

    return self;
}

static VALUE
container_unfreeze(VALUE self)
{
    int ret;
    struct container_data *data;

    Data_Get_Struct(self, struct container_data, data);

    ret = data->container->unfreeze(data->container);
    if (!ret)
        rb_raise(Error, "unable to unfreeze container");

    return self;
}

static VALUE
container_wait(int argc, VALUE *argv, VALUE self)
{
    int ret, timeout;
    char *state;
    struct container_data *data;
    VALUE rb_state, rb_timeout;

    rb_scan_args(argc, argv, "11", &rb_state, &rb_timeout);

    state = StringValuePtr(rb_state);
    timeout = NIL_P(rb_timeout) ? -1 : NUM2INT(rb_timeout);

    Data_Get_Struct(self, struct container_data, data);

    ret = data->container->wait(data->container, state, timeout);
    if (!ret)
        rb_raise(Error, "error waiting for container");

    return self;
}

void
Init_lxc(void)
{
    VALUE LXC = rb_define_module("LXC");
    VALUE Container = rb_define_class_under(LXC, "Container", rb_cObject);
    rb_define_alloc_func(Container, container_alloc);

    rb_define_method(Container, "initialize", container_initialize, -1);

    rb_define_method(Container, "config_file_name",
                     container_config_file_name, 0);
    rb_define_method(Container, "controllable?", container_controllable_p, 0);
    rb_define_method(Container, "defined?", container_defined_p, 0);
    rb_define_method(Container, "init_pid", container_init_pid, 0);
    rb_define_method(Container, "name", container_name, 0);
    rb_define_method(Container, "running?", container_running_p, 0);
    rb_define_method(Container, "state", container_state, 0);

    rb_define_method(Container, "add_device_node",
                     container_add_device_node, -1);
    rb_define_method(Container, "attach", container_attach, -1);
    rb_define_method(Container, "clear_config", container_clear_config, -1);
    rb_define_method(Container, "clear_config_item",
                     container_clear_config_item, 1);
    rb_define_method(Container, "clone", container_clone, -1);
    rb_define_method(Container, "console", container_console, -1);
    rb_define_method(Container, "console_fd", container_console_fd, -1);
    rb_define_method(Container, "create", container_create, -1);
    rb_define_method(Container, "destroy", container_destroy, 0);
    rb_define_method(Container, "freeze", container_freeze, 0);
    rb_define_method(Container, "cgroup_item", container_cgroup_item, 1);
    rb_define_method(Container, "config_item", container_config_item, 1);
    rb_define_method(Container, "config_path", container_config_path, 0);
    rb_define_method(Container, "keys", container_keys, 1);
    rb_define_method(Container, "interfaces", container_interfaces, 0);
    rb_define_method(Container, "ip_addresses", container_ips, -1);
    rb_define_method(Container, "load_config", container_load_config, -1);
    rb_define_method(Container, "reboot", container_reboot, 0);
    rb_define_method(Container, "remove_device_node",
                     container_remove_device_node, 0);
    rb_define_method(Container, "save_config", container_save_config, -1);
    rb_define_method(Container, "cgroup_item=", container_set_cgroup_item, 2);
    rb_define_method(Container, "config_item=", container_set_config_item, 2);
    rb_define_method(Container, "config_path=", container_set_config_path, 1);
    rb_define_method(Container, "shutdown", container_shutdown, -1);
    rb_define_method(Container, "snapshot", container_snapshot, -1);
    rb_define_method(Container, "snapshot_destroy",
                     container_snapshot_destroy, 1);
    rb_define_method(Container, "snapshot_list", container_snapshot_list, 0);
    rb_define_method(Container, "snapshot_restore",
                     container_snapshot_restore, -1);
    rb_define_method(Container, "start", container_start, -1);
    rb_define_method(Container, "stop", container_stop, 0);
    rb_define_method(Container, "unfreeze", container_unfreeze, 0);
    rb_define_method(Container, "wait", container_wait, -1);

    Error = rb_define_class_under(LXC, "Error", rb_eStandardError);
}