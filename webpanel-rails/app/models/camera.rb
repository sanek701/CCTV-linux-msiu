class Camera < ActiveRecord::Base
  has_many :events
  has_many :videofiles

  after_create { |cam| cam.update_attribute(:position, cam.id-1) }
end
