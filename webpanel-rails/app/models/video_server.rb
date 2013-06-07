require 'socket'

class VideoServer
  def initialize
    @s = nil
    connect
  end

  def show_real_image(cameras)
    result = send type: 'real', cam_ids: cameras.map(&:id)
    p result
    session_id = result['session_id']
    "rtsp://localhost:8554/stream_#{session_id}"
  end

  private

  def connect
    @s = UNIXSocket.new("/tmp/videoserver")
  rescue
    puts "Can not connect to video server"
  end

  def send(h)
    if @s
      @s.puts(h.to_json)
      JSON.parse(@s.gets)
    end
  end
end
