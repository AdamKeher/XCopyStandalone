// Web Sockets
// --------------------------------------------------------
var connection = new ReconnectingWebSocket('ws://' + location.hostname + ':81/', ['arduino'], {debug: true, reconnectInterval: 3000, timeoutInterval: 5000, automaticOpen: true });
var connectionState = false; 

setupWebsocket();

function ping() {
  if (!uploadInProgress && connectionState) {
    connection.send('ping');
    tm = setTimeout(function () {
      console.log('WebSocket Timeout');
      setWebsocketStatus("closed");
      connectionState = false;
    }, 1000);
  }
}

function webSocketRefresh() {
  try {
    connection.refresh();
  } catch (error) { }
}

function setWebsocketStatus(status) {
  $('#websocketStatus')
    .removeClass('alert-danger')
    .removeClass('alert-warning')
    .removeClass('alert-success')

  if (status == 'open') {
    $('#websocketStatus')
      .addClass('alert-success')
      .html('Websocket Open');
    $('#websocketModal').modal('hide');
    connectionState = true;
  }
  else if (status == 'closed') {
    $('#websocketStatus')
      .addClass('alert-warning')
      .html('Websocket Closed');
    $('#websocketModal').modal('show');
    connectionState = false;
  }
  else {
    $('#websocketStatus')
      .addClass('alert-danger')
      .html('Websocket Error');
    $('#websocketModal').modal('show');
    connectionState = false;
  }
}

function setupWebsocket() {
  connection.onopen = function () {
    setWebsocketStatus("open");
    setInterval(ping, 5000);
    connection.send('espCommand,busyPin');
    console.log('WebSocket Open');
  };
  
  connection.onclose = function () {
    // setWebsocketStatus("closed");
    console.log('WebSocket Closed');
  };
  
  connection.onerror = function (error) {
    setWebsocketStatus("error");
    console.log('WebSocket Error ', error);
  };
  
  connection.onmessage = function (e) {
    message = e.data;
  
    // if pong reset counters
    if (message == 'pong') {
      clearTimeout(tm);
      return;
    }
  
    // replace escaped characters
    message = message.replaceAll('\033[^M', '\r');
    message = message.replaceAll('\033[^J', '\n');
  
    // debug log
    console.log('Server: ', message);
  
    // split message
    var res = message.split(",", 5);
  
    if (res[0] == "pinStatus") {
      setHardwareStatus(res[1]);
    }
  
    if (res[0] == "setTrack") {
      setTrack(res[1], res[2], res[3]);
    }
  
    if (res[0] == "resetTracks") {
      resetTracks(res[1], res[2]);
    }
  
    if (res[0] == "resetDisk") {
      setDiskname("");
      resetTracks("track", 0);
      clearFlux();
    }
  
    if (res[0] == "setDiskname") {
      setDiskname(res[1]);
    }
  
    if (res[0] == "setStatus") {
      setStatus(res[1]);
    }
  
    if (res[0] == "setMode") {
      setMode(res[1]);
    }
  
    if (res[0] == "flux") {
      drawFlux(res[1], res[2]);
    }
  
    if (res[0] == "setState") {
      switch (res[1]) {
        case '3':
          setState('copyDiskToADF');
          break;
        case '5':
          setState('copyADFToDisk');
          break;
        case '13':
          setState('copyDiskToDisk');
          break;
        case '24':
          setState('fluxDisk');
          break;
        case '25':
          setState('formatDisk');
          break;
        case '4':
          setState('testDisk');
          break;
      }
    }
  
    if (res[0] == "log") {
      term.write(res[1]);
      // log(res[1]);
    }
  
    if (res[0] == "clearSdFiles") {
      clearSdFiles();
    }
  
    if (res[0] == "addSdFile") {
      addSdFile(res[1]);
    }
  
    if (res[0] == "drawSdFiles") {
      drawSdFiles();
    }

    if (res[0] == "cancelUpload") {
      fileUploadCancel(res[1]);
    }
  };
}
