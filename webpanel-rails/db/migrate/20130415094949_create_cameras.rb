class CreateCameras < ActiveRecord::Migration
  def change
    create_table :cameras do |t|
      t.string :code, :limit => 128
      t.string :about, :imit => 512
      t.string :url, :limit => 255
      t.integer :analize_frames
      t.integer :threshold
      t.integer :motion_delay
      t.integer :position

      t.timestamps
    end
  end
end
