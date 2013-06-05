class Videofile < ActiveRecord::Base
  belongs_to :camera
  
  def path
    "#{STORE_DIR}#{url}"
  end

  def url
    "#{camera.code}/#{started_at.strftime('%Y%m%d')}/#{started_at.strftime('%Y%m%d%H%m%s')}.mp4"
  end
end
