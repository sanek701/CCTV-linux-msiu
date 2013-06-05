class Camera < ActiveRecord::Base
  has_many :events
  has_many :videofiles
end
