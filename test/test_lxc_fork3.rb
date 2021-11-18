$:.unshift File.expand_path(File.join(File.dirname(__FILE__), '../lib'))

require 'test/unit'
require 'lxc'

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

  def test_attach
    @name = 'test'
    @container = LXC::Container.new(@name)
    assert ! @container.running?

    for i in 1..2 do
      @container.start
      assert @container.running?
      puts "started"

      pid = @container.attach

      puts "[#{prefix}] attached: #{pid}"

      if pid > 0
        puts "[#{prefix}] waiting: #{pid}"

        Process.waitpid(pid)

        puts "[#{prefix}] done with child=#{pid}"

        @container.stop
        assert ! @container.running?
        puts "stopped"
      else
        puts "[#{prefix}] hello from container"
        puts "[#{prefix}] #{@@shared} is present"
        fork do
          puts "[#{prefix}] and fork"
        end
        sleep(1)
        puts "[#{prefix}] exiting from"
        Process.exit(0)
      end
    end
  end
end
