// Web Sockets
// --------------------------------------------------------
//
// The connection is a small explicit state machine. The rule that matters: a
// disconnected state is only ever entered from a real socket event. The old
// code let a heartbeat timeout declare the link dead while the socket was still
// open, and because the heartbeat was itself gated on that same flag it could
// never send another ping, never see another pong and never recover -- the
// modal stayed up while the page kept updating behind it. Anything that now
// decides the link is dead closes the socket and lets onclose drive the state.

var WS_URL = 'ws://' + location.hostname + ':81/';
var WS_PROTOCOL = 'arduino';

var WS_PING_INTERVAL = 5000;    // how often we probe a quiet link
var WS_RX_TIMEOUT = 12000;      // silence after which the socket is considered dead
var WS_CONNECT_TIMEOUT = 6000;  // how long a socket may sit in CONNECTING
var WS_BACKOFF_BASE = 500;      // first retry delay
var WS_BACKOFF_MAX = 30000;     // ceiling on the retry delay
var WS_BANNER_DELAY = 5000;     // don't nag about a blip shorter than this
var WS_BANNER_OK_TIME = 2000;   // how long "Reconnected" stays up

// 'connecting' | 'open' | 'reconnecting' | 'offline'
var wsState = 'connecting';
var connection = null;          // the live WebSocket, or null while backing off
var hardwareStatus = true;      // device idle -- busy/idle pill and the resync gate

var wsAttempt = 0;              // consecutive failed attempts, drives the backoff
var wsHasConnected = false;     // has this page ever had a working session?
var wsLastRxAt = 0;             // last inbound frame, of any kind
var wsLastPingAt = 0;
var wsHeartbeatTimer = null;    // created once, never per connection
var wsRetryTimer = null;
var wsConnectTimer = null;
var wsBannerTimer = null;

// Connection
// --------------------------------------------------------

function wsConnect() {
  wsClearTimer('wsRetryTimer');

  // One heartbeat for the life of the page. The old code created this inside
  // onopen, so every reconnect stacked another interval on top of the last.
  if (wsHeartbeatTimer === null) {
    wsHeartbeatTimer = setInterval(wsHeartbeat, 2000);
  }

  if (connection !== null) {
    // Drop the handlers before closing, so a dying socket cannot schedule a
    // retry that races the connection we are about to make.
    wsDetach(connection);
    try { connection.close(); } catch (error) { }
    connection = null;
  }

  if (!navigator.onLine) {
    wsSetState('offline');
    return;
  }

  wsSetState(wsHasConnected ? 'reconnecting' : 'connecting');

  try {
    connection = new WebSocket(WS_URL, [WS_PROTOCOL]);
  } catch (error) {
    console.log('WebSocket: construction failed', error);
    wsScheduleReconnect();
    return;
  }

  var socket = connection;
  socket.onopen = function () { wsOnOpen(socket); };
  socket.onclose = function (event) { wsOnClose(socket, event); };
  socket.onerror = function (error) { console.log('WebSocket: error', error); };
  socket.onmessage = function (msg) { wsOnMessage(socket, msg); };

  // A host that accepts the TCP connection and then says nothing leaves the
  // browser in CONNECTING indefinitely, so time it out here.
  wsClearTimer('wsConnectTimer');
  wsConnectTimer = setTimeout(function () {
    wsConnectTimer = null;
    if (socket.readyState === WebSocket.CONNECTING) {
      console.log('WebSocket: connect timed out');
      try { socket.close(); } catch (error) { }
    }
  }, WS_CONNECT_TIMEOUT);
}

function wsDetach(socket) {
  socket.onopen = null;
  socket.onclose = null;
  socket.onerror = null;
  socket.onmessage = null;
}

function wsOnOpen(socket) {
  if (socket !== connection) return;
  wsClearTimer('wsConnectTimer');

  var reconnected = wsHasConnected;
  wsHasConnected = true;
  wsLastRxAt = Date.now();
  wsLastPingAt = 0;
  wsSetState('open');
  console.log('WebSocket: open');

  wsResync(reconnected);
}

