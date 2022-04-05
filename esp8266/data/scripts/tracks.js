// Tracks
// --------------------------------------------------------

function setTrack(trackNum, classname, text = "") {

    $('#track' + trackNum).attr("class", "track " + classname);
    $('#track' + trackNum).html(text)
  }
  
  function resetTracks(classname = "", start = 0) {
    for (i = start; i < 160; i++) {
      $('#track' + i).attr("class", classname == "" ? "track" : "track " + classname);
      $('#track' + i).html("");
    }
  }
  