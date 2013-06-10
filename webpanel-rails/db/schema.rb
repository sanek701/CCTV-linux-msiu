# encoding: UTF-8
# This file is auto-generated from the current state of the database. Instead
# of editing this file, please use the migrations feature of Active Record to
# incrementally modify your database, and then regenerate this schema definition.
#
# Note that this schema.rb definition is the authoritative source for your
# database schema. If you need to create the application database on another
# system, you should be using db:schema:load, not running all the migrations
# from scratch. The latter is a flawed and unsustainable approach (the more migrations
# you'll amass, the slower it'll run and the greater likelihood for issues).
#
# It's strongly recommended to check this file into your version control system.

ActiveRecord::Schema.define(:version => 20130517115712) do

  create_table "cameras", :force => true do |t|
    t.string   "code",           :limit => 128
    t.string   "about"
    t.string   "url"
    t.integer  "analize_frames"
    t.integer  "threshold"
    t.integer  "motion_delay"
    t.integer  "position"
    t.datetime "created_at",                    :null => false
    t.datetime "updated_at",                    :null => false
  end

  create_table "events", :force => true do |t|
    t.integer  "camera_id"
    t.datetime "started_at"
    t.datetime "finished_at"
  end

  add_index "events", ["camera_id"], :name => "index_events_on_camera_id"

  create_table "videofiles", :force => true do |t|
    t.integer  "camera_id"
    t.datetime "started_at"
    t.datetime "finished_at"
  end

  add_index "videofiles", ["camera_id"], :name => "index_videofiles_on_camera_id"

end
