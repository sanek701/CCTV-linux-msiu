WebPanel::Application.routes.draw do
  get "dashboard/index"
  get "dashboard/select_cameras"

  resources :cameras do
    get :timeline_entries, on: :member
    get :seek, on: :member
    get :watch, on: :collection
  end
end
