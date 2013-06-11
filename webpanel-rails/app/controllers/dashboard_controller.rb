class DashboardController < ApplicationController
  def index
    stats = VIDEO_SERVER.stats
    hdd_usage = 100 - (stats['free_space'].to_f / stats['tolal_space'] * 100).round
    hdd_usage_class = 'warning' if hdd_usage > 50
    hdd_usage_class = 'danger' if hdd_usage > 80
    @stats = {hdd_usage: hdd_usage, hdd_usage_class: hdd_usage_class, ncams: stats['ncams'] }
  end
end