function wsOnClose(socket, event) {
  if (socket !== connection) return;
  wsClearTimer('wsConnectTimer');
  console.log('WebSocket: closed (' + (event ? event.code : '?') + ')');
  connection = null;
  wsScheduleReconnect();
}

function wsOnMessage(socket, msg) {
  if (socket !== connection) return;

  // Any inbound frame proves the link is alive, not just a pong. During a copy
  // the Teensy floods setTrack messages, so a busy link needs no pings at all --
  // which is exactly the case the old hardwareStatus gate switched off.
  wsLastRxAt = Date.now();

  // Only a session that actually carried traffic clears the backoff. Resetting
  // on open alone would spin at the base delay against a server that accepts a
  // connection and immediately drops it.
  wsAttempt = 0;

  onWebSocketMessage(msg);
}

function wsScheduleReconnect() {
  if (wsRetryTimer !== null) return;

  if (!navigator.onLine) {
    wsSetState('offline');
    return;
  }

  var delay = Math.min(WS_BACKOFF_MAX, WS_BACKOFF_BASE * Math.pow(2, wsAttempt));
  delay = Math.round(delay * (0.75 + Math.random() * 0.5));
  wsAttempt++;

  wsSetState('reconnecting');

  console.log('WebSocket: reconnecting in ' + delay + 'ms (attempt ' + wsAttempt + ')');
  wsRetryTimer = setTimeout(function () {
    wsRetryTimer = null;
    wsConnect();
  }, delay);
}

// Reconnect now, without waiting out the backoff. Bound to the banner button,
// and to coming back online or to the foreground.
function wsRetryNow() {
  wsAttempt = 0;
  wsClearTimer('wsRetryTimer');
  wsConnect();
}

function wsHeartbeat() {
  if (wsState !== 'open' || connection === null) return;

  // The upload is a separate HTTP POST that saturates the ESP. A false trip
  // there would be worse than not checking at all.
  if (fileTransferInProgress) {
    wsLastRxAt = Date.now();
    return;
  }

  var now = Date.now();

  if (now - wsLastRxAt > WS_RX_TIMEOUT) {
    console.warn('WebSocket: no traffic for ' + (now - wsLastRxAt) + 'ms, closing');
    // Close the real socket rather than fake a state. onclose does the rest.
    try { connection.close(); } catch (error) { }
    return;
  }

  // Probe silence only. A link carrying setTrack messages has already proved
  // itself, and the ESP has better things to do mid-copy than answer pings.
  if (now - wsLastRxAt > WS_PING_INTERVAL && now - wsLastPingAt > WS_PING_INTERVAL) {
    wsLastPingAt = now;
    wsSend('ping');
  }
}

function wsClearTimer(name) {
  if (window[name] !== null && window[name] !== undefined) {
    clearTimeout(window[name]);
    window[name] = null;
  }
}

// Sending
// --------------------------------------------------------

// Commands are user intent. Queueing them across an outage and replaying a
// stale writeADFFile minutes later would be worse than dropping them, so
// nothing is buffered -- the caller is told, and the banner says so.
function wsSend(message) {
  if (wsState !== 'open' || connection === null || connection.readyState !== WebSocket.OPEN) {
    console.log('WebSocket: not connected, dropped "' + message + '"');
    wsFlashBanner('Not connected &ndash; command was not sent');
    return false;
  }

  try {
    connection.send(message);
    return true;
  } catch (error) {
    console.log('WebSocket: send failed', error);
    return false;
  }
}

function sendKey(key) {
  // Keystrokes are noise when the link is down -- drop them without the banner.
  if (wsState !== 'open') return;
  key = key.replaceAll('\r', '\033[^M');
  key = key.replaceAll('\n', '\033[^J');
  wsSend('k,' + key);
}

// Re-sync what a newly connected client can actually recover
// --------------------------------------------------------

function wsResync(reconnected) {
  wsSend('espCommand,busyPin');

  // The listing costs the Teensy an SD scan, so ask only when it is idle and
  // only if the user has actually been browsing files.
  if (typeof sdFetched !== 'undefined' && sdFetched && hardwareStatus) {
    wsSend('getSdFiles,' + sdPath);
  }

  // The Teensy keeps no shadow copy of the web track grid, so tracks that
  // completed while we were away are gone for good. Say so, rather than leave a
  // stale grid looking authoritative.
  if (reconnected && typeof term !== 'undefined' && term) {
    term.write('\r\n\x1b[33m*** reconnected - display may be out of date ***\x1b[0m\r\n');
  }
}

