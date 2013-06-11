class Camera < ActiveRecord::Base
  has_many :events
  has_many :videofiles

  after_create { |cam| cam.update_attribute(:position, cam.id-1) }

  validates_presence_of :code, :url, :analize_frames, :threshold, :motion_delay
  validates_uniqueness_of :code
  validates_length_of :code, maximum: 128
  validates_length_of :about, maximum: 512
  validates_length_of :url, maximum: 255
  validates_format_of :code, with: /^[a-zA-Z0-9_]+$/, message: I18n.t('errors.code_format')
end
