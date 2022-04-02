var connection = new WebSocket('ws://' + location.hostname + ':81/', ['arduino']);

connection.onopen = function () {
  element = document.getElementById('webSocketStatus');
  element.classList.add("socketOpen");
  element.classList.remove("socketClosed");
  element.classList.remove("socketError");
  element.innerHTML = "Open";

  connection.send('espCommand,busyPin');
};

connection.onclose = function () {
  element = document.getElementById('webSocketStatus');
  element.classList.add("socketClosed");
  element.classList.remove("socketOpen");
  element.classList.remove("socketError");
  element.innerHTML = "Closed";
}

connection.onerror = function (error) {
  console.log('WebSocket Error ', error);
  element = document.getElementById('webSocketStatus');
  element.classList.add("socketError");
  element.classList.remove("socketOpen");
  element.classList.remove("socketClosed");
  element.innerHTML = "Error";
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

  if (res[0] == "resetDisk") {
    setDiskname("");
    resetTracks("trackDefault", 0);
    clearFlux();
  }

  if (res[0] == "setDiskname") {
    setDiskname(res[1]);
  }

  if (res[0] == "setStatus") {
    setStatus(res[1]);
  }

  if (res[0] == "flux") {
    drawFlux(res[1], res[2]);
  }
};

connection.onclose = function () {
  console.log('WebSocket connection closed');
};

function componentToHex(c) {
  var hex = c.toString(16);
  return hex.length == 1 ? "0" + hex : hex;
}

function LerpRGB(a, b, t) {
  t = 1.0;

  ar = (a & 0xff0000) >> 16;
  ag = (a & 0x00ff00) >> 8;
  ab = (a & 0x0000ff);

  br = (b & 0xff0000) >> 16;
  bg = (b & 0x00ff00) >> 8;
  bb = (b & 0x0000ff);

  cr = ar + (br - ar) * t;
  cg = ag + (bg - ag) * t;
  cb = ab + (bb - ab) * t;

  result = "#" + componentToHex(cr) + componentToHex(cg) + componentToHex(cb)
  return result;
}

function clearFlux() {
  canvas = document.getElementById('fluxCanvas');
  ctx = canvas.getContext("2d");
  ctx.clearRect(0, 0, canvas.width, canvas.height);
}

function drawFlux(trackNum, fluxString) {
  fluxData = fluxString.split("|", 255);
  element = document.getElementById('fluxCanvas');
  ctx = element.getContext("2d");

  for (i = 1; i < fluxData.length; i++) {
    if (fluxData[i] > 0) {
      if (fluxData[i] < 5)
        ctx.fillStyle = LerpRGB(0x000000, 0xffff00, fluxData[i]);
      else if (fluxData[i] < 50)
        ctx.fillStyle = LerpRGB(0xffff00, 0xffa500, fluxData[i]);
      else
        ctx.fillStyle = LerpRGB(0xffa500, 0xff0000, fluxData[i]);

      ctx.fillRect(trackNum * 2, i, 2, 1);
    }
  }
}

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

  if (status == "Testing Disk") {
    document.getElementById('src_floppy').style.visibility = "visible";
    document.getElementById('src_sdcard').style.visibility = "hidden";
    document.getElementById('src_flash').style.visibility = "hidden";
    
    document.getElementById('dst_floppy').style.visibility = "hidden";
    document.getElementById('dst_sdcard').style.visibility = "hidden";
    document.getElementById('dst_flash').style.visibility = "hidden";
  } else {
    document.getElementById('src_floppy').style.visibility = "visible";
    document.getElementById('src_sdcard').style.visibility = "visible";
    document.getElementById('src_flash').style.visibility = "visible";
    
    document.getElementById('dst_floppy').style.visibility = "visible";
    document.getElementById('dst_sdcard').style.visibility = "visible";
    document.getElementById('dst_flash').style.visibility = "visible";
  }
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

