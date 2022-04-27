# require "rbconfig"

require 'rake/extensiontask'
require 'rake/testtask'

require 'rdoc/task'
require 'rubygems/package_task'

require File.expand_path("../lib/platform", __FILE__)
include Platform

spec = Gem::Specification.load('ruby-lxc.gemspec')

Gem::PackageTask.new(spec) do |pkg|
end

Rake::ExtensionTask.new('lxc', spec) do |ext|
  ext.lib_dir = 'lib/lxc'
  ext.ext_dir = ext_path
end

Rake::RDocTask.new do |rd|
  rd.main = "#{ext_path}/lxc.c"
  rd.rdoc_dir = 'doc'
  rd.rdoc_files.include(FileList["#{ext_path}/lxc.c"])
end

Rake::TestTask.new do |t|
  t.libs << 'test'
  t.test_files = FileList['test/test_*.rb']
  t.verbose = true
end
