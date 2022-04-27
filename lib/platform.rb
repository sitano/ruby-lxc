module Platform

  def linux?
    require "rbconfig"
    host_os = RbConfig::CONFIG['host_os'].downcase
    not host_os =~ /linux/
  end

  def ext_path
    return "ext/lxc" if linux?
    return "ext/lxc_stub"
  end

end
