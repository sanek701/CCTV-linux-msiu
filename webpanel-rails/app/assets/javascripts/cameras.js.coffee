jQuery ->
  $session_id = undefined

  check_cam_cnt = ->
    if $('.camera.selected').length > 0
      $('.watch').removeClass('disabled')
    else
      $('.watch').addClass('disabled')

  _02d = (n)->
    if n < 10
      '0' + n
    else
      n.toString()

  $('.cameras.selectable .camera-btn').click ->
    cam = $(this).closest('.camera')
    if cam.hasClass('selected')
      cam.removeClass('selected')
    else
      cam.addClass('selected')
    check_cam_cnt()
    false

  $('.watch').click ->
    cam_ids = $.map $('.camera.selected'), (obj)->
      return $(obj).data('camera-id')
    if cam_ids.length > 0
      window.location.href = $('.watch').data('url') + '?ids=' + cam_ids.join(',') + '&template=' + $('.templates .btn.btn-success').text()
    false

  $('.templates .btn').click ->
    $('.templates .btn').removeClass('btn-success')
    $(this).addClass('btn-success')
    false

  $('.toggle-all').click ->
    if $('.camera.selected').length > 0
      $('.camera.selected').removeClass('selected')
    else
      $('.camera').addClass('selected')
    check_cam_cnt()

  $('.cameras.sortable').sortable({
    helper: 'clone',
    update: (event, ui)->
      $.post($('.cameras').data('sort-url'), { camera_id: ui.item.data('camera-id'), position: ui.item.index() })
  });

  $('.timeline').on 'mousemove', (e) ->
    top = $(this).position().top
    posX = $(this).parent().position().left
    percentage = $(this).parent().scrollLeft() / $(this).width() + (e.pageX - posX) / $(this).width()
    $(this).data('position', percentage)
    time = Math.round(24*60*60*percentage)
    $('.time').text(_02d(Math.floor(time/60/60)) + ':' + _02d(Math.floor(time/60%60)) + ':' + _02d(Math.floor(time%60)))
    left = $(this).width() * percentage - $('.time').width() / 2
    $('.time').css({left: left}).show()
    $('.pointer').css({left: $(this).width() * percentage}).show()

  $('.timeline').on 'mouseleave', ->
    $('.time, .pointer').hide()

  $('.timeline').click (e)->
    that = $(this)
    $.ajax {
      url: that.data('seek-url'),
      type: "GET",
      dataType: 'json',
      data: { date: that.data('date'), position: that.data('position'), session_id: $session_id },
      success: (data)->
        if data.url
          id = @vlc.playlist.add(data.url, "");
          @vlc.playlist.playItem(id);
        if data.session_id
          $session_id = data.session_id;
    }

  $('#zoom-in').on 'mousedown', ->
    width = $('.timeline').data('width')
    @timeout = setInterval ->
      width += 5
      $('.timeline').data('width', width)
      $('.timeline').css('width', "#{width}%")
    , 200
    false

  $('#zoom-out').on 'mousedown', ->
    width = $('.timeline').data('width')
    @timeout = setInterval ->
      if width-5 >= 100
        width -= 5
        $('.timeline').data('width', width)
        $('.timeline').css('width', "#{width}%")
    , 200
    false

  $('#zoom-in, #zoom-out').on 'mouseup mouseout', ->
    clearInterval(@timeout)
    false
