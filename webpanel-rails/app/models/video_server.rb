require 'socket'

class VideoServer
  def initialize
    @s = nil
    connect
  end

  def show_archive(camera_id, timestamp, session_id=nil)
    result = send type: 'archive', cam_ids: [camera_id], timestamp: timestamp.to_i, session_id: session_id
    result['session_id']
  end

  def show_real_image(cameras)
    result = send type: 'real', cam_ids: cameras.map(&:id)
    result['session_id']
  end

  def stats
    send type: 'stats'
  end

  private

  def connect(reconnect = false)
    @s = UNIXSocket.new("/tmp/videoserver")
  rescue Exception => e
    raise(e) if reconnect
    puts "Can not connect to video server"
  end

  def send(h, retrying=false)
    @s.puts(h.to_json + "\n\n")
    JSON.parse(@s.gets)
  rescue Exception => e
    raise(e) if retrying
    connect(true)
    send(h, true)
  end
end
