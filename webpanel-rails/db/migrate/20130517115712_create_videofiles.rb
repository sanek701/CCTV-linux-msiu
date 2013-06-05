class CreateVideofiles < ActiveRecord::Migration
  def change
    create_table :videofiles do |t|
      t.references :camera
      t.datetime :started_at
      t.datetime :finished_at
    end
    add_index :videofiles, :camera_id
  end
end
