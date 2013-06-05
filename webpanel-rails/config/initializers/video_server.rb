require 'socket'
begin
  VIDEO_SERVER = UNIXSocket.new("/tmp/videoserver")
rescue
  VIDEO_SERVER = nil
  puts "Can not connect to video server"
end
