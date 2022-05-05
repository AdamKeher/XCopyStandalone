var currentBlock = 0;
var maxBlocks = (80 * 11 * 2) - 1;

function onLoad_DiskView() {
  // filter disk view input
  $('.numbers').keyup(function () { 
    this.value = this.value.replace(/[^0-9\.]/g,'');

    if (this.id == 'dvEditTrack' && parseInt(this.value) > 79) this.value = "79";
    if (this.id == 'dvEditTrack' && parseInt(this.value) < 0) this.value = "0";

    if (this.id == 'dvEditSector' && parseInt(this.value) > 10) this.value = "10";
    if (this.id == 'dvEditSector' && parseInt(this.value) < 0) this.value = "0";

    if (this.id == 'dvEditSide' && parseInt(this.value) > 1) this.value = "1";
    if (this.id == 'dvEditSide' && parseInt(this.value) < 0) this.value = "0";
  });

  // load block on direct edit
  $('.numbers').focusout(function () {
    size = this.id == 'dvEditSide' ? 1 : 2;
    this.value = String(this.value).padStart(size, '0');
    setBlock();
    getBlock(currentBlock);
  });

  // hook hover event on all hex chars for highlighting
  $('.hex').hover(
    function () {
      id = '#' + this.id.replace('hex', "ascii");
      $(id).addClass('asciiHighlight');
      id = '#' + this.id.replace('hex', "ascii2");
      $(id).addClass('asciiHighlight');
    }, 
    function () {
      id = '#' + this.id.replace('hex', "ascii");
      $(id).removeClass('asciiHighlight');
      id = '#' + this.id.replace('hex', "ascii2");
      $(id).removeClass('asciiHighlight');
  });    

  clearSectorHist();
}

function setBlock() {
  track = parseInt($('#dvEditTrack').val());
  sector = parseInt($('#dvEditSector').val());
  side = parseInt($('#dvEditSide').val());
  currentBlock = (track * 2 * 11) + sector + (side * 11);
}

function getBlock(block) {
  connection.send("getBlock," + block);
  drawSectorDetails(block);
}

function updateSideIcon() {
  side = $('#dvEditSide').val();
  $('#dvSideIcon').removeClass('fa-toggle-off');
  $('#dvSideIcon').removeClass('fa-toggle-on');
  $('#dvSideIcon').addClass(side == '0' ? 'fa-toggle-off' : 'fa-toggle-on');
  setBlock();
}

function toggleSide() {
  side = parseInt($('#dvEditSide').val());
  $('#dvEditSide').val(side  == '0' ? '1' : '0');
  updateSideIcon();
  getBlock(currentBlock);
}

function htmlEncode(value){
	//create a in-memory div, set it's inner text(which jQuery automatically encodes)
	//then grab the encoded contents back out. The div never exists on the page.
	return $('<div/>').text(value).html();
}

function prevBlock() {
  currentBlock--;
  if (currentBlock < 0) currentBlock = 0;
  getBlock(currentBlock);
}

function nextBlock() {
  currentBlock++;
  if (currentBlock > maxBlocks) currentBlock = maxBlocks;
  getBlock(currentBlock);
}

function prevTrack() {
  currentBlock = currentBlock - 22;
  if (currentBlock < 0) currentBlock = 0;
  getBlock(currentBlock);
}

function nextTrack() {
  currentBlock = currentBlock + 22;
  if (currentBlock > maxBlocks) currentBlock = maxBlocks;
  getBlock(currentBlock);
}

function hexToAscii(hex) {
  var str = "";
  for (var n = 0; n < hex.length; n += 2) {
    let c = parseInt(hex.substr(n, 2), 16);
    str += ((c < 32) | (c > 126)) ? "." : String.fromCharCode(c);
  }
  return str;
}

function decimalToHex(dec, len = 4) {
  let hex = parseInt(dec).toString(16).padStart(len, '0');
  return "0x" + hex;  
}

function drawSectorDetails(block) {
  $('#dvBlock').text(String(block).padStart(4, '0'));
  track = Math.floor(block / 22);
  sector = block % 22;
  side = sector < 11 ? 0 : 1;
  $('#dvEditTrack').val(String(track).padStart(2, '0'));
  $('#dvEditSector').val(String(sector % 11).padStart(2, '0'));
  $('#dvEditSide').val(side);
  
  updateSideIcon();
}

function drawSectorStats(block, track, errors, sectors, bits, format, gap, datachk, headerchk) {
  $('#dvTrackFound').text(String(track).padStart(2, '0'));
  $('#dvErrors').text(String(errors).padStart(3, '0'));
  $('#dvSectorCount').text(String(sectors).padStart(2, '0'));
  $('#dvBitCount').text(String(bits).padStart(5, '0'));
  $('#dvFormatType').text(String(format).padStart(3, '0'));
  $('#dvGap').text(String(gap).padStart(2, '0'));
  $('#dvDataChk').text(decimalToHex(datachk, 8));
  $('#dvheaderChk').text(decimalToHex(headerchk, 8));
}

function drawSector(block, row, sector) {
  let sectors = sector.split("|").filter(e => e);
  let count = 0;
  sectors.forEach(value => {
    $('#hex_' + row + '_' + count++).html(value);
  });

  let ascii = hexToAscii(sectors.join(""));
  for (var i=0; i < ascii.length; i++) {
    $('#ascii_' + row + '_' + i).html(htmlEncode(ascii.charAt(i)));
    $('#ascii2_' + row + '_' + i).html(htmlEncode(ascii.charAt(i)));
  }

  if (row == 15) {
    currentBlock = parseInt(block);
  }
}

function clearSectorHist() {
  canvas = document.getElementById('histCanvas');
  ctx = canvas.getContext("2d");  
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle =  "#0000FF";
  ctx.fillRect(100, 0, 1, canvas.height);
  ctx.fillRect(200, 0, 1, canvas.height);
  ctx.fillRect(300, 0, 1, canvas.height);
  ctx.fillRect(400, 0, 1, canvas.height);
  ctx.fillStyle =  "#00FF00";
  ctx.font = 'bold 10pt Calibri';
  ctx.fillText('2μs', 105, 10);
  ctx.fillText('4μs', 205, 10);
  ctx.fillText('6μs', 305, 10);
  ctx.fillText('8μs', 405, 10);
}

function drawSectorHist(line) {
  clearSectorHist();

  element = document.getElementById('histCanvas');
  ctx = element.getContext("2d");  
  ctx.fillStyle =  "#FFFFFF";
  let items = line.split('&');
  items.forEach(item => {
    values = item.split('|');
    height = Math.max(values[1] / 128.0, 1);
    ctx.fillRect(values[0] * 50, element.height - height, 1, height);
    console.log(values[0] + " | " + values[1] + "|" + height);
  });
}

function resetEmptyBlocks() {
  for (let track = 0; track < 80; track++) {
    for (let sector = 0; sector < 11; sector++) {
      $('#empty_' + track + '_0_' + sector).removeClass('empty').removeClass('full');
      $('#empty_' + track + '_1_' + sector).removeClass('empty').removeClass('full');
    }    
  }
}

function setEmptyBlock(track, side, sector, empty) {
  $('#empty_' + track + '_' + side + '_' + sector).removeClass('empty').removeClass('full');
  $('#empty_' + track + '_' + side + '_' + sector).addClass(empty ? 'empty' : 'full');
}
