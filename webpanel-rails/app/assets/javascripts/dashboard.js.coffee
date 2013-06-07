jQuery ->
  $('.camera-btn').click ->
    cam = $(this).closest('.camera')
    if cam.hasClass('selected')
      cam.removeClass('selected')
    else
      cam.addClass('selected')
    
    false

  $('.watch').click ->
    cam_ids = $.map $('.camera.selected'), (obj)->
      return $(obj).find('.camera-btn').data('camera-id')
    window.location.href = $('.watch').data('url') + '?ids=' + cam_ids.join(',')
    false

  $('.templates .btn').click ->
    $('.templates .btn').removeClass('btn-success')
    $(this).addClass('btn-success')
    false
