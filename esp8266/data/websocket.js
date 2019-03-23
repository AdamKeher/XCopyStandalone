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
    setHardwareStatus(res[1]);
    if (res[1] == 0)
      setButtons(true);
    else
      setButtons(false);
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
  if (status == 0) {
    element.className = "idle";
    element.innerHTML = "Idle";
  }
  else if (status == 1) {
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

function setButtons(value) {
  if (value) {
    document.getElementById('copyADFtoDisk').classList.remove("disable");
    document.getElementById('copyDisktoADF').classList.remove("disable");
    document.getElementById('copyDisktoDisk').classList.remove("disable");
    document.getElementById('copyDisktoFlash').classList.remove("disable");
    document.getElementById('copyFlashtoDisk').classList.remove("disable");
    document.getElementById('testDisk').classList.remove("disable");
    document.getElementById('formatDisk').classList.remove("disable");
    document.getElementById('diskFlux').classList.remove("disable");
  }
  else {
    document.getElementById('copyADFtoDisk').classList.add("disable");
    document.getElementById('copyDisktoADF').classList.add("disable");
    document.getElementById('copyDisktoDisk').classList.add("disable");
    document.getElementById('copyDisktoFlash').classList.add("disable");
    document.getElementById('copyFlashtoDisk').classList.add("disable");
    document.getElementById('testDisk').classList.add("disable");
    document.getElementById('formatDisk').classList.add("disable");
    document.getElementById('diskFlux').classList.add("disable");
  }
}

function copyADFtoDisk() {
  connection.send("copyADFtoDisk");
}
function copyDisktoADF() {
  connection.send("copyDisktoADF");
}
function copyDisktoDisk() {
  connection.send("copyDisktoDisk");
}
function copyDisktoFlash() {
  connection.send("copyDisktoFlash");
}
function copyFlashtoDisk() {
  connection.send("copyFlashtoDisk");
}
function testDisk() {
  connection.send("testDisk");
}
function formatDisk() {
  connection.send("formatDisk");
}
function diskFlux() {
  connection.send("diskFlux");
}

