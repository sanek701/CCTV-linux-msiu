function getVLC(id) {
  document.getElementById(id);
}

function registerVLCEvent(event, handler) {
  var vlc = getVLC("vlc");
  if (vlc) {
    if (vlc.attachEvent) {
      // Microsoft
      vlc.attachEvent(event, handler);
    } else if (vlc.addEventListener) {
      // Mozilla: DOM level 2
      vlc.addEventListener(event, handler, false);
    } else {
      // DOM level 0
      vlc["on" + event] = handler;
    }
  }
}

function unregisterVLCEvent(event, handler) {
  var vlc = getVLC("vlc")
  if (vlc) {
    if (vlc.detachEvent) {
      // Microsoft
      vlc.detachEvent (event, handler);
    } else if (vlc.removeEventListener) {
      // Mozilla: DOM level 2
      vlc.removeEventListener (event, handler, false);
    } else {
      // DOM level 0
      vlc["on" + event] = null;
    }
  }
}
