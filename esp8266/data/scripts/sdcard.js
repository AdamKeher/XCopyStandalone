var sdFiles = [];
var sdPath = "/"

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

  sdFiles
    .filter(file => {
      return file.isADF == true || file.isDir == true;
    })
    .forEach(file => {
      var filename = file.name;
      if (file.isDir) { filename = "<span class='sdcardDirectory'>" + file.name + "</span>"; }
      if (file.isADF) { filename = "<span class='sdcardADF'>" + file.name + "</span>"; }
      if (file.isDir) {
        filename = "<a onclick=\"getSdFiles('" + sdPath + file.name + "/" + "');\" href=\"#\">" + filename + "</a>";
      } else {
        filename = "<a onclick=\"writeADFFile('" + sdPath + file.name + "');\" href=\"#\">" + filename + "</a>";
      }
      tablerow = "<tr><td>" + file.date + "</td><td>" + file.time + "</td><td>" + file.size + "</td><td>" + filename + "</td></tr>";
      console.log(tablerow);
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
  sdFiles.push(file);
}