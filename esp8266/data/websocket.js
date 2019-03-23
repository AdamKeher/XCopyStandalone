var connection = new WebSocket('ws://' + location.hostname + ':81/', ['arduino']);

connection.onopen = function () {
  connection.send('Connect ' + new Date());
};

connection.onerror = function (error) {
  console.log('WebSocket Error ', error);
};

connection.onmessage = function (e) {
  console.log('Server: ', e.data);

  var res = e.data.split(",", 5);

  if (res[0] == "pinStatus") {
    setHardwareStatus(res[1])
  }

  if (res[0] == "setTrack") {
    setTrack(res[1], res[2], res[3]);
  }

  if (res[0] == "resetTracks") {
    resetTracks(res[1], res[2]);
  }

  if (res[0] == "setDiskname") {
    setDiskname(res[1]);
  }

  if (res[0] == "setStatus") {
    setStatus(res[1]);
  }
};

connection.onclose = function () {
  console.log('WebSocket connection closed');
};

function setHardwareStatus(status) {
  element = document.getElementById('hardwareStatus');
  if (status == 0)
  {
    element.className = "idle";
    element.innerHTML = "Idle";
  }
  else if (status == 1)
  {
    element.className = "busy";
    element.innerHTML = "Busy";
  }
}

function setTrack(trackNum, classname, text = "") {
  element = document.getElementById('track' + trackNum);
  element.className = classname;
  if (text != "")
    element.innerHTML = text;
}

function setDiskname(diskname) {
  element = document.getElementById('diskname');
  element.innerHTML = diskname;
}

function setStatus(status) {
  element = document.getElementById('status');
  element.innerHTML = status;
}

function resetTracks(classname = "trackDefault", start = 0) {
  for (i = start; i < 160; i++) {
    setTrack(i, classname)
  }
}

function sendChime() {
  connection.send("chime");
}