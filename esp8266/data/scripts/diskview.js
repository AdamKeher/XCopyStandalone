var currentBlock = 0;
var maxBlocks = (80 * 11 * 2) - 1;

function htmlEncode(value){
	//create a in-memory div, set it's inner text(which jQuery automatically encodes)
	//then grab the encoded contents back out. The div never exists on the page.
	return $('<div/>').text(value).html();
}

function getBlock(block) {
  connection.send("getBlock," + block);
  $('#sectorTable').empty();
  $('#asciiTable').empty();
  drawSectorDetails(block);
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
  $('#dvTrack').text(String(track).padStart(2, '0'));
  $('#dvSector').text(String(sector % 11).padStart(2, '0'));
  $('#dvSide').text(side);
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
  let line = "<pre><span class=\"range\">";
  let range = row * 32;
  line += decimalToHex(range);
  line += "..";
  line += decimalToHex(range + 31);
  line += "</span>";
  sectors.forEach(value => {
    line += "<span class=\"hex\">" + value + "</span>";
  });

  line += "<span class=\"ascii\">";
  line += htmlEncode(hexToAscii(sectors.join("")));
  line += "</span>";

  line += "</pre>";
  $('#sectorTable').append(line);

  if (row == 15) {
    drawAsciiBlock();
    currentBlock = parseInt(block);
  }
}

function drawAsciiBlock() {
  let test = 0;
  let output = "<div><p>";
  let line = "";
  $('#sectorTable > pre').children('.ascii').each(function() {
    line += this.innerHTML;
    if (test++ % 2 == 1) {
      line = "<pre>; <span class=\"range\">" + decimalToHex((test-2) * 32) + ".." + decimalToHex(((test-2) * 32) + 63) + "</span><span class=\"ascii\">" + line + "</span></pre>";
      output += line;
      line = "";
    }
  })
  output += "</p></div>";
  $('#asciiTable').append(output);
}