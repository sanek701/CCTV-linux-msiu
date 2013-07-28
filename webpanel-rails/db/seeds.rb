=begin
(100..124).each_with_index do |k, i|
  next if k == 109
  Camera.create!(code: "cam_#{i+1}", about: "Camera ##{i+1}", url: "rtsp://192.168.85.#{k}", analize_frames: 2, threshold: 50, motion_delay: 5)
end
=end

Camera.create!(code: 'cam_1', about: "First camera", url: 'rtsp://62.117.90.143/', analize_frames: 2, threshold: 50, motion_delay: 5)
Camera.create!(code: 'cam_2', about: "Second camera", url: 'rtsp://62.117.90.146/', analize_frames: 2, threshold: 50, motion_delay: 5)
