class CreateEvents < ActiveRecord::Migration
  def change
    create_table :events do |t|
      t.references :camera
      t.datetime :started_at
      t.datetime :finished_at
    end
    add_index :events, :camera_id
  end
end
