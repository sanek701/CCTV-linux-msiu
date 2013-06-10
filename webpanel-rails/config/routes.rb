WebPanel::Application.routes.draw do
  resources :cameras do
    get :timeline_entries, on: :member
    get :watch_archive, on: :member
    get :seek, on: :member
    get :select, on: :collection
    get :watch, on: :collection
    get :archive, on: :collection
    post :sort, on: :collection
  end

  root to: "dashboard#index"
end
