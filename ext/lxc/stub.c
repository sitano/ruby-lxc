#include <ruby.h>
#include <stdlib.h>

#if defined(_WIN32)
    #define PLATFORM_NAME "windows" // Windows
#elif defined(_WIN64)
    #define PLATFORM_NAME "windows" // Windows
#elif defined(__CYGWIN__) && !defined(_WIN32)
    #define PLATFORM_NAME "windows" // Windows (Cygwin POSIX under Microsoft Window)
#elif defined(__ANDROID__)
    #define PLATFORM_NAME "android" // Android (implies Linux, so it must come first)
#elif defined(__linux__)
    #define PLATFORM_NAME "linux" // Debian, Ubuntu, Gentoo, Fedora, openSUSE, RedHat, Centos and other
#elif defined(__unix__) || !defined(__APPLE__) && defined(__MACH__)
    #include <sys/param.h>
    #if defined(BSD)
        #define PLATFORM_NAME "bsd" // FreeBSD, NetBSD, OpenBSD, DragonFly BSD
    #endif
#elif defined(__hpux)
    #define PLATFORM_NAME "hp-ux" // HP-UX
#elif defined(_AIX)
    #define PLATFORM_NAME "aix" // IBM AIX
#elif defined(__APPLE__) && defined(__MACH__) // Apple OSX and iOS (Darwin)
    #include <TargetConditionals.h>
    #if TARGET_IPHONE_SIMULATOR == 1
        #define PLATFORM_NAME "ios" // Apple iOS
    #elif TARGET_OS_IPHONE == 1
        #define PLATFORM_NAME "ios" // Apple iOS
    #elif TARGET_OS_MAC == 1
        #define PLATFORM_NAME "osx" // Apple OSX
    #endif
#elif defined(__sun) && defined(__SVR4)
    #define PLATFORM_NAME "solaris" // Oracle Solaris, Open Indiana
#else
    #define PLATFORM_NAME NULL
#endif

void (*rb_thread_stop_timer_thread_ptr)();

static VALUE Container;
static VALUE Error;

static VALUE
method_missing_not_supported(int argc, VALUE *argv, VALUE self)
{
  rb_raise(rb_eLoadError, "LXC is not supported on this platform: " PLATFORM_NAME);
}

static VALUE
container_initialize(VALUE klass)
{
  rb_raise(rb_eLoadError, "LXC is not supported on this platform: " PLATFORM_NAME);
}

static int
locate_ruby_vm_funcs(void)
{
    char *f2_ofs;
    intptr_t f2;

    f2_ofs = getenv("RB_THREAD_STOP_TIMER_THREAD_OFFSET");
    if (!f2_ofs) {
        rb_raise(Error, "RB_THREAD_STOP_TIMER_THREAD_OFFSET environment variable is not set. Set it to: nm libruby.so | grep rb_thread_stop_timer_thread");
        return -1;
    }

    f2 = strtoll(f2_ofs, NULL, 16);
    if (f2 < 1) {
        rb_raise(Error,"RB_THREAD_STOP_TIMER_THREAD_OFFSET value is invalid");
        return -1;
    }

    // rb_thread_start_timer_thread_ptr = (void(*)())(info->dlpi_addr + info->dlpi_phdr[j].p_vaddr + f1);
    rb_thread_stop_timer_thread_ptr = (void(*)())(f2);

    return 0;
}

void
Init_lxc(void)
{
    VALUE LXC = rb_define_module("LXC");

    rb_define_singleton_method(LXC, "method_missing", method_missing_not_supported, -1);

    Container = rb_define_class_under(LXC, "Container", rb_cObject);

    rb_define_method(Container, "initialize", container_initialize, -1);
    rb_define_singleton_method(Container, "method_missing", method_missing_not_supported, -1);

    Error = rb_define_class_under(LXC, "Error", rb_eStandardError);

    if (!rb_thread_stop_timer_thread_ptr) {
        locate_ruby_vm_funcs();
    }

    if (!rb_thread_stop_timer_thread_ptr) {
        rb_raise(Error, "failed to locate rb_thread_stop_timer_thread()");
    }
}