// Status indicators
// --------------------------------------------------------

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

/*
   Signal strength of the network the device is on.

   Sent by the ESP rather than by the Teensy, which never sees it: the radio is
   the only thing that knows its own RSSI, and the wording and the thresholds
   come with the message so the pill and the console never disagree.

   wifi,<rssi>,<bars>,<quality>,<ssid>, with bars 1..4 and 0 for no network.
*/
function setWifiStatus(rssi, bars, quality, ssid) {
  var pill = $('#wifiStatus').removeClass('alert-success alert-warning alert-danger alert-secondary');
  bars = parseInt(bars, 10);

  if (!bars) {
    pill.addClass('alert-secondary').attr('title', 'The device has not joined a wireless network');
    wifiPillText(pill, 'No network');
    return;
  }

  pill.addClass(bars >= 3 ? 'alert-success' : (bars == 2 ? 'alert-warning' : 'alert-danger'));
  pill.attr('title', 'Signal ' + quality + ' on ' + ssid);
  wifiPillText(pill, ssid + '   ' + rssi + ' dBm');
}

// Nothing about the network is known while the link to the device is down, and a
// stale "excellent" would be the most misleading thing the footer could say.
function wifiUnknown() {
  var pill = $('#wifiStatus').removeClass('alert-success alert-warning alert-danger').addClass('alert-secondary');
  pill.attr('title', 'Not known while the link to the device is down');
  wifiPillText(pill, '\u2014');
}

// A text node, not html(): the SSID is somebody else's string and arrives
// unescaped.
function wifiPillText(pill, text) {
  pill.empty()
      .append($('<i>').addClass('fa-solid fa-wifi'))
      .append(document.createTextNode(' ' + text));
}

// The only writer of wsState.
function wsSetState(next) {
  if (wsState === next) {
    // The attempt count keeps moving while reconnecting, so keep the text fresh.
    if (next === 'reconnecting') wsUpdateBanner();
    return;
  }

  wsState = next;
  wsUpdatePill();
  wsUpdateBanner();
}

function wsUpdatePill() {
  var pill = $('#websocketStatus').removeClass('alert-success alert-warning alert-danger');

  if (wsState !== 'open') wifiUnknown();

  if (wsState === 'open') {
    pill.addClass('alert-success').html('Connected');
  }
  else if (wsState === 'offline') {
    pill.addClass('alert-danger').html('Offline');
  }
  else if (wsState === 'connecting') {
    pill.addClass('alert-warning').html('Connecting&hellip;');
  }
  else {
    pill.addClass('alert-warning').html('Reconnecting&hellip;');
  }
}

function wsUpdateBanner() {
  var banner = document.getElementById('connectionBanner');
  if (!banner) return;

  if (wsState === 'open') {
    wsClearTimer('wsBannerTimer');
    if (banner.hidden) return;

    // Only worth confirming to someone who was shown the problem to begin with.
    wsShowBanner('Reconnected', 'ok');
    wsBannerTimer = setTimeout(function () {
      wsBannerTimer = null;
      wsHideBanner();
    }, WS_BANNER_OK_TIME);
    return;
  }

  if (!banner.hidden) {
    wsShowBanner(wsBannerText(), wsState === 'offline' ? 'offline' : '');
    return;
  }

  // Not up yet: arm it, so a blip shorter than WS_BANNER_DELAY is never seen.
  if (wsBannerTimer === null) {
    wsBannerTimer = setTimeout(function () {
      wsBannerTimer = null;
      if (wsState === 'open') return;
      wsShowBanner(wsBannerText(), wsState === 'offline' ? 'offline' : '');
    }, WS_BANNER_DELAY);
  }
}

function wsBannerText() {
  if (wsState === 'offline') return 'Your browser is offline';
  if (wsState === 'connecting') return 'Connecting to XCopy&hellip;';
  return 'Reconnecting to XCopy&hellip;' + (wsAttempt > 1 ? ' (attempt ' + wsAttempt + ')' : '');
}

