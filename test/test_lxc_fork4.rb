# $ LD_LIBRARY_PATH=~/lxc/src/lxc/.libs systemd-run --unit=myshell --user --scope -p "Delegate=pids memory" ruby -I test test/test_lxc_fork4.rb -n test_attach

$:.unshift File.expand_path(File.join(File.dirname(__FILE__), '../lib'))

require 'benchmark'
require 'fileutils'
require 'lxc'
require 'test/unit'

class TestLXCFork < Test::Unit::TestCase
  @@shared = "shared memory"

  def setup
    # if Process::Sys::geteuid != 0
    #   raise 'This test must be run as root'
    # end

    LXC::init_log("ERROR")

    $stdout.sync = true
  end

  def prefix
    "pid=#{Process.pid}, ppid=#{Process.ppid}"
  end

  def now
    Process.clock_gettime(Process::CLOCK_MONOTONIC, :nanosecond)
  end

  def to_ms(d)
    d / 1000000
  end

  def putx(s)
    puts "[#{prefix}] #{s}"
  end

  def measure(label) # :yield:
    t0, r0 = Process.times, now
    ret = yield
    t1, r1 = Process.times, now
    putx "#{label}: " + Benchmark::Tms.new(t1.utime  - t0.utime,
                       t1.stime  - t0.stime,
                       t1.cutime - t0.cutime,
                       t1.cstime - t0.cstime,
                       (1.0 * r1 - r0)/1000000000).to_s
    return ret
  end

  def clone(template = "test_template", gen = "test")
    path = LXC.global_config_item("lxc.lxcpath")
    id = now
    name = "#{gen}_#{id}"
    putx path

    # measure "clone #{name}" do container.clone(name) end
    # lxc: ruby: storage/storage.c: storage_copy: 333 Original rootfs path "/home/sitano.public/.local/share/lxc/test/rootfs" does not include container name "test_template"
    # lxc ruby 20211119150527.545 ERROR    lxccontainer - lxccontainer.c:copy_storage:3576 - Error copying storage.
    # lxc: ruby: lxccontainer.c: copy_storage: 3576 Error copying storage.

    measure "clone #{name}" do
      FileUtils.mkdir "#{path}/#{name}"
      FileUtils.cp "#{path}/#{template}/config","#{path}/#{name}/config"
      # TODO: FileUtils.chown '100000', 'sitano', "#{path}/#{name}"
    end

    return LXC::Container.new(name)
  end

  def test_attach
    cs = []
    ps = []
    ns = 1..5

    for i in ns do
      container = clone
      assert ! container.running?
      cs.push container

      measure "start" do container.start end
      assert container.running?
    end

    putx "started"

    for c in cs do
       pid = measure "attach" do c.attach end
       # pid = c.attach
       # ~
       # pid = Process.fork

       putx "attached: #{pid}"

       if pid > 0
         ps.push(pid)
       else
         putx "#{@@shared} is present"
         fork do
           putx "and fork"
         end
         sleep(1)
         putx "exiting from"
         Process.exit(0)
       end
    end

    putx "attached"

    for pid in ps
      putx "waiting: #{pid}"
      Process.waitpid(pid)
      putx "done with child=#{pid}"
    end

    putx "done"

    for c in cs do
        measure "stop" do c.stop end
        assert ! c.running?
    end

    putx "stopped"
  end
end
