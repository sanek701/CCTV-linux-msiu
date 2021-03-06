class CamerasController < InheritedResources::Base
  def show
    @cam_info = VIDEO_SERVER.cam_info(resource.id)
  end

  def watch_archive

  end

  def timeline_entries
    camera = Camera.find(params[:id])
    date = Date.parse(params[:date]).to_time
    day_len = 24*60*60.0
    entries = (camera.events.where("started_at >= ? AND finished_at < ?", date, date+1.day).all +
    camera.videofiles.where("started_at >= ? AND (finished_at < ? OR finished_at IS NULL)", date, date+1.day).all).map do |entry|
      next if entry.started_at.day != date.day
      entry.finished_at = Time.now if entry.is_a?(Videofile) && entry.finished_at.nil?

      rel_margin = ((entry.started_at.to_i - date.to_i) / day_len).round(4)*100
      rel_width = ((entry.finished_at.to_i - entry.started_at.to_i) / day_len).round(4)*100
      rel_width = 0.1 if rel_width < 0.1

      {
        entry_type: entry.class.name.underscore,
        started_at: entry.started_at.to_i,
        finished_at: entry.finished_at.to_i,
        rel_margin: "#{rel_margin}%",
        rel_width: "#{rel_width}%"
      }
    end
    render json: { entries: entries.compact }
  end

  def seek
    camera = Camera.find(params[:id])
    date = Date.parse(params[:date]).to_time
    position = params[:position].to_f * 24 * 60 * 60
    screen_id = params[:screen_id].try(:to_i)
    date += position.seconds

    screen_id = VIDEO_SERVER.show_archive(camera.id, date, screen_id)
    render json: { rtsp: "rtsp://#{request.host}:8554/#{screen_id}", screen_id: screen_id }
  end

  def watch
    template = params[:template].to_i
    cameras = Camera.find(params[:ids].split(','))

    @screen_id = VIDEO_SERVER.show_real_image(template, cameras)
    @rtsp_link = "rtsp://#{request.host}:8554/#{@screen_id}"
  end

  def select
  end

  def archive
  end

  def sort
    cam1 = Camera.find(params[:camera_id])
    cam2 = Camera.find_by_position(params[:position])
    cam2.update_attribute(:position, cam1.position)
    cam1.update_attribute(:position, params[:position])
    render nothing: true
  end
end
