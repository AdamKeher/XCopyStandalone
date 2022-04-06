var sdFiles = [];

function drawSdFiles() {
  $('#sdcardTable tbody').empty();

  sdFiles.forEach(file => {
    var filename = file.name;
    if (file.isDir) { filename = "<span class='sdcardDirectory'>" + file.name + "</span>"; }
    if (file.isADF) { filename = "<span class='sdcardADF'>" + file.name + "</span>"; }
    filename = "<a href='/scard/" + file.name + "'>" + filename + "</a>";
    tablerow = "<tr><td>" + file.date + "</td><td>" + file.time + "</td><td>" + file.size + "</td><td>" + filename + "</td></tr>";
    $('#sdcardTable tbody').append(tablerow);
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