function wsShowBanner(html, variant) {
  var banner = document.getElementById('connectionBanner');
  if (!banner) return;
  banner.className = 'connection-banner' + (variant ? ' ' + variant : '');
  document.getElementById('connectionBannerText').innerHTML = html;
  banner.hidden = false;
}

function wsHideBanner() {
  var banner = document.getElementById('connectionBanner');
  if (banner) banner.hidden = true;
}

// A transient message on the banner that does not change the connection state.
function wsFlashBanner(html) {
  if (wsState !== 'open') return;   // the real status is already on screen
  wsClearTimer('wsBannerTimer');
  wsShowBanner(html, 'offline');
  wsBannerTimer = setTimeout(function () {
    wsBannerTimer = null;
    wsHideBanner();
  }, WS_BANNER_OK_TIME);
}

// Wake-ups the backoff timer alone would miss
// --------------------------------------------------------

window.addEventListener('online', function () {
  console.log('WebSocket: browser back online');
  wsRetryNow();
});

window.addEventListener('offline', function () {
  console.log('WebSocket: browser went offline');
  wsSetState('offline');
});

document.addEventListener('visibilitychange', function () {
  // Background tabs have their timers throttled to roughly once a minute, so a
  // backgrounded page can sit disconnected long after the device is back.
  if (document.visibilityState === 'visible' && wsState !== 'open' && wsRetryTimer !== null) {
    wsRetryNow();
  }
});

// Message dispatch
// --------------------------------------------------------

function onWebSocketMessage(msg) {
  message = msg.data;

  // Liveness is stamped in onmessage for every inbound frame, so a pong carries
  // nothing beyond the fact that it arrived.
  if (message == 'pong') { return; }

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

  if (res[0] == "headCal") {
    headCalResult(res[1], res[2], res[3], res[4], res[5], res[6], res[7]);
  }

  if (res[0] == "headCalConfig") {
    headCalConfig(res[1], res[2], res[3], res[4], res[5], res[6], res[7], res[8], res[9]);
  }

  if (res[0] == "dtState") {
    dtState(res[1], res[2], res[3], res[4], res[5], res[6], res[7], res[8]);
  }

  if (res[0] == "setState") {
    switch (res[1]) {
      case '3':
        setState('copyDiskToADF');
        break;
      case '42':
        setState('copyDiskToSCP');
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
        break;
      // setTab rather than setState: the calibration panel has no button for
      // diskcopy.js to highlight and no diskname for it to hide, so the browser
      // simply follows the device onto the tab.
      case '44':
        setTab('headcal');
        break;
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

  if (res[0] == "wifi") {
    // The SSID is the whole of the tail rather than a field, since it is the one
    // thing in this message that can contain a comma of its own.
    var head = 'wifi,' + res[1] + ',' + res[2] + ',' + res[3] + ',';
    setWifiStatus(res[1], res[2], res[3], message.substring(head.length));
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

  /*
     Disk Info.

     These re-split rather than using res[]: the shared split above stops at 12
     fields and dinfoTrack carries fourteen, so flags and the sector list would be
     dropped on the floor. Raising the shared limit would change how every other
     message is parsed, which is not worth it for one family.
  */
  if (res[0] == "dinfoBegin") {
    // The name is last and may itself contain commas, so it is taken as the tail
    // of the line the way "log" and "wifi" are, not as a split field.
    var b = message.split(",", 8);
    var head = b.slice(0, 8).join(",") + ",";
    dinfoBegin(b[1], b[2], b[3], b[4], b[5], b[6], b[7], message.substring(head.length));
  }

  if (res[0] == "dinfoTrack") {
    var t = message.split(",", 14);
    dinfoTrack(t[1], t[2], t[3], t[4], t[5], t[6], t[7], t[8], t[9], t[10], t[11], t[12], t[13]);
  }

  if (res[0] == "dinfoProfile") {
    dinfoProfile(res[1], res[2], res[3]);
  }

  if (res[0] == "dinfoHist") {
    dinfoHist(res[1], res[2], res[3]);
  }

  if (res[0] == "dinfoEnd") {
    dinfoEnd(res[1], res[2], res[3]);
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
