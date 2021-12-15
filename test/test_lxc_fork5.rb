$:.unshift File.expand_path(File.join(File.dirname(__FILE__), '../lib'))

require 'test/unit'
require 'lxc'

class TestLXCFork < Test::Unit::TestCase
  @@shared = "shared memory"

  def setup
    # if Process::Sys::geteuid != 0
    #   raise 'This test must be run as root'
    # end

    LXC::init_log("WARN")

    $stdout.sync = true
  end

  def prefix
    "pid=#{Process.pid}, ppid=#{Process.ppid}"
  end

  def now
    Process.clock_gettime(Process::CLOCK_MONOTONIC, :nanosecond)
  end

  def test_attach
    @name = 'test'
    @container = LXC::Container.new(@name)

    unless @container.running?
        t1 = now
        @container.start
        t2 = now
        assert @container.running?
        puts "started in #{(t2-t1)/1000000}ms"
    end

    t3 = now
    pid = @container.attach ({
        :fork => true,
        :stdin => -1,
        # this actually would not work without namespace & NEWNS but let it be
        :flags => LXC::LXC_ATTACH_REMOUNT_PROC_SYS | LXC::LXC_ATTACH_DEFAULT
    })
    t4 = now

    puts "[#{prefix}] attached: #{pid} in #{(t4-t3)/1000000}ms"

    if pid > 0
      puts "[#{prefix}] waiting: #{pid}"

      # on lxc_fork parent looses parent-child relationship
      Process.waitpid(spawn("tail --pid=#{pid} -f /dev/null"))

      puts "[#{prefix}] done with child=#{pid}"

      t5 = now
      @container.stop
      t6 = now

      assert ! @container.running?

      puts "stopped in #{(t6-t5)/1000000}ms"
    elsif pid < 0
      @container.stop
      raise "error: attach failed"
    else
      puts "[#{prefix}] hello from container"
      puts "[#{prefix}] #{@@shared} is present"
      puts "/proc: #{Dir["/proc/**"]}"
      t = []
      t << Thread.new do
        i = 0
        puts "hello 1"
        until i > 5
          puts i
          sleep(0.2)
          i = i + 1
        end
      end
      t << Thread.new do
        i = 0
        puts "hello 2"
        until i > 5
          puts i
          sleep(0.2)
          i = i + 1
        end
      end
      until ! t.any? { |x| x.alive? }
        puts "+"
        sleep(1)
      end
      # can't call joins as they explicitly switch
      puts "[#{prefix}] exiting from"
    end

    puts "[#{prefix}] bye bye"
  end
end