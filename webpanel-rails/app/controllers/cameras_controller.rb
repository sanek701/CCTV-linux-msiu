class CamerasController < InheritedResources::Base
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
    position = params[:position].to_f * 24 * 60 * 60 / 100
    date += position.seconds

    if date > Time.now
      render json: { url: camera.url }
      return
    end

    vf = camera.videofiles.where("started_at <= ? AND (finished_at <= ? OR finished_at IS NULL)", date, date).first
    render json: { url: vf.url, position: ((date - vf.started_at) * 1000).to_i }
  end

  def watch
    template = params[:template]
    cameras = Camera.find(params[:ids].split(','))

    @rtsp_link = VIDEO_SERVER.show_real_image(cameras)
  end
end
