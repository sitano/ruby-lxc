require 'mkmf'
require "rbconfig"

def linux?
  host_os = RbConfig::CONFIG['host_os'].downcase
  host_os =~ /linux/
end

def add_define(name)
  $defs.push("-D#{name}")
end

if linux?
  abort 'missing liblxc' unless find_library('lxc', 'lxc_container_new')
  abort 'missing lxc/lxccontainer.h' unless have_header('lxc/lxccontainer.h')

  add_define "HAVE_RB_THREAD_CALL_WITHOUT_GVL" if have_func('rb_thread_call_without_gvl')
  add_define "HAVE_RB_THREAD_BLOCKING_REGION" if have_func('rb_thread_blocking_region')

  $CFLAGS += " -Wall #{ENV['CFLAGS']}"

  $srcs = ["lxc.c"]
else
  $srcs = ["stub.c"]
end

create_makefile('lxc/lxc')
