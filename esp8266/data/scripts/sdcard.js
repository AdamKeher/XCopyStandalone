var sdFiles = [];
var sdPath = "/"
// Set once the user has actually browsed the card, so a reconnect only re-runs
// the Teensy SD scan for someone who is looking at a listing.
var sdFetched = false;

// Which kind of file the shared selection modal is currently offering, and what
// clicking one does. The modal is a single instance reused by every tab, so the
// button that opened it says what it wanted; without this the Disk Info tab would
// get the ADF list and send a write command with an SCP path.
var fileselectMode = 'adf';

var FILESELECT_MODES = {
  adf: { title: 'Select ADF file from SD Card', match: function (f) { return f.isADF; }, pick: function (path) { writeADFFile(path); } },
  scp: { title: 'Select SCP image from SD Card', match: function (f) { return f.isSCP; }, pick: function (path) { diskInfoFile(path); } }
};

/*
   Open the selection modal in one of the modes above.

   Called from the button's onclick, before Bootstrap's data-bs-toggle shows the
   dialog, so the mode is set by the time drawSdFiles() runs.
*/
function openFileSelect(mode) {
  fileselectMode = mode;
  $('#staticBackdropLabel').html('<i class="fa-solid fa-sd-card"></i> ' + FILESELECT_MODES[mode].title);
  if (!sdFetched) { getSdFiles(sdPath); } else { drawSdFiles(); }
}

// Named so the modal's own onclick has something to call that survives a mode
// change; the row builder below closes over neither.
function fileselectPick(path) {
  FILESELECT_MODES[fileselectMode].pick(path);
}

function drawSdFiles() {
  // sd card table
  $('#sdcardPath').html(sdPath);
  $('#sdcardTable tbody').empty();

  if (sdPath != "/") {
    var parentPath = sdPath;
    if (parentPath.endsWith("/")) {
      parentPath = parentPath.substring(0, parentPath.length-1);
    }
    var parentPath = parentPath.substring(0, parentPath.lastIndexOf("/")+1);
    tablerow = "<tr><td colspan='4'><a onclick=\"getSdFiles('" + parentPath + "');\" href='#'><i class=\"fa-solid fa-circle-chevron-up\"></i> ..</a></td></tr>";
    $('#sdcardTable tbody').append(tablerow);
  }

  sdFiles.forEach(file => {
    var filename = file.name;
    if (file.isDir) { filename = "<span class='sdcardDirectory'>" + file.name + "</span>"; }
    if (file.isADF) { filename = "<span class='sdcardADF'>" + file.name + "</span>"; }
    if (file.isSCP) { filename = "<span class='sdcardSCP'>" + file.name + "</span>"; }
    if (file.isDir) {
      filename = "<a onclick=\"getSdFiles('" + sdPath + file.name + "/" + "');\" href=\"#\">" + filename + "</a>";
    } else {
      filename = "<a href='/sdcard" + sdPath + file.name + "'>" + filename + "</a>";
    }
    tablerow = "<tr><td>" + file.date + "</td><td>" + file.time + "</td><td>" + file.size + "</td><td>" + filename + "</td></tr>";
    $('#sdcardTable tbody').append(tablerow);
  });

  $('#sdcardFileCount').html(sdFiles.length);

  // file selection dialog
  $('#fileselectPath').html(sdPath);
  $('#fileselectTable tbody').empty();
  if (sdPath != "/") {
    var parentPath = sdPath;
    if (parentPath.endsWith("/")) {
      parentPath = parentPath.substring(0, parentPath.length-1);
    }
    var parentPath = parentPath.substring(0, parentPath.lastIndexOf("/")+1);
    tablerow = "<tr><td colspan='4'><a onclick=\"getSdFiles('" + parentPath + "');\" href='#'><i class=\"fa-solid fa-circle-chevron-up\"></i> ..</a></td></tr>";
    $('#fileselectTable tbody').append(tablerow);
  }

  var mode = FILESELECT_MODES[fileselectMode];

  sdFiles
    .filter(file => {
      return mode.match(file) == true || file.isDir == true;
    })
    .forEach(file => {
      var filename = file.name;
      if (file.isDir) { filename = "<span class='sdcardDirectory'>" + file.name + "</span>"; }
      if (file.isADF) { filename = "<span class='sdcardADF'>" + file.name + "</span>"; }
      if (file.isSCP) { filename = "<span class='sdcardSCP'>" + file.name + "</span>"; }
      if (file.isDir) {
        filename = "<a onclick=\"getSdFiles('" + sdPath + file.name + "/" + "');\" href=\"#\">" + filename + "</a>";
      } else {
        filename = "<a onclick=\"fileselectPick('" + sdPath + file.name + "');\" href=\"#\">" + filename + "</a>";
      }
      tablerow = "<tr><td>" + file.date + "</td><td>" + file.time + "</td><td>" + file.size + "</td><td>" + filename + "</td></tr>";
      $('#fileselectTable tbody').append(tablerow);
    });
}

function clearSdFiles() {
  sdFiles = [];
}

function addSdFile(details) {
  var values = details.split("&");
  file = {};
  file.date = values[0];
  file.time = values[1];
  file.size = values[2];
  file.name = values[3];
  file.isDir = values[4] == "1" ? true : false;
  file.isADF = values[5] == "1" ? true : false;
  file.isSCP = values[6] == "1" ? true : false;
  sdFiles.push(file);
}

function getSdFiles(path) {
    sdPath = path;
    sdFetched = true;
    wsSend("getSdFiles," + sdPath);
}