// Web Sockets
// --------------------------------------------------------
var connection = new ReconnectingWebSocket('ws://' + location.hostname + ':81/', ['arduino'], {debug: true, reconnectInterval: 3000, timeoutInterval: 5000, automaticOpen: true });
var connectionState = false; 
var hardwareStatus = true;

setupWebsocket();

function ping() {
  if (!fileTransferInProgress && connectionState && hardwareStatus) {
    connection.send('ping');
    timer = setTimeout(function () {
      console.log('WebSocket Timeout');
      if (!fileTransferInProgress) { setWebsocketStatus("closed"); }
      connectionState = false;
    }, 3000);
  }
}

function sendKey(key) {
  if (!fileTransferInProgress && connectionState) {
    key = key.replaceAll('\r', '\033[^M');
    key = key.replaceAll('\n', '\033[^J');
    connection.send('k,' + key);
  }
}

function setHardwareStatus(status) {
  hardwareStatus = status == 0 ? true : false;
  if (status == 0) {
    $('#hardwareStatus').removeClass('alert-danger').addClass('alert-success').html('Device Idle');
    disableInterface(false);
  }
  else if (status == 1) {
    $('#hardwareStatus').removeClass('alert-success').addClass('alert-danger').html('Device Busy');
    disableInterface(true);
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

function onWebSocketMessage(msg) {
  message = msg.data;

  // if pong reset counters
  if (message == 'pong') {
    clearTimeout(timer);
    return;
  }

  // replace escaped characters
  message = message.replaceAll('\033[^M', '\r');
  message = message.replaceAll('\033[^J', '\n');

  // debug log
  console.log('Server: ', message);

  // split message
  var res = message.split(",", 12);

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
    $('#mode').html(res[1]);
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
      case '18':
        setState('copyDiskToFlash');
        break;
      case '19':
        setState('copyFlashToDisk');
    }
  }

  // don't write res[1] as it may contain commas itself and have been split
  if (res[0] == "log") {
    term.write(message.substring(4));
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

  if (res[0] == "download") {
    if (res[1] == "start") {
      fileTransferInProgress = true
    }
    if (res[1] == "end") {
      fileTransferInProgress = false;
    }
  }

  if (res[0] == "setTab") {
    setTab(res[1]);
  }

  if (res[0] == "sendBlock") {
    drawSector(res[1], res[2], res[3]);
  }

  if (res[0] == "sendBlockDetails") {
    drawSectorStats(res[1], res[2], res[3], res[4], res[5], res[6], res[7], res[8], res[9]);
  }

  if (res[0] == "sendBlockHist") {
    drawSectorHist(res[1]);
  }

  if (res[0] == "resetEmptyBlocks") {
    resetEmptyBlocks();
  }

  if (res[0] == "setEmptyBlock") {
    setEmptyBlock(res[1], res[2], res[3], res[4] == 'true' ? true : false);
  }

  if (res[0] == "highlightEmptyBlock") {
    highlightEmptyBlock(res[1], res[2], res[3], res[4], res[5] == 'true' ? true : false);
  }

  if (res[0] == "clearHighlightedBlocks") {
    clearHighlightedBlocks();
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
  
  connection.onmessage = onWebSocketMessage;
}
