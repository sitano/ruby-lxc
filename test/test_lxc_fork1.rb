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

    @container.start
    assert @container.running?
    puts "[#{prefix}] started"

    pid = @container.attach do
      puts "[#{prefix}] hello from container"
      puts "[#{prefix}] #{@@shared} is present"
      fork do
        puts "[#{prefix}] and fork"
      end
      sleep(1)
      puts "[#{prefix}] exiting from"
    end

    puts "[#{prefix}] done with child=#{pid}"

    Process.waitpid(pid)

    @container.stop
    assert ! @container.running?
    puts "stopped"
  end
end
