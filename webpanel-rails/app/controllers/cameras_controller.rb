class CamerasController < InheritedResources::Base
  def watch_archive

  end

  def timeline_entries
    camera = Camera.find(params[:id])
    date = Date.parse(params[:date]).to_time
    day_len = 24*60*60.0
    entries = (camera.events.where("started_at >= ? AND finished_at < ?", date, date+1.day).all +
    camera.videofiles.where("started_at >= ? AND (finished_at < ? OR finished_at IS NULL)", date, date+1.day).all).map do |entry|
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
    render json: { entries: entries }
  end

  def seek
    camera = Camera.find(params[:id])
    date = Date.parse(params[:date]).to_time
    position = params[:position].to_f * 24 * 60 * 60
    session_id = params[:session_id].try(:to_i)
    date += position.seconds

    VIDEO_SERVER
  end

  def watch
    template = params[:template]
    cameras = Camera.find(params[:ids].split(','))

    @rtsp_link = VIDEO_SERVER.show_real_image(cameras)
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
