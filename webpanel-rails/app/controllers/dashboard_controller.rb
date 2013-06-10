class DashboardController < ApplicationController
  def index
    stats = VIDEO_SERVER.stats
    @stats = {hdd_usage: (stats['free_space'].to_f / stats['tolal_space'] * 100).round, ncams: stats['ncams'] }
  end
end
